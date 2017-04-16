/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>
   Final Cartridge III, DreamLoad, ELoad fastloader support:
   Copyright (C) 2008-2011  Thomas Giesel <skoe@directbox.com>
   Nippon Loader support:
   Copyright (C) 2010  Joerg Jungermann <abra@borkum.net>

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


   fl-geos.c: High level handling of GEOS/Wheels fastloaders

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "d64ops.h"
#include "display.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fastloader-ll.h"
#include "fileops.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "uart.h"
#include "ustring.h"
#include "wrapops.h"
#include "fastloader.h"


/*
 *
 *  Main GEOS code, partially re-used by Wheels below
 *
 */

/* Receive a fixed-length data block */
static void geos_receive_datablock(void *data_, uint16_t length) {
  uint8_t *data = (uint8_t*)data_;

  data += length-1;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    while (!IEC_CLOCK);
    set_data(1);
    while (length--)
      *data-- = fast_get_byte();
    set_data(0);
  }
}

/* Receive a data block from the computer */
static void geos_receive_lenblock(uint8_t *data) {
  uint8_t exitflag = 0;
  uint16_t length = 0;

  /* Receive data length */
  while (!IEC_CLOCK && IEC_ATN)
    if (check_keys()) {
      /* User-requested exit */
      exitflag = 1;
      break;
    }

  /* Exit if ATN is low */
  if (!IEC_ATN || exitflag) {
    *data++ = 0;
    *data   = 0;
    return;
  }

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    set_data(1);
    length = fast_get_byte();
    set_data(0);
  }

  if (length == 0)
    length = 256;

  geos_receive_datablock(data, length);
}

/* Send a single byte to the computer after waiting for CLOCK high */
static void geos_transmit_byte_wait(uint8_t byte) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    /* Wait until clock is high */
    while (!IEC_CLOCK) ;
    set_data(1);

    /* Send byte */
    fast_send_byte(byte);
    set_clock(1);
    set_data(0);
  }

  delay_us(25); // educated guess
}

/* Send data block to computer */
static void geos_transmit_buffer_s3(uint8_t *data, uint16_t len) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Wait until clock is high */
    while (!IEC_CLOCK) ;
    set_data(1);

    /* Send data block */
    uint16_t i = len;
    data += len - 1;

    while (i--) {
      fast_send_byte(*data--);
    }

    set_clock(1);
    set_data(0);

    delay_us(15); // guessed
  }
}

static void geos_transmit_buffer_s2(uint8_t *data, uint16_t len) {
  /* Send length byte */
  geos_transmit_byte_wait(len);

  /* the rest is the same as in stage 3 */
  geos_transmit_buffer_s3(data, len);
}

/* Send job status to computer */
static void geos_transmit_status(void) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Send a single 1 byte as length indicator */
    geos_transmit_byte_wait(1);

    /* Send (faked) job result code */
    if (current_error == 0)
      geos_transmit_byte_wait(1);
    else
      if (current_error == ERROR_WRITE_PROTECT)
        geos_transmit_byte_wait(8);
      else
        geos_transmit_byte_wait(2); // random non-ok job status
  }
}

/* GEOS READ operation */
static void geos_read_sector(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('R');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  read_sector(buf, current_part, track, sector);
}

/* GEOS WRITE operation */
static void geos_write_sector_41(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('W');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  /* Provide "unwritten data present" feedback */
  mark_buffer_dirty(buf);

  /* Receive data */
  geos_receive_lenblock(buf->data);

  /* Write to image */
  write_sector(buf, current_part, track, sector);

  /* Reset "unwritten data" feedback */
  mark_buffer_clean(buf);
}

/* GEOS WRITE_71 operation */
static void geos_write_sector_71(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('W');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  /* Provide "unwritten data present" feedback */
  mark_buffer_dirty(buf);

  /* Receive data */
  geos_receive_datablock(buf->data, 256);

  /* Write to image */
  write_sector(buf, current_part, track, sector);

  /* Send status */
  geos_transmit_status();

  /* Reset "unwritten data" feedback */
  mark_buffer_clean(buf);
}


