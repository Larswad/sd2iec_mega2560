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


   ds1307-3231.c: RTC support for DS1307/DS3231 chips

   The exported functions in this file are weak-aliased to their corresponding
   versions defined in rtc.h so when this file is the only RTC implementation
   compiled in they will be automatically used by the linker.

*/

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "i2c.h"
#include "progmem.h"
#include "uart.h"
#include "ustring.h"
#include "utils.h"
#include "time.h"
#include "rtc.h"
#include "ds1307-3231.h"

#define RTC_ADDR 0xd0

#define REG_SECOND      0
#define REG_MINUTE      1
#define REG_HOUR        2
#define REG_DOW         3
#define REG_DOM         4
#define REG_MONTH       5
#define REG_YEAR        6

/* DS3231 registers */
#define REG_AL1_SECOND  7
#define REG_AL1_MINUTE  8
#define REG_AL1_HOUR    9
#define REG_AL1_DAY    10
#define REG_AL2_MINUTE 11
#define REG_AL2_HOUR   12
#define REG_AL2_DAY    13
#define REG_CONTROL_31 14
#define REG_CTLSTATUS  15
#define REG_AGING      16
#define REG_TEMP_MSB   17
#define REG_TEMP_LSB   18

/* DS1307 registers */
#define REG_CONTROL_07  7

#define STATUS_OSF     0x80  // oscillator stopped (1307: CH bit in reg 0)

static enum {
  RTC_1307, RTC_3231
} dsrtc_type;

/* Read the current time from the RTC */
void dsrtc_read(struct tm *time) {
  uint8_t tmp[7];

  /* Set to default value in case we abort */
  memcpy_P(time, &rtc_default_date, sizeof(struct tm));
  if (rtc_state != RTC_OK)
    return;

  if (i2c_read_registers(RTC_ADDR, REG_SECOND, 7, &tmp))
    return;

  time->tm_sec  = bcd2int(tmp[REG_SECOND] & 0x7f);
  time->tm_min  = bcd2int(tmp[REG_MINUTE]);
  time->tm_hour = bcd2int(tmp[REG_HOUR]);
  time->tm_mday = bcd2int(tmp[REG_DOM]);
  time->tm_mon  = bcd2int(tmp[REG_MONTH] & 0x7f) - 1;
  time->tm_wday = bcd2int(tmp[REG_DOW]) - 1;
  time->tm_year = bcd2int(tmp[REG_YEAR]) + 100 * !!(tmp[REG_MONTH] & 0x80) + 100;
  // FIXME: Leap year calculation is wrong in 2100
}
void read_rtc(struct tm *time) __attribute__ ((weak, alias("dsrtc_read")));

/* Set the time in the RTC */
void dsrtc_set(struct tm *time) {
  uint8_t tmp[7];

  if (rtc_state == RTC_NOT_FOUND)
    return;

  tmp[REG_SECOND] = int2bcd(time->tm_sec);
  tmp[REG_MINUTE] = int2bcd(time->tm_min);
  tmp[REG_HOUR]   = int2bcd(time->tm_hour);
  tmp[REG_DOW]    = int2bcd(time->tm_wday+1);
  tmp[REG_DOM]    = int2bcd(time->tm_mday);
  tmp[REG_MONTH]  = int2bcd(time->tm_mon+1) | 0x80 * (time->tm_year >= 2100);
  tmp[REG_YEAR]   = int2bcd(time->tm_year % 100);
  i2c_write_registers(RTC_ADDR, REG_SECOND, 7, tmp);

  if (dsrtc_type == RTC_1307) {
    i2c_write_register(RTC_ADDR, REG_CONTROL_07, 0); // disable SQW output
  } else {
    i2c_write_register(RTC_ADDR, REG_CONTROL_31, 0); // enable oscillator on battery, interrupts off
    i2c_write_register(RTC_ADDR, REG_CTLSTATUS, 0); // clear "oscillator stopped" flag
  }

  rtc_state = RTC_OK;
}
void set_rtc(struct tm *time) __attribute__ ((weak, alias("dsrtc_set")));

/* detect DS RTC type and initialize */
void dsrtc_init(void) {
  int16_t tmp;

  rtc_state = RTC_NOT_FOUND;

  uart_puts_P(PSTR("DSrtc "));
  tmp = i2c_read_register(RTC_ADDR, 0);
  if (tmp < 0) {
    uart_puts_P(PSTR("not found"));
    goto fail;
  }

  /* check if register 0x12 (temp low) is writeable */
  i2c_write_register(RTC_ADDR, REG_TEMP_LSB, 0x55);

  tmp = i2c_read_register(RTC_ADDR, REG_TEMP_LSB);

  if (tmp == 0x55) {
    /* "register" is writeable (actually RAM), RTC is DS1307 */
    dsrtc_type = RTC_1307;
    uart_puts_P(PSTR("1307 "));

    tmp = i2c_read_register(RTC_ADDR, REG_SECOND);

  } else {
    /* register is read-only, RTC is DS3231 */
    dsrtc_type = RTC_3231;
    uart_puts_P(PSTR("3231 "));

    tmp = i2c_read_register(RTC_ADDR, REG_CTLSTATUS);
  }

  if (tmp & STATUS_OSF) {
    rtc_state = RTC_INVALID;
    uart_puts_P(PSTR("invalid"));
  } else {
    rtc_state = RTC_OK;
    uart_puts_P(PSTR("ok"));
  }

fail:
  uart_putcrlf();
}
void rtc_init(void) __attribute__ ((weak, alias("dsrtc_init")));
