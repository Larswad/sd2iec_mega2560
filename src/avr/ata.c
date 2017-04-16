/*
    Copyright Jim Brain and Brain Innovations, 2005

    This file is part of uIEC.

    uIEC is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    uIEC is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with uIEC; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


    The exported functions in this file are weak-aliased to their corresponding
    versions defined in diskio.h so when this file is the only diskio provider
    compiled in they will be automatically used by the linker.

*/
#include <inttypes.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include "config.h"

#include "diskio.h"
#include "ata.h"

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/


static DSTATUS ATA_drv_flags[2];

#define ATA_WRITE_CMD(cmd) { ata_write_reg(ATA_REG_CMD,cmd); }

/* Yes, this is a very inaccurate delay mechanism, but this interface only
 * uses it for timeout that will cause a fatal error, so it should be fine.
 * About the only downside is a possible additional delay if a drive is not
 * present on the bus.
 */

#define DELAY_VALUE(ms) ((double)F_CPU/40000UL * ms)
#define ATA_INIT_TIMEOUT 31000  /* 31 sec */

/*-----------------------------------------------------------------------*/
/* Read an ATA register                                                  */
/*-----------------------------------------------------------------------*/

static BYTE ata_read_reg(BYTE reg) {
  BYTE data;

  ATA_PORT_CTRL_OUT = reg;
  ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_RD;
  ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_RD;
  ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_RD;
  data = ATA_PORT_DATA_LO_IN;
  ATA_PORT_CTRL_OUT |= ATA_PIN_RD;
  return data;
}


/*-----------------------------------------------------------------------*/
/* Write a byte to an ATA register                                       */
/*-----------------------------------------------------------------------*/

static void ata_write_reg(BYTE reg, BYTE data) {
  ATA_PORT_DATA_LO_DDR = 0xff;            /* bring to output */
  ATA_PORT_DATA_LO_OUT = data;
  ATA_PORT_CTRL_OUT = reg;
  ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_WR;
  ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_WR; /* delay */
  ATA_PORT_CTRL_OUT |= ATA_PIN_WR;
  ATA_PORT_DATA_LO_OUT = 0xff;
  ATA_PORT_DATA_LO_DDR = 0x00;            /* bring to input */
}


/*-----------------------------------------------------------------------*/
/* Wait for Data Ready                                                   */
/*-----------------------------------------------------------------------*/

static BOOL ata_wait_data(void) {
  BYTE s;
  DWORD i = DELAY_VALUE(1000);
  do {
    if(!--i) return FALSE;
    s=ata_read_reg(ATA_REG_STATUS);
  } while((s & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) != ATA_STATUS_DRQ && !(s & ATA_STATUS_ERR));
  //} while((s&ATA_STATUS_BSY)!= 0 && (s&ATA_STATUS_ERR)==0 && (s & (ATA_STATUS_DRDY | ATA_STATUS_DRQ)) != (ATA_STATUS_DRDY | ATA_STATUS_DRQ));
  if(s & ATA_STATUS_ERR)
    return FALSE;

  ata_read_reg(ATA_REG_ALTSTAT);
  return TRUE;
}


static void ata_select_sector(BYTE drv, DWORD sector, BYTE count) {
  if(ATA_drv_flags[drv] & STA_48BIT) {
    ata_write_reg (ATA_REG_COUNT, 0);
    ata_write_reg (ATA_REG_COUNT, count);
    ata_write_reg (ATA_REG_LBA0, (uint8_t)(sector >> 24));
    ata_write_reg (ATA_REG_LBA0, (uint8_t)sector);
    ata_write_reg (ATA_REG_LBA1, 0);
    ata_write_reg (ATA_REG_LBA1, (uint8_t)(sector >> 16));
    ata_write_reg (ATA_REG_LBA2, 0);
    ata_write_reg (ATA_REG_LBA2, (uint8_t)(sector >> 8));
    ata_write_reg (ATA_REG_LBA3, ATA_LBA3_LBA
                                 | (drv ? ATA_DEV_SLAVE : ATA_DEV_MASTER));
  } else {
    ata_write_reg(ATA_REG_COUNT, count);
    ata_write_reg(ATA_REG_LBA0, (BYTE)sector);
    ata_write_reg(ATA_REG_LBA1, (BYTE)(sector >> 8));
    ata_write_reg(ATA_REG_LBA2, (BYTE)(sector >> 16));
    ata_write_reg(ATA_REG_LBA3, ((BYTE)(sector >> 24) & 0x0F)
                                | ATA_LBA3_LBA
                                | ( drv ? ATA_DEV_SLAVE : ATA_DEV_MASTER));
  }
}


