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


   fl-ar6.c: High level handling of AR6 fastloader/-saver

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "timer.h"
#include "fastloader.h"


/* 1581 loader */
void load_ar6_1581(UNUSED_PARAMETER) {
  buffer_t *buf;
  uint16_t i;

  buf = find_buffer(0);
  if (!buf) {
    /* The file should've been open? */
    return;
  }

  set_clock(0);
  set_data(1);
  delay_ms(1);

  while (1) {
    /* Send number of bytes in sector */
    ar6_1581_send_byte(buf->lastused-1);

    /* Send bytes in sector */
    for (i=2; i<=buf->lastused; i++)
      ar6_1581_send_byte(buf->data[i]);

    /* Check for end of file */
    if (buf->sendeoi)
      break;

    /* Read next sector */
    if (buf->refill(buf)) {
      /* Error, end transmission */
      break;
    }
  }

  /* Send end marker */
  ar6_1581_send_byte(0);
  delay_ms(1);
  set_clock(1);
  set_data(1);
}

/* 1581 saver */
void save_ar6_1581(UNUSED_PARAMETER) {
  buffer_t *buf;
  uint8_t i;

  buf = find_buffer(1);

  if (!buf) {
    /* File isn't open */
    return;
  }

  set_clock(0);
  set_data(1);
  delay_ms(1);

  do {
    mark_buffer_dirty(buf);

    /* Receive sector */
    i = 0;
    do {
      buf->data[i] = ar6_1581p_get_byte();
      i++;
    } while (i != 0);

    /* Set end */
    // FIXME: Although this works, it is not exactly clean:
    //        The code here should update lastused and mustflush.
    if (buf->data[0] == 0) {
      buf->position = buf->data[1];
    } else
      buf->position = 0;

    /* Write data */
    if (buf->refill(buf))
      break;
  } while (buf->data[0] != 0);

  cleanup_and_free_buffer(buf);
}
