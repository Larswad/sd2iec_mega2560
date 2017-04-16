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


   fl-mmzak.c: High level handling of MM/Zak fastloaders

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "errormsg.h"
#include "fastloader-ll.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "wrapops.h"
#include "fastloader.h"

/**
 * mmzak_send_byte - send one byte
 * @byte: value to transmit
 *
 * This function sends one byte with the MM/Zak protocol.
 * Returns false if successful, true if user aborted.
 */
static bool mmzak_send_byte(uint8_t byte) {
  for (uint8_t i = 0; i < 4; i++) {
    /* wait until ready */
    while (!IEC_CLOCK)
      if (check_keys())
        return true;

    /* send top bit */
    if (byte & 0x80)
      set_data(1);
    else
      set_data(0);

    byte <<= 1;

    /* wait until ready */
    while (IEC_CLOCK)
      if (check_keys())
        return true;

    /* send top bit */
    if (byte & 0x80)
      set_data(1);
    else
      set_data(0);

    byte <<= 1;
  }

  return false;
}

/**
 * mmzak_read_byte - receive one byte
 *
 * This function receives one byte with the MM/Zak protocol and
 * returns it if successful. Returns -1 instead if user aborted.
 */
static int16_t mmzak_read_byte(void) {
  uint8_t value = 0;

  for (uint8_t i = 0; i < 4; i++) {
    /* wait until clock is low */
    while (IEC_CLOCK)
      if (check_keys())
        return -1;

    value <<= 1;

    delay_us(3);
    if (!IEC_DATA)
      value |= 1;

    /* wait until clock is high */
    while (!IEC_CLOCK)
      if (check_keys())
        return -1;

    value <<= 1;

    delay_us(3);
    if (!IEC_DATA)
      value |= 1;
  }

  return value;
}

/**
 * mmzak_send_error - signal an error to the C64
 *
 * This function sends an error result to the C64.
 * Returns false if successful, true on user abort.
 */
static bool mmzak_send_error(void) {
  set_clock(1);
  set_data(1);
  if (mmzak_send_byte(0x01))
    return true;

  if (mmzak_send_byte(0x11))
    return true;

  return false;
}

/**
 * mmzak_read_sector - read and transmit a sector
 * @track : track number
 * @sector: sector number
 * @buf   : buffer to use
 *
 * This function reads the given sector and transmits it to the C64.
 * Returns false if successful, true on user abort.
 */
static bool mmzak_read_sector(uint8_t track, uint8_t sector, buffer_t *buf) {
  read_sector(buf, current_part, track, sector);
  if (current_error != 0) {
    /* read failed, send error */
    return mmzak_send_error();
  }

  set_clock(1);
  set_data(1);
  delay_us(3);

  /* send contents */
  uint8_t *ptr = buf->data;

  for (uint16_t i = 0; i < 256; i++) {
    if (*ptr == 0x01)
      if (mmzak_send_byte(0x01))
        return true;

    if (mmzak_send_byte(*ptr++))
      return true;
  }

  /* send status */
  if (mmzak_send_byte(0x01))
    return true;

  if (mmzak_send_byte(0x81))
    return true;

  set_clock(0);
  set_data(1);
    
  return false;
}

/**
 * mmzak_write_sector - receive and write a sector
 * @track : track number
 * @sector: sector number
 * @buf   : buffer to use
 *
 * This function receives a sector from the C64 and writes it to disk.
 * Returns false if successful, true on user abort.
 */
static bool mmzak_write_sector(uint8_t track, uint8_t sector, buffer_t *buf) {
  set_clock(1);
  set_data(1);
  delay_us(3);

  /* receive data */
  uint8_t *ptr = buf->data;
  mark_buffer_dirty(buf);

  for (uint16_t i = 0; i < 256; i++) {
    int16_t v = mmzak_read_byte();

    if (v < 0)
      return true;

    *ptr++ = v;
  }

  set_clock(0);

  /* write data */
  write_sector(buf, current_part, track, sector);
  mark_buffer_clean(buf);

  if (current_error != 0) {
    return mmzak_send_error();
  }

  return false;
}

void load_mmzak(UNUSED_PARAMETER) {
  buffer_t *buf = alloc_system_buffer();

  if (buf == NULL)
    return;

  set_atn_irq(0);

  /* initial handshake */
  /* FIXME: Not sure if needed, actually uses the ATN-ACK HW on a 1541 */
  for (uint8_t i = 0; i < 8; i++) {
    set_clock(1);
    set_data(0);
    delay_us(1285);
    set_data(1);
    delay_us(1290);
  }

  /* wait until bus is released */
  while ((iec_bus_read() & (IEC_BIT_CLOCK | IEC_BIT_DATA)) !=
         (IEC_BIT_CLOCK | IEC_BIT_DATA)) {
    if (check_keys())
      return;
  }

  /* command loop */
  bool done = false;

  while (!done) {
    int16_t v;
    uint8_t track, sector, command;

    /* signal our readyness */
    set_clock(0);
    set_data(1);
    set_busy_led(0);
    delay_us(3);

    /* wait for C64 */
    while (IEC_DATA) { // wait until data is low
      if (check_keys()) {
        done = true;
        break;
      }
    }

    if (done)
      break;

    set_clock(1);

    /* receive command block */
    v = mmzak_read_byte();
    if (v < 0)
      break;
    track = v;

    v = mmzak_read_byte();
    if (v < 0)
      break;
    sector = v;

    v = mmzak_read_byte();
    if (v < 0)
      break;
    command = v;

    set_busy_led(1);
    set_clock(0);

    /* check command */
    switch (command) {
    case 0x20:
      /* exit loader */
      done = true;
      break;

    case 0x30:
      /* read sector */
      if (mmzak_read_sector(track, sector, buf))
        done = true;
      break;

    case 0x40:
      /* write sector */
      if (mmzak_write_sector(track, sector, buf))
        done = true;
      break;

    default:
      /* unknown, signal error */
      if (mmzak_send_error())
        done = true;
      break;
    }
  }

  set_busy_led(0);
}
