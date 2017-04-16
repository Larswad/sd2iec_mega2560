/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   sdcard.c: SD/MMC access routines


   The exported functions in this file are weak-aliased to their corresponding
   versions defined in diskio.h so when this file is the only diskio provider
   compiled in they will be automatically used by the linker.

*/

#include "config.h"
#include "crc.h"
#include "diskio.h"
#include "spi.h"
#include "timer.h"
#include "uart.h"
#include "sdcard.h"

#ifdef CONFIG_TWINSD
#  define MAX_CARDS 2
#else
#  define MAX_CARDS 1
#endif

/* SD/MMC commands */
#define GO_IDLE_STATE             0x40
#define SEND_OP_COND              0x41
#define SWITCH_FUNC               0x46
#define SEND_IF_COND              0x48
#define SEND_CSD                  0x49
#define SEND_CID                  0x4a
#define STOP_TRANSMISSION         0x4c
#define SEND_STATUS               0x4d
#define SET_BLOCKLEN              0x50
#define READ_SINGLE_BLOCK         0x51
#define READ_MULTIPLE_BLOCK       0x52
#define WRITE_BLOCK               0x58
#define WRITE_MULTIPLE_BLOCK      0x59
#define PROGRAM_CSD               0x5b
#define SET_WRITE_PROT            0x5c
#define CLR_WRITE_PROT            0x5d
#define SEND_WRITE_PROT           0x5e
#define ERASE_WR_BLK_STAR_ADDR    0x60
#define ERASE_WR_BLK_END_ADDR     0x61
#define ERASE                     0x66
#define LOCK_UNLOCK               0x6a
#define APP_CMD                   0x77
#define GEN_CMD                   0x78
#define READ_OCR                  0x7a
#define CRC_ON_OFF                0x7b

/* SD ACMDs */
#define SD_STATUS                 0x4d
#define SD_SEND_NUM_WR_BLOCKS     0x56
#define SD_SET_WR_BLK_ERASE_COUNT 0x57
#define SD_SEND_OP_COND           0x69
#define SD_SET_CLR_CARD_DETECT    0x6a
#define SD_SEND_SCR               0x73

/* R1 status bits */
#define STATUS_IN_IDLE            0x01
#define STATUS_ERASE_RESET        0x02
#define STATUS_ILLEGAL_COMMAND    0x04
#define STATUS_CRC_ERROR          0x08
#define STATUS_ERASE_SEQ_ERROR    0x10
#define STATUS_ADDRESS_ERROR      0x20
#define STATUS_PARAMETER_ERROR    0x40

/* card types */
#define CARD_MMCSD 0
#define CARD_SDHC  1

static uint8_t cardtype[MAX_CARDS];

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * swap_word - swaps the bytes in a uint32 value
 * @input: input byte
 *
 * This function swaps the words in a uint32 value to convert
 * between little and big endian. Returns the word with bytes
 * swapped.
 */
#ifdef __thumb2__
static inline uint32_t swap_word(uint32_t input) {
  uint32_t result;
  asm("rev %[result], %[input]" : [result] "=r" (result) : [input] "r" (input));
  return result;
}
#else
static inline uint32_t swap_word(uint32_t input) {
  union {
    uint32_t val32;
    uint8_t  val8[4];
  } in,out;

  /* Compiles to a few movs on AVR */
  in.val32 = input;
  out.val8[0] = in.val8[3];
  out.val8[1] = in.val8[2];
  out.val8[2] = in.val8[1];
  out.val8[3] = in.val8[0];
  return out.val32;
}
#endif

/**
 * getbits - read value from bit buffer
 * @buffer: pointer to the data buffer
 * @start : index of the first bit in the value
 * @bits  : number of bits in the value
 *
 * This function returns a value from the memory region passed as
 * buffer, starting with bit "start" and "bits" bit long. The buffer
 * is assumed to be MSB first, passing 0 for start will read starting
 * from the highest-value bit of the first byte of the buffer.
 */
static uint32_t getbits(void *buffer, uint16_t start, int8_t bits) {
  uint8_t *buf = buffer;
  uint32_t result = 0;

  if ((start % 8) != 0) {
    /* Unaligned start */
    result += buf[start / 8] & (0xff >> (start % 8));
    bits  -= 8 - (start % 8);
    start += 8 - (start % 8);
  }
  while (bits >= 8) {
    result = (result << 8) + buf[start / 8];
    start += 8;
    bits -= 8;
  }
  if (bits > 0) {
    result = result << bits;
    result = result + (buf[start / 8] >> (8-bits));
  } else if (bits < 0) {
    /* Fraction of a single byte */
    result = result >> -bits;
  }
  return result;
}