/*-----------------------------------------------------------------------*/
/* Read a part of data block                                             */
/*-----------------------------------------------------------------------*/

static void ata_read_part (BYTE *buff, BYTE ofs, BYTE count) {
  BYTE c = 0, dl, dh;

  ATA_PORT_CTRL_OUT = ATA_REG_DATA;          /* Select Data register */
  do {
    ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_RD;  /* IORD = L */
    ATA_PORT_CTRL_OUT &= (BYTE)~ATA_PIN_RD;  /* delay */
    dl = ATA_PORT_DATA_LO_IN;                /* Read even data */
    dh = ATA_PORT_DATA_HI_IN;                /* Read odd data */
    ATA_PORT_CTRL_OUT |= ATA_PIN_RD;         /* IORD = H */
    if (count && (c >= ofs)) {               /* Pick up a part of block */
      *buff++ = dl;
      *buff++ = dh;
      count--;
    }
  } while (++c);
  ata_read_reg(ATA_REG_ALTSTAT);
  ata_read_reg(ATA_REG_STATUS);
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


static void reset_disk(void) {
  ATA_PORT_CTRL_OUT = (BYTE)~ATA_PIN_RESET;
  // wait a bit for the drive to reset.
  _delay_ms(RESET_DELAY);
  ATA_PORT_CTRL_OUT |= ATA_PIN_RESET;
  ATA_drv_flags[0] = STA_NOINIT;
  ATA_drv_flags[1] = STA_NOINIT;
}


#ifdef CF_CHANGE_HANDLER
CF_CHANGE_HANDLER {
  if (cfcard_detect())
    disk_state = DISK_CHANGED;
  else
    disk_state = DISK_REMOVED;
}
#endif


void ata_init(void) {
  cfcard_interface_init();
  disk_state=DISK_OK;
  ATA_drv_flags[0] = STA_NOINIT | STA_FIRSTTIME;
  if(ATA_PORT_DATA_HI_OUT == 0xff)
    ATA_drv_flags[0] = STA_NOINIT | STA_NODISK;
  ATA_drv_flags[1] = STA_NOINIT | STA_FIRSTTIME;

  /* Initialize the ATA control port */
  ATA_PORT_CTRL_OUT=0xff;
  ATA_PORT_CTRL_DDR=0xff;
}
void disk_init(void) __attribute__ ((weak, alias("ata_init")));


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS ata_initialize (BYTE drv) {
  BYTE data[(83 - 49 + 1) * 2];
  DWORD i = DELAY_VALUE(ATA_INIT_TIMEOUT);

  if(drv>1) return STA_NOINIT;
  if(!(ATA_drv_flags[drv] & STA_FIRSTTIME) && disk_state != DISK_OK)
    reset_disk();
  if(ATA_drv_flags[drv] & STA_NODISK) return STA_NOINIT; // cannot initialize a drive with no disk in it.
  // we need to set the drive.
  ata_write_reg (ATA_REG_LBA3, ATA_LBA3_LBA | (drv ? ATA_DEV_SLAVE : ATA_DEV_MASTER));
  do {
    if (!--i) goto di_error;
  } while ((ata_read_reg(ATA_REG_STATUS) & (ATA_STATUS_BSY | ATA_STATUS_DRDY)) == ATA_STATUS_BSY);

  ata_write_reg(ATA_REG_DEVCTRL, ATA_DEVCTRL_SRST | ATA_DEVCTRL_NIEN);  /* Software reset */
  _delay_ms(20);
  ata_write_reg(ATA_REG_DEVCTRL, ATA_DEVCTRL_NIEN);
  _delay_ms(20);
  i = DELAY_VALUE(ATA_INIT_TIMEOUT);
  do {
    if (!--i) goto di_error;
  } while ((ata_read_reg(ATA_REG_STATUS) & (ATA_STATUS_DRDY|ATA_STATUS_BSY)) != ATA_STATUS_DRDY);

  ata_write_reg (ATA_REG_FEATURES, 3); /* set PIO mode 0 */
  ata_write_reg (ATA_REG_COUNT, 1);
  ATA_WRITE_CMD (ATA_CMD_SETFEATURES);
  i = DELAY_VALUE(1000);
  do {
    if(!--i) goto di_error;
  } while(ata_read_reg(ATA_REG_STATUS) & ATA_STATUS_BSY);  /* Wait cmd ready */
  ATA_WRITE_CMD(ATA_CMD_IDENTIFY);
  if(!ata_wait_data()) goto di_error;
  ata_read_part(data, 49, 83 - 49 + 1);
  if(!(data[1] & 0x02)) goto di_error; /* No LBA support */
  if(data[(83 - 49) * 2 + 1] & 0x04)   /* 48 bit addressing... */
    ATA_drv_flags[drv] |= STA_48BIT;
  ATA_drv_flags[drv] &= (BYTE)~( STA_NOINIT | STA_NODISK);

  disk_state = DISK_OK;
  return 0;

di_error:
  ATA_drv_flags[drv]=(STA_NOINIT | STA_NODISK); // no disk in drive
  return STA_NOINIT | STA_NODISK;
}
DSTATUS disk_initialize (BYTE drv) __attribute__ ((weak, alias("ata_initialize")));


/*-----------------------------------------------------------------------*/
/* Return Disk Status                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS ata_status (BYTE drv) {
  if(drv>1)
     return STA_NOINIT;
  return ATA_drv_flags[drv] & (STA_NOINIT | STA_NODISK);
}
DSTATUS disk_status (BYTE drv) __attribute__ ((weak, alias("ata_status")));


/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT ata_read (BYTE drv, BYTE *data, DWORD sector, BYTE count) {
  BYTE c, iord_l, iord_h;

  if (drv > 1 || !count) return RES_PARERR;
  if (ATA_drv_flags[drv] & STA_NOINIT) return RES_NOTRDY;

  /* Issue Read Sector(s) command */
  ata_select_sector(drv, sector, count);
  ATA_WRITE_CMD(ATA_drv_flags[drv] & STA_48BIT ? ATA_CMD_READ_EXT : ATA_CMD_READ);

  iord_h = ATA_REG_DATA;
  iord_l = ATA_REG_DATA & (BYTE)~ATA_PIN_RD;
  do {
    if (!ata_wait_data()) return RES_ERROR; /* Wait data ready */
    ATA_PORT_CTRL_OUT = ATA_REG_DATA;
    c = 0;
    do {
      ATA_PORT_CTRL_OUT = iord_l;       /* IORD = L */
      ATA_PORT_CTRL_OUT = iord_l;       /* delay */
      ATA_PORT_CTRL_OUT = iord_l;       /* delay */
      ATA_PORT_CTRL_OUT = iord_l;       /* delay */
      ATA_PORT_CTRL_OUT = iord_l;       /* delay */
      *data++ = ATA_PORT_DATA_LO_IN;    /* Get even data */
      *data++ = ATA_PORT_DATA_HI_IN;    /* Get odd data */
      ATA_PORT_CTRL_OUT = iord_h;       /* IORD = H */
      ATA_PORT_CTRL_OUT = iord_h;       /* delay */
      ATA_PORT_CTRL_OUT = iord_h;       /* delay */
      ATA_PORT_CTRL_OUT = iord_h;       /* delay */
    } while (++c);
  } while (--count);

  ata_read_reg(ATA_REG_ALTSTAT);
  ata_read_reg(ATA_REG_STATUS);

  return RES_OK;
}
DRESULT disk_read (BYTE drv, BYTE *data, DWORD sector, BYTE count) __attribute__ ((weak, alias("ata_read")));


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _READONLY == 0
DRESULT ata_write (BYTE drv, const BYTE *data, DWORD sector, BYTE count) {
  BYTE s, c, iowr_l, iowr_h;

  if (drv > 1 || !count) return RES_PARERR;
  if (ATA_drv_flags[drv] & STA_NOINIT) return RES_NOTRDY;

  /* Issue Write Setor(s) command */
  ata_select_sector(drv,sector,count);
  ATA_WRITE_CMD(ATA_drv_flags[drv] & STA_48BIT ? ATA_CMD_WRITE_EXT : ATA_CMD_WRITE);

  iowr_h = ATA_REG_DATA;
  iowr_l = ATA_REG_DATA & (BYTE)~ATA_PIN_WR;
  do {
    if (!ata_wait_data()) return RES_ERROR;
    ATA_PORT_CTRL_OUT = ATA_REG_DATA;
    ATA_PORT_DATA_LO_DDR = 0xff;      /* bring to output */
    ATA_PORT_DATA_HI_DDR = 0xff;      /* bring to output */
    c = 0;
    do {
      ATA_PORT_DATA_LO_OUT = *data++; /* Set even data */
      ATA_PORT_DATA_HI_OUT = *data++; /* Set odd data */
      ATA_PORT_CTRL_OUT = iowr_l;     /* IOWR = L */
      ATA_PORT_CTRL_OUT = iowr_h;     /* IOWR = H */
    } while (++c);
  } while (--count);
  ATA_PORT_DATA_LO_OUT = 0xff;        /* Set D0-D15 as input */
  ATA_PORT_DATA_HI_OUT = 0xff;
  ATA_PORT_DATA_LO_DDR = 0x00;        /* bring to input */
  ATA_PORT_DATA_HI_DDR = 0x00;        /* bring to input */

  DWORD i = DELAY_VALUE(1000);
  do {
    if(!--i) return RES_ERROR;
    s=ata_read_reg(ATA_REG_STATUS);   /* Wait cmd ready */
  } while(s & ATA_STATUS_BSY);
  if (s & ATA_STATUS_ERR) return RES_ERROR;
  ata_read_reg(ATA_REG_ALTSTAT);
  ata_read_reg(ATA_REG_STATUS);

  return RES_OK;
}
DRESULT disk_write (BYTE drv, const BYTE *data, DWORD sector, BYTE count) __attribute__ ((weak, alias("ata_write")));
#endif /* _READONLY == 0 */


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL != 0
DRESULT ata_ioctl (BYTE drv, BYTE ctrl, void *buff) {
  BYTE n, dl, dh, ofs, w, *ptr = buff;

  if (drv > 1) return RES_PARERR;
  if (ATA_drv_flags[drv] & STA_NOINIT) return RES_NOTRDY;

  switch (ctrl) {
    case GET_SECTOR_COUNT : /* Get number of sectors on the disk (DWORD) */
      ofs = 60; w = 2; n = 0;
      break;

    case GET_SECTOR_SIZE :  /* Get sectors on the disk (WORD) */
      *(WORD*)buff = 512;
      return RES_OK;

    case GET_BLOCK_SIZE :   /* Get erase block size in sectors (DWORD) */
      *(DWORD*)buff = 1;
      return RES_OK;

    case CTRL_SYNC :        /* Nothing to do */
      return RES_OK;

    case ATA_GET_REV :      /* Get firmware revision (8 chars) */
      ofs = 23; w = 4; n = 4;
      break;

    case ATA_GET_MODEL :    /* Get model name (40 chars) */
      ofs = 27; w = 20; n = 20;
      break;

    case ATA_GET_SN :       /* Get serial number (20 chars) */
      ofs = 10; w = 10; n = 10;
      break;

    default:
      return RES_PARERR;
  }

  ATA_WRITE_CMD(ATA_CMD_IDENTIFY);
  if (!ata_wait_data()) return RES_ERROR;
  ata_read_part(ptr, ofs, w);
  while (n--) {
    dl = *ptr; dh = *(ptr+1);
    *ptr++ = dh; *ptr++ = dl;
  }

  return RES_OK;
}
DRESULT disk_ioctl (BYTE drv, BYTE ctrl, void *buff) __attribute__ ((weak, alias("ata_ioctl")));
#endif /*  _USE_IOCTL != 0 */

DRESULT ata_getinfo(BYTE drv, BYTE page, void *buffer) {
  diskinfo0_t *di = buffer;

  if (page != 0)
    return RES_ERROR;

  ATA_WRITE_CMD(ATA_CMD_IDENTIFY);
  if (!ata_wait_data()) return RES_ERROR;

  ata_read_part((BYTE *)&di->sectorcount, 60, 2);

  di->validbytes = sizeof(diskinfo0_t);
  di->disktype   = DISK_TYPE_ATA;
  di->sectorsize = 2;

  return RES_OK;
}
DRESULT disk_getinfo(BYTE drv, BYTE page, void *buffer) __attribute__ ((weak, alias("ata_getinfo")));
