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


   fl-ulm3.c: High level handling of ULoad Model 3

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "errormsg.h"
#include "fastloader-ll.h"
#include "fileops.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "wrapops.h"
#include "fastloader.h"


static uint8_t uload3_transferchain(uint8_t track, uint8_t sector, uint8_t saving) {
  buffer_t *buf;
  uint8_t i,bytecount,first;

  first = 1;

  buf = alloc_buffer();
  if (!buf) {
    uload3_send_byte(0xff);
    return 0;
  }

  do {
    /* read current sector */
    read_sector(buf, current_part, track, sector);
    if (current_error != 0) {
      uload3_send_byte(0xff);
      return 0;
    }

    /* send number of bytes in sector */
    if (buf->data[0] == 0) {
      bytecount = buf->data[1]-1;
    } else {
      bytecount = 254;
    }
    uload3_send_byte(bytecount);

    if (saving) {
      if (first) {
        /* send load address */
        first = 0;
        uload3_send_byte(buf->data[2]);
        uload3_send_byte(buf->data[3]);
        i = 2;
      } else
        i = 0;

      /* receive sector contents */
      for (;i<bytecount;i++) {
        int16_t tmp = uload3_get_byte();
        if (tmp < 0)
          return 1;

        buf->data[i+2] = tmp;
      }

      /* write sector */
      write_sector(buf, current_part, track, sector);
      if (current_error != 0) {
        uload3_send_byte(0xff);
        return 0;
      }
    } else {
      /* reading: send sector contents */
      for (i=0;i<bytecount;i++)
        uload3_send_byte(buf->data[i+2]);
    }

    track  = buf->data[0];
    sector = buf->data[1];
  } while (track != 0);

  /* send end marker */
  uload3_send_byte(0);

  free_buffer(buf);
  return 0;
}

void load_uload3(UNUSED_PARAMETER) {
  int16_t cmd,tmp;
  uint8_t t,s;
  dh_t dh;
  path_t curpath;

  curpath.part = current_part;
  curpath.dir  = partition[current_part].current_dir;
  opendir(&dh, &curpath);

  while (1) {
    /* read command */
    cmd = uload3_get_byte();
    if (cmd < 0) {
      /* ATN received */
      break;
    }

    switch (cmd) {
    case 1: /* load a file */
    case 2: /* save and replace a file */
      tmp = uload3_get_byte();
      if (tmp < 0)
        return;
      t = tmp;

      tmp = uload3_get_byte();
      if (tmp < 0)
        /* ATN received */
        return;
      s = tmp;

      if (uload3_transferchain(t,s, (cmd == 2)))
        return;

      break;

    case '$':
      /* read directory */
      uload3_transferchain(dh.dir.d64.track, dh.dir.d64.sector, 0);
      break;

    default:
      /* unknown command */
      uload3_send_byte(0xff);
      break;
    }
  }
}
