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


   arch-eeprom.h: EEPROM access functions

*/

#ifndef ARCH_EEPROM_H
#define ARCH_EEPROM_H

/* EEPROM structure hack, section is mapped to 0x80000000 in the linker script */
#define EEMEM __attribute__((section(".eeprom")))

/* No safety required */
#define eeprom_safety() do {} while (0)

uint8_t  eeprom_read_byte(void *addr);
uint16_t eeprom_read_word(void *addr);
void     eeprom_read_block(void *destptr, void *addr, unsigned int length);
void     eeprom_write_byte(void *addr, uint8_t value);
void     eeprom_write_word(void *addr, uint16_t value);
void     eeprom_write_block(void *srcptr, void *addr, unsigned int length);

#endif
