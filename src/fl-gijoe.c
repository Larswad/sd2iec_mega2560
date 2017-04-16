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


   fl-gijoe.c: High level handling of GI Joe fastloader

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "doscmd.h"
#include "fastloader-ll.h"
#include "fileops.h"
#include "iec-bus.h"
#include "iec.h"
#include "timer.h"
#include "uart.h"
#include "fastloader.h"


static void gijoe_send_byte(uint8_t value) {
  uint8_t i;

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    for (i=0;i<4;i++) {
      /* Wait for clock high */
      while (!IEC_CLOCK) ;

      set_data(value & 1);
      value >>= 1;

      /* Wait for clock low */
      while (IEC_CLOCK) ;

      set_data(value & 1);
      value >>= 1;
    }
  }
}

void load_gijoe(UNUSED_PARAMETER) {
  buffer_t *buf;

  set_data(1);
  set_clock(1);
  set_atn_irq(0);

  /* Wait until the bus has settled */
  delay_ms(10);
  while (!IEC_DATA || !IEC_CLOCK) ;

  while (1) {
    /* Handshake */
    set_clock(0);

    while (IEC_DATA)
      if (check_keys())
        return;

    set_clock(1);
    uart_flush();

    /* First byte is ignored */
    if (gijoe_read_byte() < 0)
      return;

    /* Read two file name characters */
    command_buffer[0] = gijoe_read_byte();
    command_buffer[1] = gijoe_read_byte();

    set_clock(0);

    command_buffer[2] = '*';
    command_buffer[3] = 0;
    command_length = 3;

    /* Open the file */
    file_open(0);
    uart_flush();
    buf = find_buffer(0);
    if (!buf) {
      set_clock(1);
      gijoe_send_byte(0xfe);
      gijoe_send_byte(0xfe);
      gijoe_send_byte(0xac);
      gijoe_send_byte(0xf7);
      continue;
    }

    /* file is open, transfer */
    while (1) {
      uint8_t i = buf->position;

      set_clock(1);
      delay_us(2);

      do {
        if (buf->data[i] == 0xac)
          gijoe_send_byte(0xac);

        gijoe_send_byte(buf->data[i]);
      } while (i++ < buf->lastused);

      /* Send end marker and wait for the next name */
      if (buf->sendeoi) {
        gijoe_send_byte(0xac);
        gijoe_send_byte(0xff);

        cleanup_and_free_buffer(buf);
        break;
      }

      /* Send "another sector following" marker */
      gijoe_send_byte(0xac);
      gijoe_send_byte(0xc3);
      delay_us(50);
      set_clock(0);

      /* Read next block */
      if (buf->refill(buf)) {
        /* Send error marker */
        gijoe_send_byte(0xfe);
        gijoe_send_byte(0xfe);
        gijoe_send_byte(0xac);
        gijoe_send_byte(0xf7);

        cleanup_and_free_buffer(buf);
        break;
      }
    }
  }
}