/* ------------------------------------------------------------------------- */
/*  internal SD functions                                                    */
/* ------------------------------------------------------------------------- */

/* Detect changes of SD card 0 */
#ifdef SD_CHANGE_HANDLER
SD_CHANGE_HANDLER {
  if (sdcard_detect())
    disk_state = DISK_CHANGED;
  else
    disk_state = DISK_REMOVED;
}
#endif

#if defined(CONFIG_TWINSD) && defined(SD2_CHANGE_HANDLER)
/* Detect changes of SD card 1 */
SD2_CHANGE_HANDLER {
  if (sdcard2_detect())
    disk_state = DISK_CHANGED;
  else
    disk_state = DISK_REMOVED;
}
#endif

/* wait for a certain byte from the card */
/* (with 500ms timeout) */
static uint8_t expect_byte(uint8_t value) {
  uint8_t b;
  tick_t  timeout = getticks() + HZ/2;

  do {
    b = spi_rx_byte();
  } while (b != value && time_before(getticks(), timeout));

  return b == value;
}

/* deselect card(s) and send 24 clocks */
/* (was 8, but some cards prefer more) */
static void deselect_card(void) {
  spi_select_device(SPIDEV_NONE);
  set_sd_led(0);
  spi_rx_byte();
  spi_rx_byte();
  spi_rx_byte();
}

/* check if card in @drv is write protected */
static uint8_t sd_wrprot(uint8_t drv) {
#ifdef CONFIG_TWINSD
  if (drv != 0)
    return sdcard2_wp();
  else
#endif
    return sdcard_wp();
}

/**
 * sendCommand - send a command to the SD card
 * @card     : card number to be accessed
 * @cmd      : command to be sent
 * @parameter: parameter to be sent
 *
 * This function calculates the correct CRC7 for the command and
 * parameter and transmits all of it to the SD card.
 */
static uint8_t send_command(const uint8_t  card,
                            const uint8_t  cmd,
                            const uint32_t parameter) {
  tick_t   timeout;
  uint8_t  crc, errors, res;
  uint32_t tmp;

  // FIXME: Only works on little-endian
  // (but is much more efficient on AVR)
  union {
    uint32_t val32;
    uint8_t  val8[4];
  } convert;

  /* calculate CRC for command+parameter */
  convert.val32 = parameter;
  crc = crc7update(0,   cmd);
  crc = crc7update(crc, convert.val8[3]);
  crc = crc7update(crc, convert.val8[2]);
  crc = crc7update(crc, convert.val8[1]);
  crc = crc7update(crc, convert.val8[0]);
  crc = (crc << 1) | 1;

  errors = 0;
  while (errors < CONFIG_SD_AUTO_RETRIES) {
    /* select card */
    spi_select_device(card+1);
    set_sd_led(1);

    /* send command */
    spi_tx_byte(cmd);
    tmp = swap_word(parameter); // AVR spi_tx_block clobbers the buffer
    spi_tx_block(&tmp, 4);
    spi_tx_byte(crc);

    /* wait up to 500ms for a valid response */
    timeout = getticks() + HZ/2;
    do {
      res = spi_rx_byte();
    } while ((res & 0x80) &&
             time_before(getticks(), timeout));

    /* check for CRC error */
    if (res & STATUS_CRC_ERROR) {
      uart_putc('x');
      deselect_card();
      errors++;
      continue;
    }

    break; // FIXME: Ugly control flow
  }

  return res;
}

/* ------------------------------------------------------------------------- */
/*  external SD functions                                                    */
/* ------------------------------------------------------------------------- */

/**
 * sd_init - initialize SD interface
 *
 * This function initializes the SD interface hardware
 */
void sd_init(void) {
  sdcard_interface_init();
}
void disk_init(void) __attribute__ ((weak, alias("sd_init")));


/**
 * sd_status - get card status
 * @drv: drive
 *
 * This function reads the current status of the chosen
 * SD card. Returns STA_PROTECT for a write-protected
 * card, STA_NOINIT|STA_NODISK if no card is present in
 * the selected slot, RES_OK if a card is present and
 * not write-protected.
 */
