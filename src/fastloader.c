/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>
   Final Cartridge III, DreamLoad, ELoad fastloader support:
   Copyright (C) 2008-2011  Thomas Giesel <skoe@directbox.com>
   Nippon Loader support:
   Copyright (C) 2010  Joerg Jungermann <abra@borkum.net>

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


   fastloader.c: High level handling of fastloader protocols

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "diskchange.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "timer.h"
#include "fastloader.h"

uint8_t detected_loader;

/* Function pointer to the current byte transmit/receive functions */
/* (to simplify loaders with multiple variations of these)         */
uint8_t (*fast_send_byte)(uint8_t byte);
uint8_t (*fast_get_byte)(void);

/* track to load, used as a kind of jobcode */
volatile uint8_t fl_track;

/* sector to load, used as a kind of jobcode */
volatile uint8_t fl_sector;

#ifdef PARALLEL_ENABLED
/* parallel byte received */
volatile uint8_t parallel_rxflag;
#endif

/* Small helper for fastloaders that need to detect disk changes */
uint8_t check_keys(void) {
  /* Check for disk changes etc. */
  if (key_pressed(KEY_NEXT | KEY_PREV | KEY_HOME)) {
    change_disk();
  }
  if (key_pressed(KEY_SLEEP)) {
    reset_key(KEY_SLEEP);
    set_busy_led(0);
    set_dirty_led(1);

    /* wait for release */
    while (key_pressed(IGNORE_KEYS)) ;

    return 1;
  }

  return 0;
}


/*
 *
 *  GIJoe/EPYX common code
 *
 */
#if defined(CONFIG_LOADER_GIJOE) || defined(CONFIG_LOADER_EPYXCART)
/* Returns the byte read or <0 if the user aborts */
/* Aborting on ATN is not reliable for at least one version */
int16_t gijoe_read_byte(void) {
  uint8_t i;
  uint8_t value = 0;

  for (i=0;i<4;i++) {
    while (IEC_CLOCK)
      if (check_keys())
        return -1;

    value >>= 1;

    delay_us(3);
    if (!IEC_DATA)
      value |= 0x80;

    while (!IEC_CLOCK)
      if (check_keys())
        return -1;

    value >>= 1;

    delay_us(3);
    if (!IEC_DATA)
      value |= 0x80;
  }

  return value;
}
#endif


/*
 *
 *  Generic parallel speeder
 *
 */

#ifdef PARALLEL_ENABLED
/* parallel handshake interrupt handler */
PARALLEL_HANDLER {
  parallel_rxflag = 1;
}
#endif
