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


   fl-nippon.c: High level handling of Nippon fastloader

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "wrapops.h"
#include "fastloader.h"


/* protocol sync: wait for Clock low, if ATN is also low, then return signal to resync */
static uint8_t nippon_atn_clock_handshake(void) {
  if (IEC_ATN) {
    while(IEC_CLOCK) ;
    return 1;
  } else {
    set_clock(0);
    while(IEC_ATN) ;
    return 0;
  }
}


/* reads a byte from IEC, if loader is out of sync return false */
static uint8_t nippon_read_byte(uint8_t *b) {
  uint8_t tmp = 0, i;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    set_clock(1);
    set_data(1);
    delay_us(3); // allow for slow rise times
    for (i=8; i; i--) {
      if (! nippon_atn_clock_handshake())
        return 0;
      tmp = (tmp >> 1) | (IEC_DATA ? 0 : 128);
      while(!IEC_CLOCK) ;
    }
    set_clock(0);
    set_data(1);
    *b = tmp;
    return 1;
  }
  return 1; // not reached
}

/* sends a byte via IEC, if loader is out of sync return false */
static uint8_t nippon_send_byte(uint8_t b) {
  uint8_t i;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    set_clock(1);
    delay_us(3); // allow for slow rise times
    for (i=8; i; i--) {
      if (! nippon_atn_clock_handshake())
        return 0;
      set_data(b & 1);
      b = b >> 1;
      while(!IEC_CLOCK) ;
    }
    set_clock(0);
    set_data(1);
    return 1;
  }
  return 1; // not reached
}

/* nippon idle loop */
void load_nippon(UNUSED_PARAMETER) {
  uint8_t t, s, i=0, j;
  buffer_t *buf;

  /* init */
  uart_puts_P(PSTR("NIPPON"));
  set_atn_irq(0);
  buf = alloc_system_buffer();
  if (!buf) {
    uart_puts_P(PSTR("BUF ERR")); uart_putcrlf();
    return;
  }

  /* loop until IEC master sends a track greater than $80 = exit code */
  while (1) {

    /* initial state */
    /* timing is critical for ATN/CLK here, endless loop in $0bf0 at the cevi
     * raise ATN CLK quick, before setting LEDs output to serial console
     */
    set_data(1);
    set_clock(1);
    set_busy_led(0);
    uart_putcrlf(); uart_putc('L'); // idle loop entered

    /* wait for IEC master or human master to command us something */
    while(IEC_ATN && !(i = check_keys())) ;
    if (i)
      break; /* user requested exit */
    set_clock(0);
    set_busy_led(1);

    /* fetch command, sector and track, on failure resync/reinit protocol
     * if track > 128 exit loader
     * if sector > 128 read sector, else write */
    while(!IEC_ATN) ;
    if (! nippon_read_byte(&t))
      continue;
    uart_putc('T');
    uart_puthex(t);
    if (t & 128)
      break;

    if (! nippon_read_byte(&s))
      continue;
    uart_putc('S');
    uart_puthex(s & 0x7f);

    if (s & 128) {
      /* read sector */
      s &= 0x7f;
      uart_putc('R');
      read_sector(buf, current_part, t, s);
      i = 0;
      do {
        if (! nippon_send_byte(buf->data[i]))
          break;
        i++;
      } while (i);
    }
    else {
      /* write sector */
      uart_putc('W');
      i = 0;
      do {
        if (! (j = nippon_read_byte(&(buf->data[i]))))
          break;
        i++;
      } while (i);
      if (!j)
        break;
      write_sector(buf, current_part, t, s);
    }
  }

  /* exit */

  free_buffer(buf);
  uart_puts_P(PSTR("NEXT")); uart_putcrlf();
}