DSTATUS sd_status(BYTE drv) {
#ifdef CONFIG_TWINSD
  if (drv != 0) {
    if (sdcard2_detect()) {
      if (sdcard2_wp()) {
        return STA_PROTECT;
      } else {
        return RES_OK;
      }
    } else {
      return STA_NOINIT | STA_NODISK;
    }
  } else
#endif
  if (sdcard_detect())
    if (sdcard_wp())
      return STA_PROTECT;
    else
      return RES_OK;
  else
    return STA_NOINIT | STA_NODISK;
}
DSTATUS disk_status(BYTE drv) __attribute__ ((weak, alias("sd_status")));


/**
 * sd_initialize - initialize SD card
 * @drv   : drive
 *
 * This function tries to initialize the selected SD card.
 */
DSTATUS sd_initialize(BYTE drv) {
  uint32_t parameter;
  uint16_t tries = 3;
  uint8_t  i,res;
  tick_t   timeout;

  if (drv >= MAX_CARDS)
    return STA_NOINIT | STA_NODISK;

  /* skip initialisation if the card is not present */
  if (sd_status(drv) & STA_NODISK)
    return sd_status(drv);

#ifdef SPI_LATE_INIT
  /* JLB: Should be in sd_init, but some uIEC versions have
   * IEC lines tied to SPI, so I moved it here to resolve the
   * conflict.
   */
  spi_init(SPI_SPEED_SLOW);
#else
  spi_set_speed(SPI_SPEED_SLOW);
#endif

 retry:
  disk_state = DISK_ERROR;
  cardtype[drv] = CARD_MMCSD;

  /* send 80 clocks with SS high */
  spi_select_device(SPIDEV_NONE);
  set_sd_led(0);

  for (i=0; i<10; i++)
    spi_tx_byte(0xff);

#ifdef CONFIG_TWINSD
  /* move both cards into SPI mode at the same time */
  if (drv == 0) {
    spi_select_device(SPIDEV_ALLCARDS);
    spi_tx_byte(GO_IDLE_STATE);
    parameter = 0;
    spi_tx_block(&parameter, 0);
    spi_tx_byte(0x95);

    /* send ten more bytes and ignore status */
    for (i=0; i<10; i++)
      spi_tx_byte(0xff);
  }
#endif

  /* switch card to idle state */
  res = send_command(drv, GO_IDLE_STATE, 0);
  deselect_card();
  if (res & 0x80)
    return STA_NOINIT;

  /* attempt initialisation again if it failed */
  if (res != 1) {
    if (--tries)
      goto retry;
    else
      return STA_NOINIT;
  }

  /* send interface conditions (required for SDHC) */
  res = send_command(drv, SEND_IF_COND, 0b000110101010);
  if (res == 1) {
    /* read answer and check */
    spi_rx_block(&parameter, 4);
    parameter = swap_word(parameter);
    deselect_card();
    if (((parameter >> 8) & 0x0f) != 0b0001)
      /* the card did not accept the voltage specs */
      return STA_NOINIT | STA_NODISK;

    /* do not check pattern echo because IIRC */
    /* some MMC cards would fail here */
  } else {
    deselect_card();
  }

  deselect_card();

  /* tell SD/SDHC cards to initialize */
  timeout = getticks() + HZ/2;
  do {
    /* send APP_CMD */
    res = send_command(drv, APP_CMD, 0);
    deselect_card();
    if (res != 1)
      goto not_sd;

    /* send SD_SEND_OP_COND */
    res = send_command(drv, SD_SEND_OP_COND, 1L<<30);
    deselect_card();
  } while (res == 1 && time_before(getticks(), timeout));

  /* failure just means that the card isn't SDHC */
  /* there are MMC cards that accept APP_CMD but not SD_SEND_OP_COND */
  if (res != 0)
    goto not_sd;

  /* send READ_OCR to detect SDHC cards */
  res = send_command(drv, READ_OCR, 0);

  if (res <= 1) {
    parameter = 0;
    spi_rx_block(&parameter, 4);

    /* check card type */
    if (parameter & swap_word(0x40000000))
      cardtype[drv] = CARD_SDHC;
  }

  deselect_card();

 not_sd:
  /* tell MMC cards to initialize (SD ignores this) */
  timeout = getticks() + HZ/2;
  do {
    res = send_command(drv, SEND_OP_COND, 1L<<30);
    deselect_card();
  } while (res != 0 && time_before(getticks(), timeout));

  if (res != 0)
    return STA_NOINIT;

  /* enable CRC checks */
  res = send_command(drv, CRC_ON_OFF, 1);
  deselect_card();
  if (res > 1)
    return STA_NOINIT | STA_NODISK;

  /* set block size to 512 */
  res = send_command(drv, SET_BLOCKLEN, 512);
  deselect_card();
  if (res != 0)
    return STA_NOINIT;

  spi_set_speed(SPI_SPEED_FAST);
  disk_state = DISK_OK;

  return sd_status(drv);
}
DSTATUS disk_initialize(BYTE drv) __attribute__ ((weak, alias("sd_initialize")));


