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


   fl-dreamload.c: High level handling of Dreamload

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "diskchange.h"
#include "doscmd.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "wrapops.h"
#include "fastloader.h"



#ifndef HAVE_CLOCK_IRQ
#  error "Sorry, DreamLoad is only supported on platforms with a CLK interrupt"
#endif

static void dreamload_send_block(const uint8_t* p) {
  uint8_t checksum = 0;
  int     n;

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    // checksum is EOR of all bytes
    for (n = 0; n < 256; n++)
      checksum ^= p[n];

    // send status, data bytes and checksum
    dreamload_send_byte(0);
    for (n = 0; n < 256; n++) {
      dreamload_send_byte(*p);
      p++;
    }
    dreamload_send_byte(checksum);

    // release CLOCK and DATA
    set_clock(1);
    set_data(1);
  }
}

void load_dreamload(UNUSED_PARAMETER) {
  uint16_t n;
  uint8_t  type;
  buffer_t *buf;

  /* disable IRQs while loading the final code, so no jobcodes are read */
  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    set_clock_irq(0);
    set_atn_irq(0);

    /* Release clock and data */
    set_clock(1);
    set_data(1);

    /* wait until the C64 has released clock */
    while (!IEC_CLOCK) ;

    /* load final drive code, fixed length */
    type = 0;
    for (n = 4 * 256; n != 0; --n) {
      type ^= dreamload_get_byte();
    }

    if ((type == 0xac) || (type == 0xdc)) {
      set_atn_irq(1);
      detected_loader = FL_DREAMLOAD_OLD;
    } else {
      set_clock_irq(1);
    }

    /* mark no job waiting, enable IRQs to get job codes */
    fl_track = 0xff;
  }

  buf = alloc_system_buffer();
  if (!buf) {
    /* &@$% :-( */
    goto error;
  }

  /* Find the start sector of the current directory */
  dh_t dh;
  path_t curpath;

  curpath.part = current_part;
  curpath.dir  = partition[current_part].current_dir;
  opendir(&dh, &curpath);

  for (;;) {

    while (fl_track == 0xff) {
      if (check_keys()) {
        fl_track = 0;
        fl_sector = 0;
        break;
      }
    }

    set_busy_led(1);

    /* Output the track/sector for debugging purposes */
    uart_puthex(fl_track);
    uart_putc('/');
    uart_puthex(fl_sector);
    uart_putcrlf();

    if (fl_track == 0) {
      // check special commands first
      if (fl_sector == 0) {
        // end loader
        set_busy_led(0);
        break;
      } else if (fl_sector == 1) {
        // command: load first sector of directory
        // slow down 18/1 loading, so diskswap has a higher chance
        tick_t targettime = ticks + MS_TO_TICKS(1000);
        while (time_before(ticks,targettime)) ;

        read_sector(buf, current_part, dh.dir.d64.track, dh.dir.d64.sector);
        dreamload_send_block(buf->data);
      }
      else {
        // fl_sector == 2 is canonical
        set_busy_led(0);
      }
    } else {
      read_sector(buf, current_part, fl_track, fl_sector);
      dreamload_send_block(buf->data);
    }
    fl_track = 0xff;
  }

error:
  free_buffer(buf);
  set_clock_irq(0);
  set_atn_irq(0);
}
