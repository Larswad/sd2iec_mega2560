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


   fl-dolphin.c: High level handling of Dolphin parallel protocols

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "timer.h"
#include "uart.h"
#include "fastloader.h"

/**
 * _dolphin_getc - receive one byte from the parallel port (DD A816)
 *
 * This function tries to receive one byte via parallel and returns
 * it if successful. Returns -1 instead if the device state has
 * changed.
 */
static int16_t _dolphin_getc(void) {
  /* wait until CLOCK is high */
  while (!IEC_CLOCK)   // A818
    if (iec_check_atn())
      return -1;

  set_data(1);         // A81F

  /* start EOI timeout */
  start_timeout(86);

  /* wait until CLOCK is low or 86 microseconds passed */
  uint8_t tmp;
  do {
    if (iec_check_atn())
      return -1;
    tmp = has_timed_out();
  } while (!tmp && IEC_CLOCK);

  if (tmp) { // A835
    /* acknowledge EOI */
    set_data(0);
    delay_us(57);
    set_data(1);

    uart_putc('E');
    iec_data.iecflags |= EOI_RECVD;

    /* wait until CLOCK is low - A849 */
    while (IEC_CLOCK)
      if (iec_check_atn())
        return -1;
  }

  /* read byte */
  uint8_t b = parallel_read();
  parallel_send_handshake();

  set_data(0);
  return b;
}

/* disable interrupts in _dolphin_getc */
int16_t dolphin_getc(void) {
  int16_t val;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    val = _dolphin_getc();
  }
  return val;
}

/**
 * dolphin_putc - send a byte over parallel for DolphinDOS (A866)
 * @data    : byte to be sent
 * @with_eoi: Flags if the byte should be send with an EOI condition
 *
 * This is the DolphinDOS parallel byte transfer function.
 * Returns 0 normally or -1 if ATN is low.
 */
/* DolphinDOS parallel byte transfer - A866 */
uint8_t dolphin_putc(uint8_t data, uint8_t with_eoi) {
  set_clock(1);

  /* wait until DATA is high */
  while (!IEC_DATA)   // A870
    if (iec_check_atn())
      return -1;

  if (with_eoi) {
    /* signal EOI by waiting for a pulse on DATA */
    while (IEC_DATA)  // A87C
      if (iec_check_atn())
        return -1;

    while (!IEC_DATA) // A883
      if (iec_check_atn())
        return -1;
  }

  /* send byte - A88A */
  parallel_write(data);
  parallel_send_handshake();
  set_clock(0);

  /* wait until DATA is low */
  while (IEC_DATA)    // A89A
    if (iec_check_atn())
      return -1;

  return 0;
}

/* send a byte with hardware handshaking */
static void dolphin_write_hs(uint8_t value) {
  parallel_write(value);
  parallel_clear_rxflag();
  parallel_send_handshake();
  while (!parallel_rxflag) ;
}

/* DolphinDOS XQ command */
void load_dolphin(void) {
  /* find the already open buffer */
  buffer_t *buf = find_buffer(0);

  if (!buf)
    return;

  buf->position = 2;

  /* initial handshaking */
  // note about the delays: 100us work, not optimized
  // (doesn't matter much outside the loop)
  delay_us(100); // experimental delay
  parallel_set_dir(PARALLEL_DIR_OUT);
  set_clock(0);
  parallel_clear_rxflag();
  delay_us(100); // experimental delay
  parallel_send_handshake();
  uart_flush();
  delay_us(100); // experimental delay

  /* every sector except the last */
  uint8_t i;

  while (!buf->sendeoi) {
    iec_bus_t bus_state = iec_bus_read();

    /* transmit first byte */
    dolphin_write_hs(buf->data[2]);

    /* check DATA state before transmission */
    if (bus_state & IEC_BIT_DATA) {
      cleanup_and_free_buffer(buf);
      return;
    }

    /* transmit the rest of the sector */
    for (i = 3; i != 0; i++)
      dolphin_write_hs(buf->data[i]);

    /* read next sector */
    if (buf->refill(buf)) {
      cleanup_and_free_buffer(buf);
      return;
    }
  }

  /* last sector */
  i = 2;
  do {
    dolphin_write_hs(buf->data[i]);
  } while (i++ < buf->lastused);

  /* final handshake */
  set_clock(1);
  while (!IEC_DATA) ;
  parallel_send_handshake();
  parallel_set_dir(PARALLEL_DIR_IN);

  cleanup_and_free_buffer(buf);
}

/* DolphinDOS XZ command */
void save_dolphin(void) {
  buffer_t *buf;
  uint8_t eoi;

  /* find the already open file */
  buf = find_buffer(1);

  if (!buf)
    return;

  /* reset buffer position */
  buf->position = 2;
  buf->lastused = 2;

  /* experimental delay to avoid hangs */
  delay_us(100);

  /* handshaking */
  parallel_set_dir(PARALLEL_DIR_IN);
  set_data(0);
  parallel_clear_rxflag();
  parallel_send_handshake();
  uart_flush();

  /* receive data */
  do {
    /* flush buffer if full */
    if (buf->mustflush)
      if (buf->refill(buf))
        return; // FIXME: check error handling in Dolphin

    while (!parallel_rxflag) ;

    buf->data[buf->position] = parallel_read();
    mark_buffer_dirty(buf);

    if (buf->lastused < buf->position)
      buf->lastused = buf->position;
    buf->position++;

    /* mark for flushing on wrap */
    if (buf->position == 0)
      buf->mustflush = 1;

    eoi = !!IEC_CLOCK;

    parallel_clear_rxflag();
    parallel_send_handshake();
  } while (!eoi);

  /* the file will be closed with ATN+0xe1 by DolphinDOS */
}
