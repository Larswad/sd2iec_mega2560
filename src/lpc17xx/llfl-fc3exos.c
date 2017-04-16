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


   llfl-fc3exos.c: Low level handling of FC3/EXOS transfers

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "iec-bus.h"
#include "llfl-common.h"
#include "system.h"
#include "timer.h"
#include "fastloader-ll.h"


/* Used by the FC3 C part */
void clk_data_handshake(void) {
  set_clock(0);
  while (IEC_DATA && IEC_ATN) ;

  if (!IEC_ATN)
    return;

  set_clock(1);
  while (!IEC_DATA && IEC_ATN) ;
}

void fastloader_fc3_send_block(uint8_t *data) {
  uint32_t ticks;
  unsigned int byte, pair;

  llfl_setup();
  disable_interrupts();

  /* start in one microsecond */
  llfl_reference_time = llfl_now() + 10;
  llfl_set_clock_at(0, 0, WAIT);

  /* Transmit data */
  ticks = 120;
  for (byte = 0; byte < 4; byte++) {
    uint8_t value = *data++;

    for (pair = 0; pair < 4; pair++) {
      llfl_set_clock_at(ticks, value & 1, NO_WAIT);
      llfl_set_data_at (ticks, value & 2, WAIT);
      value >>= 2;
      ticks += 120;
    }
    ticks += 20;
  }

  llfl_set_clock_at(ticks, 1, NO_WAIT);
  llfl_set_data_at (ticks, 1, WAIT);
  // Note: Hold time is in the C part

  enable_interrupts();
  llfl_teardown();
}

static const generic_2bit_t fc3_get_def = {
  .pairtimes = {170, 300, 420, 520},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

uint8_t fc3_get_byte(void) {
  uint8_t result;

  llfl_setup();
  disable_interrupts();

  /* delay (guessed, see AVR version) */
  delay_us(10);

  /* wait for handshake */
  set_data(1);
  llfl_wait_clock(1, NO_ATNABORT);

  /* receive data */
  result = llfl_generic_save_2bit(&fc3_get_def);

  /* exit with data low */
  set_data(0);

  enable_interrupts();
  llfl_teardown();
  return result;
}

static const generic_2bit_t fc3_oldfreeze_pal_def = {
  .pairtimes = {140, 220, 300, 380},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0xff
};

static const generic_2bit_t fc3_oldfreeze_ntsc_def = {
  .pairtimes = {140, 240, 340, 440},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0xff
};

static uint8_t fc3_oldfreeze_send(const uint8_t byte,
                                  const generic_2bit_t *timingdef,
                                  unsigned int busytime) {
  llfl_setup();
  disable_interrupts();

  /* clear busy */
  set_clock(1);
  set_data(1);
  delay_us(15);
  if (!IEC_ATN)
    goto exit;

  /* wait for start signal */
  llfl_wait_clock(1, ATNABORT);
  if (!IEC_ATN)
    goto exit;

  /* transmit data */
  llfl_generic_load_2bit(timingdef, byte);

  /* re-enable busy signal */
  llfl_set_clock_at(busytime, 1, NO_WAIT);
  llfl_set_data_at (busytime, 0, WAIT);
  delay_us(1); // a little settle time for clock

 exit:
  enable_interrupts();
  llfl_teardown();
  return !IEC_ATN;
}

uint8_t fc3_oldfreeze_pal_send(const uint8_t byte) {
  return fc3_oldfreeze_send(byte, &fc3_oldfreeze_pal_def, 460);
}

uint8_t fc3_oldfreeze_ntsc_send(const uint8_t byte) {
  return fc3_oldfreeze_send(byte, &fc3_oldfreeze_ntsc_def, 520);
}
