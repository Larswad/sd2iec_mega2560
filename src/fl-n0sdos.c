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


   fl-n0sdos.c: High level handling of N0stalgia fastloaders

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "diskchange.h"
#include "doscmd.h"
#include "fastloader-ll.h"
#include "fileops.h"
#include "iec-bus.h"
#include "iec.h"
#include "timer.h"
#include "fastloader.h"


static int16_t getbyte(void) {
  uint8_t byte = 0;
  iec_bus_t bus;

  for (uint8_t i = 0; i < 8; i++) {
    /* wait for bit */
    do {
      check_keys();

      bus = iec_bus_read();

      /* immediately abort if ATN is low */
      if (!(bus & IEC_BIT_ATN))
        return -1;
    } while ((bus & (IEC_BIT_CLOCK | IEC_BIT_DATA)) ==
                    (IEC_BIT_CLOCK | IEC_BIT_DATA));

    byte >>= 1;
    if (!(bus & IEC_BIT_DATA))
      byte |= 0x80;

    /* acknowledge it */
    if (bus & IEC_BIT_DATA)
      set_data(0);
    else
      set_clock(0);
    delay_us(2);

    /* wait for C64's acknowledge */
    do {
      bus = iec_bus_read();

      if (!(bus & IEC_BIT_ATN))
        return -1;
    } while ((bus & (IEC_BIT_CLOCK | IEC_BIT_DATA)) == 0);

    /* release bus */
    set_clock(1);
    set_data(1);
    delay_us(2);
  }

  return byte;
}

void load_n0sdos_fileread(UNUSED_PARAMETER) {
  buffer_t *buf;

  set_clock(1);
  set_data(0);

  delay_ms(10); // FIXME: Probably not needed at all

  /* loader loop */
  while (IEC_ATN) {
    uint8_t *ptr = command_buffer;
    uint8_t i;

    /* receive a file name */
    set_clock(1);
    set_data(1);
    delay_us(2);

    memset(command_buffer, 0, sizeof(command_buffer));
    for (i = 0; i < 7; i++) {
      int16_t val = getbyte();

      /* abort if ATN is active */
      if (val < 0)
        return;

      if (val == 0)
        break;

      *ptr++ = val;
    }

    command_length = i;

    /* allow partial file name matches when length is at maximum */
    if (i == 7) {
      *ptr = '*';
      command_length++;
    }

    set_clock(0);
    set_data(0);

    /* try to open the file */
    file_open(0);
    buf = find_buffer(0);
    if (!buf) {
      /* failed */
      n0sdos_send_byte(0xff);
      continue;
    }

    /* transfer contents */
    n0sdos_send_byte(0x00);

    while (1) {
      uint8_t *ptr = buf->data + buf->position;

      for (uint8_t i = 0; i < 254; i++) {
        /* abort on CLOCK high */
        if (IEC_CLOCK || !IEC_ATN)
          goto leave;

        n0sdos_send_byte(*ptr++);
      }

      if (!buf->sendeoi) {
        /* read next sector if available */
        buf->refill(buf);
      }
    }

  leave:
    cleanup_and_free_buffer(buf);
  }
}