/* GEOS stage 2/3 loader */
void load_geos(UNUSED_PARAMETER) {
  buffer_t *cmdbuf = alloc_system_buffer();
  buffer_t *databuf = alloc_system_buffer();
  uint8_t *cmddata;
  uint16_t cmd;

  if (!cmdbuf || !databuf)
    return;

  cmddata = cmdbuf->data;

  /* Initial handshake */
  uart_flush();
  delay_ms(1);
  set_data(0);
  while (IEC_CLOCK) ;

  while (1) {
    /* Receive command block */
    set_busy_led(0);
    geos_receive_lenblock(cmddata);
    set_busy_led(1);

    //uart_trace(cmddata, 0, 4);

    cmd = cmddata[0] | (cmddata[1] << 8);

    switch (cmd) {
    case 0x0320: // 1541 stage 3 transmit
      geos_transmit_buffer_s3(databuf->data, 256);
      geos_transmit_status();
      break;

    case 0x031f: // 1571; 1541 stage 2 status (only seen in GEOS 1.3)
                 // 1581 transmit
      if (detected_loader == FL_GEOS_S23_1581) {
        if (cmddata[2] & 0x80) {
          geos_transmit_buffer_s3(databuf->data, 2);
        } else {
          geos_transmit_buffer_s3(databuf->data, 256);
        }
      }
      geos_transmit_status();
      break;

    case 0x0325: // 1541 stage 3 status
    case 0x032b: // 1581 status
      geos_transmit_status();
      break;

    case 0x0000: // internal QUIT
    case 0x0412: // 1541 stage 2 quit
    case 0x0420: // 1541 stage 3 quit
    case 0x0457: // 1581 quit
    case 0x0475: // 1571 stage 3 quit
      while (!IEC_CLOCK) ;
      set_data(1);
      return;

    case 0x0432: // 1541 stage 2 transmit
      if (current_error != 0) {
        geos_transmit_status();
      } else {
        geos_transmit_buffer_s2(databuf->data, 256);
      }
      break;

    case 0x0439: // 1541 stage 3 set address
    case 0x04a5: // 1571 stage 3 set address
      // Note: identical in stage 2, address 0428, probably unused
      device_address = cmddata[2] & 0x1f;
      display_address(device_address);
      break;

    case 0x049b: // 1581 initialize
    case 0x04b9: // 1581 flush
    case 0x04dc: // 1541 stage 3 initialize
    case 0x0504: // 1541 stage 2 initialize - only seen in GEOS 1.3
    case 0x057e: // 1571 initialize
      /* Doesn't do anything that needs to be reimplemented */
      break;

    case 0x057c: // 1541 stage 2/3 write
      geos_write_sector_41(cmddata[2], cmddata[3], databuf);
      break;

    case 0x058e: // 1541 stage 2/3 read
    case 0x04cc: // 1581 read
      geos_read_sector(cmddata[2] & 0x7f, cmddata[3], databuf);
      break;

    case 0x04af: // 1571 read_and_send
      geos_read_sector(cmddata[2], cmddata[3], databuf);
      geos_transmit_buffer_s3(databuf->data, 256);
      geos_transmit_status();
      break;

    case 0x047c: // 1581 write
    case 0x05fe: // 1571 write
      geos_write_sector_71(cmddata[2], cmddata[3], databuf);
      break;

    default:
      uart_puts_P(PSTR("unknown:\r\n"));
      uart_trace(cmddata, 0, 4);
      return;
    }
  }
}

/* Stage 1 only - send a sector chain to the computer */
static void geos_send_chain(uint8_t track, uint8_t sector,
                            buffer_t *buf, uint8_t *key) {
  uint8_t bytes;
  uint8_t *keyptr,*dataptr;

  do {
    /* Read sector - no error recovery on computer side */
    read_sector(buf, current_part, track, sector);

    /* Decrypt contents if we have a key */
    if (key != NULL) {
      keyptr = key;
      dataptr = buf->data + 2;
      bytes = 254;
      while (bytes--)
        *dataptr++ ^= *keyptr++;
    }

    /* Read link pointer */
    track = buf->data[0];
    sector = buf->data[1];

    if (track == 0) {
      bytes = sector - 1;
    } else {
      bytes = 254;
    }

    /* Send buffer contents */
    geos_transmit_buffer_s2(buf->data + 2, bytes);
  } while (track != 0);

  geos_transmit_byte_wait(0);
}

static const PROGMEM uint8_t geos64_chains[] = {
  19, 13,
  20, 15,
  20, 17,
  0
};

static const PROGMEM uint8_t geos128_chains[] = {
  19, 12,
  20, 15,
  23, 6,
  24, 4,
  0
};

