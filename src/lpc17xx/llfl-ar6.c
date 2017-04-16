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


   llfl-ar6.c: Low level handling of AR6 fastloader

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "iec-bus.h"
#include "llfl-common.h"
#include "system.h"
#include "timer.h"
#include "fastloader-ll.h"


static const generic_2bit_t ar6_1581_send_def = {
  .pairtimes = {50, 130, 210, 290},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

static const generic_2bit_t ar6_1581p_get_def = {
  .pairtimes = {120, 220, 380, 480},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

void ar6_1581_send_byte(uint8_t byte) {
  llfl_setup();
  disable_interrupts();

  /* wait for handshake */
  set_clock(1);
  llfl_wait_data(1, NO_ATNABORT);

  /* transmit data */
  llfl_generic_load_2bit(&ar6_1581_send_def, byte);

  /* exit with clock low, data high */
  llfl_set_clock_at(375, 0, NO_WAIT);
  llfl_set_data_at( 375, 1, WAIT);

  /* short delay to make sure bus has settled */
  delay_us(10);

  enable_interrupts();
  llfl_teardown();
}

uint8_t ar6_1581p_get_byte(void) {
  uint8_t result;

  llfl_setup();
  disable_interrupts();

  set_clock(1);

  /* wait for handshake */
  while (IEC_DATA) ;
  llfl_wait_data(1, NO_ATNABORT);

  /* receive data */
  result = llfl_generic_save_2bit(&ar6_1581p_get_def);

  /* exit with clock low */
  llfl_set_clock_at(530, 0, WAIT);

  enable_interrupts();
  llfl_teardown();
  return result;
}
