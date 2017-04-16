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


   crc.h: Definitions for CRC calculation routines (AVR version)

*/

#ifndef CRC_H
#define CRC_H

#include <util/crc16.h>

uint8_t crc7update(uint8_t crc, uint8_t data);

#define crc_xmodem_update(crc, data) _crc_xmodem_update(crc, data)
#define crc16_update(crc, data) _crc16_update(crc, data)

/* Calculate a CRC over a block of data - more efficient if inlined */
static inline uint16_t crc_xmodem_block(uint16_t crc, const uint8_t *data, unsigned int length) {
  while (length--) {
    crc = _crc_xmodem_update(crc, *data++);
  }
  return crc;
}


#endif