/* GEOS 64 stage 1 loader */
void load_geos_s1(uint8_t version) {
  buffer_t *encrbuf = find_buffer(BUFFER_SYS_CAPTURE1);
  buffer_t *databuf = alloc_buffer();
  uint8_t *encdata = NULL;
  uint8_t track, sector;
  const uint8_t *chainptr;

  if (!encrbuf || !databuf)
    return;

  if (version == 0) {
    chainptr = geos64_chains;
  } else {
    chainptr = geos128_chains;
  }

  /* Initial handshake */
  uart_flush();
  delay_ms(1);
  set_data(0);
  while (IEC_CLOCK) ;

  /* Send sector chains */
  while (1) {
    track = pgm_read_byte(chainptr++);

    if (track == 0)
      break;

    sector = pgm_read_byte(chainptr++);

    /* Transfer sector chain */
    geos_send_chain(track, sector, databuf, encdata);

    /* Turn on decryption after the first chain */
    encdata = encrbuf->data;
  }

  /* Done! */
  free_buffer(encrbuf);
  set_data(1);
}


/*
 *
 *  Wheels
 *
 */

/* Send data block to computer */
static void wheels44_transmit_buffer(uint8_t *data, uint16_t len) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Wait until clock is high */
    while (!IEC_CLOCK) ;
    set_data(1);

    /* Send data block */
    uint16_t i = len;
    data += len - 1;

    while (i--) {
      fast_send_byte(*data--);
    }

    set_clock(1);
    set_data(1);

    delay_us(5);
    while (IEC_CLOCK) ;

    set_data(0);
    delay_us(15); // guessed
  }
}

/* Send a single byte to the computer after waiting for CLOCK high */
static void wheels_transmit_byte_wait(uint8_t byte) {
  if (detected_loader == FL_WHEELS44_S2_1581) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      /* Wait until clock is high */
      while (!IEC_CLOCK) ;
      set_data(1);

      /* Send byte */
      fast_send_byte(byte);
      set_clock(1);
      set_data(1);

      delay_us(5);
      while (IEC_CLOCK) ;
      set_data(0);
    }

    delay_us(15); // educated guess
  } else {
    geos_transmit_byte_wait(byte);
    delay_us(15); // educated guess
    while (IEC_CLOCK) ;
  }
}

/* Send a fixed-length data block */
static void wheels_transmit_datablock(void *data_, uint16_t length) {
  uint8_t *data = (uint8_t*)data_;

  if (detected_loader == FL_WHEELS44_S2_1581) {
    wheels44_transmit_buffer(data, length);
  } else {
    geos_transmit_buffer_s3(data, length);

    while (IEC_CLOCK) ;
  }
}

/* Receive a fixed-length data block */
static void wheels_receive_datablock(void *data_, uint16_t length) {
  uint8_t *data = (uint8_t*)data_;

  data += length-1;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    while (!IEC_CLOCK) ;
    set_data(1);
    while (length--)
      *data-- = fast_get_byte();

    if (detected_loader == FL_WHEELS44_S2 ||
        detected_loader == FL_WHEELS44_S2_1581)
      while (IEC_CLOCK) ;

    set_data(0);
  }
}

/* Wheels STATUS operation (030f) */
static void wheels_transmit_status(void) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Send (faked) job result code */
    if (current_error == 0)
      wheels_transmit_byte_wait(1);
    else
      if (current_error == ERROR_WRITE_PROTECT)
        wheels_transmit_byte_wait(8);
      else
        wheels_transmit_byte_wait(2); // random non-ok job status
  }
}

/* Wheels CHECK_CHANGE operation (031b) */
static void wheels_check_diskchange(void) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    if (dir_changed) {
      /* Disk has changed */
      wheels_transmit_byte_wait(3);
    } else {
      /* Disk not changed */
      if (detected_loader == FL_WHEELS44_S2 &&
          (partition[current_part].imagetype & D64_TYPE_MASK) == D64_TYPE_D71) {
        wheels_transmit_byte_wait(0x80);
      } else {
        wheels_transmit_byte_wait(0);
      }
    }

    dir_changed = 0;
    while (IEC_CLOCK) ;
  }
}

/* Wheels WRITE operation (0306) */
static void wheels_write_sector(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('W');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  /* Provide "unwritten data present" feedback */
  mark_buffer_dirty(buf);

  /* Receive data */
  wheels_receive_datablock(buf->data, 256);

  /* Write to image */
  write_sector(buf, current_part, track, sector);

  /* Send status */
  wheels_transmit_status();

  /* Reset "unwritten data" feedback */
  mark_buffer_clean(buf);
}

/* Wheels READ operation (0309) */
static void wheels_read_sector(uint8_t track, uint8_t sector, buffer_t *buf, uint16_t bytes) {
  uart_putc('R');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  read_sector(buf, current_part, track, sector);
  wheels_transmit_datablock(buf->data, bytes);
  wheels_transmit_status();
}