/**
 * sd_read - reads sectors from the SD card to buffer
 * @drv   : drive
 * @buffer: pointer to the buffer
 * @sector: first sector to be read
 * @count : number of sectors to be read
 *
 * This function reads count sectors from the SD card starting
 * at sector to buffer. Returns RES_ERROR if an error occured or
 * RES_OK if successful. Up to SD_AUTO_RETRIES will be made if
 * the calculated data CRC does not match the one sent by the
 * card. If there were errors during the command transmission
 * disk_state will be set to DISK_ERROR and no retries are made.
 */
DRESULT sd_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) {
  uint8_t  res, sec, errors;
  uint16_t crc, recvcrc;

  if (drv >= MAX_CARDS)
    return RES_PARERR;

  /* convert sector number to byte offset for non-SDHC cards */
  if (cardtype[drv] == CARD_MMCSD)
    sector <<= 9;

  for (sec = 0; sec < count; sec++) {
    errors = 0;
    while (errors < CONFIG_SD_AUTO_RETRIES) {
      /* send read command */
      if (cardtype[drv] & CARD_SDHC)
        res = send_command(drv, READ_SINGLE_BLOCK, sector + sec);
      else
        res = send_command(drv, READ_SINGLE_BLOCK, sector + (sec << 9));

      /* fail if the command wasn't accepted */
      if (res != 0) {
        deselect_card();
        disk_state = DISK_ERROR;
        return RES_ERROR;
      }

      /* wait for start block token */
      if (!expect_byte(0xfe)) {
        deselect_card();
        disk_state = DISK_ERROR;
        return RES_ERROR;
      }

      /* transfer data */
      crc = 0;
#ifdef CONFIG_SD_BLOCKTRANSFER
      /* transfer data first, calculate CRC afterwards */
      spi_rx_block(buffer, 512);

      recvcrc = spi_rx_byte() << 8 | spi_rx_byte();
      crc = crc_xmodem_block(0, buffer, 512);
#else
      /* interleave transfer/CRC calculation, AVR-optimized */
      uint16_t i;
      uint8_t  tmp;
      BYTE     *ptr = buffer;

      /* start SPI data exchange */
      SPDR = 0xff;

      for (i=0; i<512; i++) {
        /* wait until byte available */
        loop_until_bit_is_set(SPSR, SPIF);
        tmp = SPDR;
        /* transmit the next byte while the current one is processed */
        SPDR = 0xff;

        *ptr++ = tmp;
        crc = crc_xmodem_update(crc, tmp);
      }
      /* wait for the first CRC byte */
      loop_until_bit_is_set(SPSR, SPIF);

      recvcrc  = SPDR << 8;
      recvcrc |= spi_rx_byte();
#endif

      /* check CRC */
      if (recvcrc != crc) {
        uart_putc('X');
        deselect_card();
        errors++;
        continue;
      }

      break; // FIXME: Ugly control flow
    }
    deselect_card();

    if (errors >= CONFIG_SD_AUTO_RETRIES)
      return RES_ERROR;

    buffer += 512;
  }

  return RES_OK;
}
DRESULT disk_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) __attribute__ ((weak, alias("sd_read")));


/**
 * sd_write - writes sectors from buffer to the SD card
 * @drv   : drive
 * @buffer: pointer to the buffer
 * @sector: first sector to be written
 * @count : number of sectors to be written
 *
 * This function writes count sectors from buffer to the SD card
 * starting at sector. Returns RES_ERROR if an error occured,
 * RES_WPRT if the card is currently write-protected or RES_OK
 * if successful. Up to SD_AUTO_RETRIES will be made if the card
 * signals a CRC error. If there were errors during the command
 * transmission disk_state will be set to DISK_ERROR and no retries
 * are made.
 */
