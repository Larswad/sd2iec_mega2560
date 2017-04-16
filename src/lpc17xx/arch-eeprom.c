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


   arch-eeprom.c: EEPROM access functions

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include "i2c.h"
#include "arch-eeprom.h"

#if I2C_EEPROM_SIZE > 256
#  define EEPROM_ADDR_BYTES 2
#  define EEPROM_ADDR_MASK  0xffff
#else
#  define EEPROM_ADDR_BYTES 1
#  define EEPROM_ADDR_MASK  0xff
#endif

static uint8_t addrbuf[EEPROM_ADDR_BYTES];
static i2cblock_t data_block;
static i2cblock_t addr_block = {
  EEPROM_ADDR_BYTES, &addrbuf, &data_block
};

/* set address byte(s) in addr_block */
static void set_address(uint16_t addr) {
  if (EEPROM_ADDR_BYTES == 2) {
    addrbuf[0] = (addr & 0xff00) >> 8;
    addrbuf[1] = addr & 0xff;
  } else {
    addrbuf[0] = addr & 0xff;
  }
}

/* converts from a pointer to an address in the EEPROM */
static uint16_t convert_address(void *a) {
  unsigned int addr = (unsigned int)a & EEPROM_ADDR_MASK;

  return addr;
}

static void wait_write_finish(void) {
  /* try reading until the chip responds */
  uint8_t dummy;

  data_block.length = 1;
  data_block.data = &dummy;
  while (i2c_read_blocks(I2C_EEPROM_ADDRESS, &addr_block, 1)) ;
}

uint8_t eeprom_read_byte(void *addr) {
  uint8_t val;

  set_address(convert_address(addr));
  data_block.data   = &val;
  data_block.length = 1;
  i2c_read_blocks(I2C_EEPROM_ADDRESS, &addr_block, 1);

  return val;
}

uint16_t eeprom_read_word(void *addr) {
  uint16_t val;

  set_address(convert_address(addr));
  data_block.data   = &val;
  data_block.length = 2;
  i2c_read_blocks(I2C_EEPROM_ADDRESS, &addr_block, 1);

  return val;
}

void eeprom_read_block(void *destptr, void *addr, unsigned int length) {
  set_address(convert_address(addr));

  data_block.length = length;
  data_block.data   = destptr;
  i2c_read_blocks(I2C_EEPROM_ADDRESS, &addr_block, 1);
}

void eeprom_write_byte(void *addr, uint8_t value) {
  set_address(convert_address(addr));

  data_block.data   = &value;
  data_block.length = 1;
  i2c_write_blocks(I2C_EEPROM_ADDRESS, &addr_block);

  wait_write_finish();
}

void eeprom_write_word(void *addr, uint16_t value) {
  eeprom_write_block(&value, addr, 2);
}

void eeprom_write_block(void *srcptr, void *addr, unsigned int length) {
  uint16_t address = convert_address(addr);
  uint8_t *srcbuf = srcptr;

  /* write data without crossing page boundaries */
  while (length > 0) {
    unsigned int curlen;

    /* limit to the current page */
    if (((address & (I2C_EEPROM_PAGESIZE - 1)) + length ) >= I2C_EEPROM_PAGESIZE) {
      curlen = I2C_EEPROM_PAGESIZE - (address & (I2C_EEPROM_PAGESIZE - 1));
    } else {
      curlen = length;
    }

    set_address(address);
    data_block.data   = srcbuf;
    data_block.length = curlen;

    i2c_write_blocks(I2C_EEPROM_ADDRESS, &addr_block);
    wait_write_finish();

    address += curlen;
    srcbuf  += curlen;
    length  -= curlen;
  }
}
