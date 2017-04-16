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


   fl-epyxcart.c: High level handling of Epyx Fastload Cart

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
#include "uart.h"
#include "fastloader.h"



void load_epyxcart(UNUSED_PARAMETER) {
  uint8_t checksum = 0;
  int16_t b,i;

  uart_flush(); // Pending output can mess up our timing

  /* Initial handshake */
  set_data(1);
  set_clock(0);
  set_atn_irq(0);

  while (IEC_DATA)
    if (!IEC_ATN)
      return;

  set_clock(1);

  /* Receive and checksum stage 2 */
  for (i=0;i<256;i++) {
    b = gijoe_read_byte();

    if (b < 0)
      return;

    if (i < 237)
      /* Stage 2 has some junk bytes at the end, ignore them */
      checksum ^= b;
  }

  /* Check for known stage2 loaders */
  if (checksum != 0x91 && checksum != 0x5b) {
    return;
  }

  /* Receive file name */
  i = gijoe_read_byte();
  if (i < 0) {
    return;
  }

  command_length = i;

  do {
    b = gijoe_read_byte();
    if (b < 0)
      return;

    command_buffer[--i] = b;
  } while (i > 0);

  set_clock(0);

  /* Open the file */
  file_open(0);

  buffer_t *buf = find_buffer(0);
  if (buf == NULL) {
    set_clock(1);
    return;
  }

  /* Transfer data */
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    while (1) {
      set_clock(1);
      set_data(1);

      /* send number of bytes in sector */
      if (epyxcart_send_byte(buf->lastused-1)) {
        break;
      }

      /* send data */
      for (i=2;i<=buf->lastused;i++) {
        if (epyxcart_send_byte(buf->data[i])) {
          break;
        }
      }

      if (!IEC_ATN)
        break;

      /* exit after final sector */
      if (buf->sendeoi)
        break;

      /* read next sector */
      set_clock(0);
      if (buf->refill(buf))
        break;
    }
  }

  set_clock(1);
  set_data(1);
  cleanup_and_free_buffer(buf);
}
