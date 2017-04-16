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


   i2c.h: Definitions for I2C transfers

   There is no i2c.c, the functions defined here are currently implemented
   by softi2c.c. An additional implementation using the hardware I2C/TWI
   peripheral should implement the same functions so either implementation
   can be used.

*/

#ifndef I2C_H
#define I2C_H

#ifdef HAVE_I2C

typedef struct i2cblock_s {
  unsigned int       length;
  void              *data;
  struct i2cblock_s *next;
} i2cblock_t;

void i2c_init(void);
uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val);
uint8_t i2c_write_registers(uint8_t address, uint8_t startreg, uint8_t count, const void *data);
int16_t i2c_read_register(uint8_t address, uint8_t reg);
uint8_t i2c_read_registers(uint8_t address, uint8_t startreg, uint8_t count, void *data);

/* send a chain of i2cblock_t over the bus */
uint8_t i2c_write_blocks(uint8_t address, i2cblock_t *head);

/* write @writeblocks i2cblock_t over the bus, switch to read mode and fill the remaining ones */
uint8_t i2c_read_blocks(uint8_t address, i2cblock_t *head, unsigned char writeblocks);

#else
#  define i2c_init() do {} while (0)
#endif

#endif