/* Wheels NATIVE_FREE operation (0312) */
static void wheels_native_free(void) {
  /* Cheat: Ignore the limits set by the C64 */
  uint16_t freeblocks;

  freeblocks = disk_free(current_part);
  wheels_transmit_datablock(&freeblocks, 2);
  wheels_transmit_status();
}

/* Wheels GET_CURRENT_PART_DIR (0315) */
static void wheels_get_current_part_dir(void) {
  uint8_t data[3];

  data[0] = partition[current_part].current_dir.dxx.track;
  data[1] = partition[current_part].current_dir.dxx.sector;
  data[2] = current_part+1;
  wheels_transmit_datablock(&data, 3);
}

/* Wheels SET_CURRENT_PART_DIR operation (0318) */
static void wheels_set_current_part_dir(void) {
  uint8_t data[3];

  wheels_receive_datablock(&data, 3);

  if (data[2] != 0)
    current_part = data[2]-1;

  partition[current_part].current_dir.dxx.track  = data[0];
  partition[current_part].current_dir.dxx.sector = data[1];
}

/* -------- */

/* Wheels stage 1 loader */
void load_wheels_s1(const uint8_t version) {
  buffer_t *buf;

  uart_flush();
  delay_ms(2);
  while (IEC_CLOCK) ;
  set_data(0);

  /* copy file name to command buffer */
  if (version == 0) {
    ustrcpy_P(command_buffer, PSTR("SYSTEM1"));
  } else {
    ustrcpy_P(command_buffer, PSTR("128SYSTEM1"));
  }
  command_length = ustrlen(command_buffer);

  /* open file */
  file_open(0);
  buf = find_buffer(0);
  if (!buf)
    goto wheels_exit;

  while (1) {
    /* Transmit current sector */
    wheels_transmit_datablock(buf->data, 256);

    /* Check for last sector */
    if (buf->sendeoi)
      break;

    /* Advance to next sector */
    if (buf->refill(buf)) {
      /* Error, abort - original loader doesn't handle this */
      break;
    }
  }

 wheels_exit:
  while (!IEC_CLOCK) ;
  set_data(1);
  set_clock(1);
  if (buf) {
    cleanup_and_free_buffer(buf);
  }
}

/* Wheels stage 2 loader */
void load_wheels_s2(UNUSED_PARAMETER) {
  struct {
    uint16_t address;
    uint8_t  track;
    uint8_t  sector;
  } cmdbuffer;
  buffer_t *databuf = alloc_system_buffer();

  if (!databuf)
    return;

  /* Initial handshake */
  uart_flush();
  delay_ms(1);
  while (IEC_CLOCK) ;
  set_data(0);
  set_clock(1);
  delay_us(3);

  while (1) {
    /* Receive command block - redundant clock line check for check_keys */
    while (!IEC_CLOCK && IEC_ATN) {
      if (check_keys()) {
        /* User-requested exit */
        return;
      }
    }

    wheels_receive_datablock(&cmdbuffer, 4);
    set_busy_led(1);
    //uart_trace(&cmdbuffer, 0, 4);

    switch (cmdbuffer.address & 0xff) {
    case 0x03: // QUIT
      while (!IEC_CLOCK) ;
      set_data(1);
      return;

    case 0x06: // WRITE
      wheels_write_sector(cmdbuffer.track, cmdbuffer.sector, databuf);
      break;

    case 0x09: // READ
      wheels_read_sector(cmdbuffer.track, cmdbuffer.sector, databuf, 256);
      break;

    case 0x0c: // READLINK
      wheels_read_sector(cmdbuffer.track, cmdbuffer.sector, databuf, 2);
      break;

    case 0x0f: // STATUS
      wheels_transmit_status();
      break;

    case 0x12: // NATIVE_FREE
      wheels_native_free();
      break;

    case 0x15: // GET_CURRENT_PART_DIR
      wheels_get_current_part_dir();
      break;

    case 0x18: // SET_CURRENT_PART_DIR
      wheels_set_current_part_dir();
      break;

    case 0x1b: // CHECK_CHANGE
      wheels_check_diskchange();
      break;

    default:
      uart_puts_P(PSTR("unknown:\r\n"));
      uart_trace(&cmdbuffer, 0, 4);
      return;
    }

    set_busy_led(0);

    /* wait until clock is low */
    while (IEC_CLOCK && IEC_ATN) {
      if (check_keys()) {
        /* User-requested exit */
        return;
      }
    }
  }
}
