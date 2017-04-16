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


   fl-eload.c: High level handling of eload

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "fastloader.h"



/*
 *
 * eload
 *
 */
void load_eload1(UNUSED_PARAMETER) {
  buffer_t *buf;
  int16_t cmd;
  uint8_t count, pos, end;

  while (1) {
    /* read command */
    cmd = uload3_get_byte();
    if (cmd < 0) {
      /* ATN received */
      break;
    }

    switch (cmd) {
    case 1: /* load a file */
      buf = find_buffer(0);

      if (!buf) {
        if (!IEC_ATN)
          return;
        uload3_send_byte(0xff); /* error */
        return;
      }

      end = 0;
      do {
        count = buf->lastused - 1;
        if (!IEC_ATN)
          return;
        uload3_send_byte(count);
        pos = 2;
        while (count--) {
          if (!IEC_ATN)
            return;
          uload3_send_byte(buf->data[pos++]);
        }
        if (buf->sendeoi) {
          uload3_send_byte(0); /* EOF */
          end = 1;
        }
        else if (buf->refill(buf)) {
          uload3_send_byte(0xff); /* error */
          end = 1;
        }
      } while (!end);
      break;

    default:
      /* unknown command */
      uload3_send_byte(0xff);
      break;
    }
  }
}
