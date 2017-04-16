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


   llfl-dreamload.c: Low level handling of Dreamload fastloader

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "fastloader.h" // for fl_track etc.
#include "iec-bus.h"
#include "llfl-common.h"
#include "timer.h"
#include "fastloader-ll.h"


void dreamload_send_byte(uint8_t byte) {
  unsigned int i;

  for (i=0; i<2; i++) {
    /* send bits 0,1 to bus */
    set_clock(byte & 1);
    set_data (byte & 2);

    /* wait until ATN is low */
    while (IEC_ATN) ;

    /* send bits 2,3 to bus */
    set_clock(byte & 4);
    set_data (byte & 8);

    /* wait until ATN is high */
    while (!IEC_ATN) ;

    /* move upper nibble down */
    byte >>= 4;
  }
}

uint8_t dreamload_get_byte(void) {
  unsigned int i;
  uint8_t result = 0;

  for (i=0;i<4;i++) {
    /* wait until clock is low */
    while (IEC_CLOCK) ;

    /* read data bit a moment later */
    delay_us(3);
    result = (result << 1) | !IEC_DATA;

    /* wait until clock is high */
    while (!IEC_CLOCK) ;

    /* read data bit a moment later */
    delay_us(3);
    result = (result << 1) | !IEC_DATA;
  }

  return result;
}

static uint8_t dreamload_get_byte_old(void) {
  unsigned int i;
  iec_bus_t tmp;
  uint8_t result = 0;

  for (i=0; i<2; i++) {
    /* shift bits to upper nibble */
    result <<= 4;

    /* wait until ATN is low */
    while (IEC_ATN) ;

    /* read two bits a moment later */
    delay_us(3);
    tmp = iec_bus_read();
    result |= (!(tmp & IEC_BIT_CLOCK)) << 3;
    result |= (!(tmp & IEC_BIT_DATA))  << 1;

    /* wait until ATN is high */
    while (!IEC_ATN) ;

    /* read two bits a moment later */
    delay_us(3);
    tmp = iec_bus_read();
    result |= (!(tmp & IEC_BIT_CLOCK)) << 2;
    result |= (!(tmp & IEC_BIT_DATA))  << 0;
  }

  return result;
}

IEC_ATN_HANDLER {
  /* just return if ATN is high */
  if (IEC_ATN)
    return;

  if (detected_loader == FL_DREAMLOAD_OLD) {
    /* handle Dreamload (old) if it's the current fast loader */
    fl_track  = dreamload_get_byte_old();
    fl_sector = dreamload_get_byte_old();
  } else {
    /* standard ATN acknowledge */
    set_data(0);
  }
}

IEC_CLOCK_HANDLER {
  if (detected_loader == FL_DREAMLOAD && !IEC_CLOCK) {
    /* handle Dreamload if it's the current fast loader and clock is low */
    fl_track  = dreamload_get_byte();
    fl_sector = dreamload_get_byte();
  }
}
