/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

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


   softi2c.c: Software I2C bus master

*/

#include <avr/io.h>
#include <util/delay.h>
#include "config.h"
#include "i2c.h"

#define SOFTI2C_SDA _BV(SOFTI2C_BIT_SDA)
#define SOFTI2C_SCL _BV(SOFTI2C_BIT_SCL)

static void set_scl(uint8_t x) {
  if (x) {
    SOFTI2C_DDR  &= (uint8_t)~SOFTI2C_SCL;
    SOFTI2C_PORT |= SOFTI2C_SCL;
    // Clock stretching
    loop_until_bit_is_set(SOFTI2C_PIN, SOFTI2C_BIT_SCL);
  } else {
    SOFTI2C_DDR  |= SOFTI2C_SCL;
    SOFTI2C_PORT &= (uint8_t)~SOFTI2C_SCL;
  }
}

static void set_sda(uint8_t x) {
  if (x) {
    SOFTI2C_DDR  &= (uint8_t)~SOFTI2C_SDA;
    SOFTI2C_PORT |= SOFTI2C_SDA;
  } else {
    SOFTI2C_DDR  |= SOFTI2C_SDA;
    SOFTI2C_PORT &= (uint8_t)~SOFTI2C_SDA;
  }
}


static void start_condition(void) {
  set_sda(1);
  set_scl(1);
  _delay_us(SOFTI2C_DELAY);
  set_sda(0);
  _delay_us(SOFTI2C_DELAY);
  set_scl(0);
}

static void stop_condition(void) {
  set_sda(0);
  _delay_us(SOFTI2C_DELAY);
  set_scl(1);
  _delay_us(SOFTI2C_DELAY);
  set_sda(1);
  _delay_us(SOFTI2C_DELAY);
}

/* Returns value of the acknowledge bit */
static uint8_t i2c_send_byte(uint8_t value) {
  uint8_t i;

  for (i=8;i!=0;i--) {
    set_scl(0);
    _delay_us(SOFTI2C_DELAY/2);
    set_sda(value & 128);
    _delay_us(SOFTI2C_DELAY/2);
    set_scl(1);
    _delay_us(SOFTI2C_DELAY);
    value <<= 1;
  }
  set_scl(0);
  _delay_us(SOFTI2C_DELAY/2);
  set_sda(1);
  _delay_us(SOFTI2C_DELAY/2);
  set_scl(1);
  _delay_us(SOFTI2C_DELAY/2);
  i = !!(SOFTI2C_PIN & SOFTI2C_SDA);
  _delay_us(SOFTI2C_DELAY/2);
  set_scl(0);

  return i;
}

/* Returns value of the byte read */
static uint8_t i2c_recv_byte(uint8_t sendack) {
  uint8_t i;
  uint8_t value = 0;

  set_sda(1);
  _delay_us(SOFTI2C_DELAY/2);
  for (i=8;i!=0;i--) {
    _delay_us(SOFTI2C_DELAY/2);
    set_scl(1);
    _delay_us(SOFTI2C_DELAY/2);
    value = (value << 1) + !!(SOFTI2C_PIN & SOFTI2C_SDA);
    _delay_us(SOFTI2C_DELAY/2);
    set_scl(0);
    _delay_us(SOFTI2C_DELAY/2);
  }
  set_sda(!sendack);
  _delay_us(SOFTI2C_DELAY/2);
  set_scl(1);
  _delay_us(SOFTI2C_DELAY);
  set_scl(0);
  set_sda(1);

  return value;
}

/* Returns 1 if there was no ACK to the address */
uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) {
  start_condition();
  if (i2c_send_byte(address)) {
    stop_condition();
    return 1;
  }
  i2c_send_byte(reg);
  i2c_send_byte(val);
  stop_condition();
  return 0;
}

/* Returns 1 if there was no ACK to the address */
uint8_t i2c_write_registers(uint8_t address, uint8_t startreg, uint8_t count, const void *data) {
  uint8_t i;

  start_condition();
  if (i2c_send_byte(address)) {
    stop_condition();
    return 1;
  }
  i2c_send_byte(startreg);
  for (i=0;i<count;i++)
    i2c_send_byte(((uint8_t *)data)[i]);
  stop_condition();
  return 0;
}

/* Returns -1 if there was no ACK to the address, register contents otherwise */
int16_t i2c_read_register(uint8_t address, uint8_t reg) {
  uint8_t val;

  start_condition();
  if (i2c_send_byte(address)) {
    stop_condition();
    return -1;
  }
  i2c_send_byte(reg);
  start_condition();
  if (i2c_send_byte(address|1)) {
    stop_condition();
    return -1;
  }
  val = i2c_recv_byte(0);
  stop_condition();
  return val;
}

/* Returns 1 if there was no ACK to the address */
uint8_t i2c_read_registers(uint8_t address, uint8_t startreg, uint8_t count, void *data) {
  uint8_t i;

  start_condition();
  if (i2c_send_byte(address)) {
    stop_condition();
    return 1;
  }
  i2c_send_byte(startreg);
  start_condition();
  if (i2c_send_byte(address|1)) {
    stop_condition();
    return 1;
  }
  for (i=0;i<count-1;i++)
    ((uint8_t *)data)[i] = i2c_recv_byte(1);
  ((uint8_t *)data)[i] = i2c_recv_byte(0);
  stop_condition();
  return 0;
}

void i2c_init(void) {
  /* Set I2C pins to input -> high with external pullups */
  SOFTI2C_DDR  &= (uint8_t)~(SOFTI2C_SCL|SOFTI2C_SDA);
  SOFTI2C_PORT &= (uint8_t)~(SOFTI2C_SCL|SOFTI2C_SDA);

  set_sda(1);
  set_scl(1);
}
