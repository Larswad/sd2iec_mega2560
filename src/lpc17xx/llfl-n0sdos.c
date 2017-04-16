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


   llfl-n0sdos.c: Low level handling of N0stalgia fastloaders

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "iec-bus.h"
#include "llfl-common.h"
#include "system.h"
#include "timer.h"
#include "fastloader-ll.h"

static const generic_2bit_t n0sdos_send_def = {
  .pairtimes = {90, 170, 250, 330},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0xff
};

void n0sdos_send_byte(uint8_t byte) {
  llfl_setup();
  disable_interrupts();

  /* wait for handshake */
  set_clock(1);
  set_data(1);
  llfl_wait_clock(1, NO_ATNABORT);

  /* transmit data */
  llfl_generic_load_2bit(&n0sdos_send_def, byte);

  /* exit with clock high, data low */
  llfl_set_clock_at(380, 1, NO_WAIT);
  llfl_set_data_at( 380, 0, WAIT);

  /* C64 sets clock low at 42.5us, make sure we exit later than that */
  delay_us(6);

  enable_interrupts();
  llfl_teardown();
}
