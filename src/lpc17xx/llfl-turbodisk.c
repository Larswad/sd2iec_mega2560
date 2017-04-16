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


   llfl-turbodisk.c: Low level handling of Turbodisk fastloader

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "iec-bus.h"
#include "llfl-common.h"
#include "timer.h"
#include "fastloader-ll.h"

static const generic_2bit_t turbodisk_byte_def = {
  .pairtimes = {310, 600, 890, 1180},
  .clockbits = {7, 5, 3, 1},
  .databits  = {6, 4, 2, 0},
  .eorvalue  = 0
};

void turbodisk_byte(uint8_t value) {
  llfl_setup();

  /* wait for handshake */
  while (IEC_DATA) ;
  set_clock(1);
  llfl_wait_data(1, NO_ATNABORT);

  /* transmit data */
  llfl_generic_load_2bit(&turbodisk_byte_def, value);

  /* exit with clock low, data high */
  llfl_set_clock_at(1470, 0, NO_WAIT);
  llfl_set_data_at( 1470, 1, WAIT);
  delay_us(5);

  llfl_teardown();
}

void turbodisk_buffer(uint8_t *data, uint8_t length) {
  unsigned int pair;
  uint32_t ticks;

  llfl_setup();

  /* wait for handshake */
  while (IEC_DATA) ;
  set_clock(1);
  llfl_wait_data(1, NO_ATNABORT);

  ticks = 70;
  while (length--) {
    uint8_t byte = *data++;

    ticks += 120;
    for (pair = 0; pair < 4; pair++) {
      ticks += 240;
      llfl_set_clock_at(ticks, byte & 0x80, NO_WAIT);
      llfl_set_data_at (ticks, byte & 0x40, WAIT);
      ticks += 50;
      byte <<= 2;
    }
    ticks += 100;
  }

  ticks += 110;

  llfl_set_clock_at(ticks, 0, NO_WAIT);
  llfl_set_data_at( ticks, 1, WAIT);
  delay_us(5);

  llfl_teardown();
}
