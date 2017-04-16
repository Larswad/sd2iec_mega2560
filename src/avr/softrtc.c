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


   softrtc.c: software RTC emulation

   The exported functions in this file are weak-aliased to their corresponding
   versions defined in rtc.h so when this file is the only RTC implementation
   compiled in they will be automatically used by the linker.

*/

#include <inttypes.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "config.h"
#include "progmem.h"
#include "time.h"
#include "rtc.h"
#include "uart.h"
#include "softrtc.h"

static volatile uint8_t ms;
static softtime_t rtc = 1217647125; // Sat Aug  2 03:18:45 2008 UTC
static const PROGMEM uint8_t month_days[12] = {
  31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

#define days_in_year(a)   (is_leap(a) ? 366 : 365)
#define days_in_month(a)  (pgm_read_byte(month_days + a))
#define is_leap(year) \
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

/* Minimal <time> functionality */

// mktime copied code from Linux kernel source kernel/time.c, ca. 2.6.20

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as softtime_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static softtime_t mktime(struct tm *tm)
{
  uint8_t mon = tm->tm_mon + 1;
  uint16_t year = tm->tm_year + 1900;

  if (0 >= (int) (mon -= 2)) {  /* 1..12 -> 11,12,1..10 */
    mon += 12;  /* Puts Feb last since it has leap day */
    year -= 1;
  }
  return (((
      (uint32_t)(year/4 - year/100 + year/400 + 367*mon/12 + tm->tm_mday) +
        (uint32_t)year*365 - 719499
      )*24 + tm->tm_hour /* now have hours */
     )*60 + tm->tm_min /* now have minutes */
    )*60 + tm->tm_sec; /* finally seconds */
}

static void gmtime(softtime_t *t, struct tm * tm)
{
  uint32_t    tim = *t;
  uint16_t    i;

  tm->tm_sec=tim%60;
  tim/=60; // now it is minutes
  tm->tm_min=tim%60;
  tim/=60; // now it is hours
  tm->tm_hour=tim%24;
  tim/=24; // now it is days
  tm->tm_wday=(tim+4)%7;

  /* Number of years in days */
  for (i = 1970; tim >= days_in_year(i); i++)
    tim -= days_in_year(i);
  tm->tm_year = i - 1900;

  /* Number of months in days left */
  for (i = 0; tim >= days_in_month(i); i++) {
    tim -= days_in_month(i);
    if(i == 1 && is_leap(tm->tm_year))
      tim-= 1;
  }
  tm->tm_mon = i;

  /* Days are what is left over (+1) from all that. */
  tm->tm_mday = tim + 1;
}

/* Public functions */

void softrtc_tick(void) {
  ms++;
  if(ms == 100) {
    rtc++;
    ms = 0;
  }
}

/* Read the current time from the RTC */
void softrtc_read(struct tm *time) {
  softtime_t t;

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    t = rtc;
  }
  gmtime(&t,time);
}
void read_rtc(struct tm *time) __attribute__ ((weak, alias("softrtc_read")));

/* Set the time in the RTC */
void softrtc_set(struct tm *time) {
  softtime_t t = mktime(time);

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    rtc = t;
  }
}
void set_rtc(struct tm *time) __attribute__ ((weak, alias("softrtc_set")));

void softrtc_init(void) {
  rtc_state = RTC_OK;
}
void rtc_init(void) __attribute__ ((weak, alias("softrtc_init")));
