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


   fl-fc3exos.c: High level handling of Final Cartridge 3/EXOS

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


void load_fc3(uint8_t freezed) {
  buffer_t *buf;
  unsigned char step,pos;
  unsigned char sector_counter = 0;
  unsigned char block[4];

  buf = find_buffer(0);

  if (!buf) {
    /* error, abort and pull down CLOCK and DATA to inform the host */
    set_data(0);
    set_clock(0);
    return;
  }

  /* to make sure the C64 VIC DMA is off */
  delay_ms(20);

  for(;;) {
    clk_data_handshake();

    /* Starting buffer position */
    /* Rewinds by 2 bytes for the first sector and normal loader */
    pos = 2;

    /* construct first 4-byte block */
    /* The 0x07 in the first byte is never used */
    block[1] = sector_counter++;
    if (buf->sendeoi) {
      /* Last sector, send number of bytes */
      block[2] = buf->lastused;
    } else {
      /* Send 0 for full sector */
      block[2] = 0;
    }
    /* First data byte */
    block[3] = buf->data[pos++];

    if (!freezed)
      delay_us(190);
    fastloader_fc3_send_block(block);

    /* send the next 64 4-byte-blocks, the last 3 bytes are read behind
       the buffer, good that we don't have an MMU ;) */
    for (step = 0; step < 64; step++) {
      if (!IEC_ATN)
        goto cleanup;

      if (freezed)
        clk_data_handshake();
      else
        delay_us(190);
      fastloader_fc3_send_block(buf->data + pos);
      pos += 4;
    }

    if (buf->sendeoi) {
      /* pull down DATA to inform the host about the last sector */
      set_data(0);
      break;
    } else {
      if (buf->refill(buf)) {
        /* error, abort and pull down CLOCK and DATA to inform the host */
        set_data(0);
        set_clock(0);
        break;
      }
    }
  }

 cleanup:
  cleanup_and_free_buffer(buf);
}

void save_fc3(UNUSED_PARAMETER) {
  unsigned char n;
  unsigned char size;
  unsigned char eof = 0;
  buffer_t *buf;

  buf = find_buffer(1);
  /* Check if this is a writable file */
  if (!buf || !buf->write)
      return;

  /* to make sure the host pulled DATA low and is ready */
  delay_ms(5);

  do {
    set_data(0);

    size = fc3_get_byte();

    if (size == 0) {
      /* a full block is coming, no EOF */
      size = 254;
    }
    else {
      /* this will be the last block */
      size--;
      eof = 1;
    }

    for (n = 0; n < size; n++) {
      /* Flush buffer if full */
      if (buf->mustflush) {
        buf->refill(buf);
        /* the FC3 just ignores things like "disk full", so do we */
      }

      buf->data[buf->position] = fc3_get_byte();

      if (buf->lastused < buf->position)
        buf->lastused = buf->position;
      buf->position++;

      /* Mark buffer for flushing if position wrapped */
      if (buf->position == 0)
        buf->mustflush = 1;
    }
  }
  while (!eof);

  cleanup_and_free_buffer(buf);
}

void load_fc3oldfreeze(UNUSED_PARAMETER) {
  buffer_t *buf;
  uint16_t i;

  /* mark as busy */
  set_srq(0);
  set_clock(1);
  set_data(0);

  /* wait until C64 has finished UNLISTEN */
  delay_us(1);
  start_timeout(100);
  while (!IEC_CLOCK && !has_timed_out()) ;

  buf = find_buffer(0);

  if (!buf) {
    /* error */
    return;
  }

  /* sector loop */
  while (1) {
    /* send sector data */
    for (i = 2; i <= buf->lastused; i++) {
      if (fast_send_byte(buf->data[i])) {
        /* ATN active, abort */
        goto done;
      }
    }

    /* check for end of file */
    if (buf->sendeoi)
      break;

    /* read next sector */
    if (buf->refill(buf)) {
      /* error */
      break;
    }
  }

 done:
  cleanup_and_free_buffer(buf);
}