DRESULT sd_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) {
  uint8_t  res, sec, errors;
  uint16_t crc;

  if (drv >= MAX_CARDS)
    return RES_PARERR;

  /* check write protect */
  if (sd_wrprot(drv))
    return RES_WRPRT;

  /* convert sector number to byte offset for non-SDHC cards */
  if (cardtype[drv] == CARD_MMCSD)
    sector <<= 9;

  for (sec = 0; sec < count; sec++) {
    errors = 0;
    while (errors < CONFIG_SD_AUTO_RETRIES) {
      /* send write command */
      if (cardtype[drv] & CARD_SDHC)
        res = send_command(drv, WRITE_BLOCK, sector + sec);
      else
        res = send_command(drv, WRITE_BLOCK, sector + (sec << 9));

      /* fail if the command wasn't accepted */
      if (res != 0) {
        deselect_card();
        disk_state = DISK_ERROR;
        return RES_ERROR;
      }

      /* send data token */
      spi_tx_byte(0xfe);

      /* transfer data */
#ifdef CONFIG_SD_BLOCKTRANSFER
      spi_tx_block(buffer, 512);
      crc = crc_xmodem_block(0, buffer, 512);
#else
      /* interleave transfer/CRC calculations, AVR-optimized */
      uint16_t i;
      const BYTE *ptr = buffer;

      crc = 0;
      spi_select_device(drv+1);
      for (i=0; i<512; i++) {
        SPDR = *ptr;
        crc = crc_xmodem_update(crc, *ptr++);
        loop_until_bit_is_set(SPSR, SPIF);
      }
#endif

      /* send CRC */
      spi_tx_byte(crc >> 8);
      spi_tx_byte(crc & 0xff);

      /* read status byte */
      res = spi_rx_byte();

      /* retry on error */
      if ((res & 0x0f) != 0x05) {
        uart_putc('X');
        deselect_card();
        errors++;
        continue;
      }

      /* wait until write is finished */
      // FIXME: Timeout?
      do {
        res = spi_rx_byte();
      } while (res == 0);

      break; // FIXME: Ugly control flow
    }
    deselect_card();

    if (errors >= CONFIG_SD_AUTO_RETRIES) {
      return RES_ERROR;
    }

    buffer += 512;
  }

  return RES_OK;
}
DRESULT disk_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) __attribute__ ((weak, alias("sd_write")));


/**
 * sd_getinfo - read card information
 * @drv   : drive
 * @page  : information page
 * @buffer: target buffer
 *
 * This function returns the requested information page @page
 * for card @drv in the buffer @buffer. Currently only page
 * 0 is supported which is the diskinfo0_t structure defined
 * in diskio.h. Returns a DRESULT to indicate success/failure.
 */
DRESULT sd_getinfo(BYTE drv, BYTE page, void *buffer) {
  uint8_t buf[18];
  uint8_t res;
  uint32_t capacity;

  if (drv >= MAX_CARDS)
    return RES_NOTRDY;

  if (sd_status(drv) & STA_NODISK)
    return RES_NOTRDY;

  if (page != 0)
    return RES_ERROR;

  /* Try to calculate the total number of sectors on the card */
  if (send_command(drv, SEND_CSD, 0) != 0) {
    deselect_card();
    return RES_ERROR;
  }

  /* Wait for data token */
  // FIXME: Timeout?
  do {
    res = spi_rx_byte();
  } while (res == 0);

  spi_rx_block(buf, 18);
  deselect_card();

  if (cardtype[drv] & CARD_SDHC) {
    /* Special CSD for SDHC cards */
    capacity = (1 + getbits(buf,127-69,22)) * 1024;
  } else {
    /* Assume that MMC-CSD 1.0/1.1/1.2 and SD-CSD 1.1 are the same... */
    uint8_t exponent = 2 + getbits(buf, 127-49, 3);
    capacity = 1 + getbits(buf, 127-73, 12);
    exponent += getbits(buf, 127-83,4) - 9;
    while (exponent--) capacity *= 2;
  }

  diskinfo0_t *di = buffer;
  di->validbytes  = sizeof(diskinfo0_t);
  di->disktype    = DISK_TYPE_SD;
  di->sectorsize  = 2;
  di->sectorcount = capacity;

  return RES_OK;
}
DRESULT disk_getinfo(BYTE drv, BYTE page, void *buffer) __attribute__ ((weak, alias("sd_getinfo")));
