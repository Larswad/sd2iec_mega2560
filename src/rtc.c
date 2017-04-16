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


   rtc.c: FatFS support function

*/

#include <inttypes.h>
#include "config.h"
#include "ds1307-3231.h"
#include "pcf8583.h"
#include "progmem.h"
#include "rtc_lpc17xx.h"
#include "softrtc.h"
#include "time.h"
#include "rtc.h"

rtcstate_t rtc_state;

/* Default date/time if the RTC isn't present or not set: 1982-08-31 00:00:00 */
const PROGMEM struct tm rtc_default_date = {
  0, 0, 0, 31, 8-1, 82, 2
};

/* Return current time in a FAT-compatible format */
uint32_t get_fattime(void) {
  struct tm time;

  read_rtc(&time);
  return ((uint32_t)time.tm_year-80) << 25 |
    ((uint32_t)time.tm_mon+1) << 21 |
    ((uint32_t)time.tm_mday)  << 16 |
    ((uint32_t)time.tm_hour)  << 11 |
    ((uint32_t)time.tm_min)   << 5  |
    ((uint32_t)time.tm_sec)   >> 1;
}

#ifdef NEED_RTCMUX
/* RTC "multiplexer" to select the best available RTC at runtime */

typedef enum { RTC_NONE, RTC_SOFTWARE, RTC_PCF8583,
               RTC_LPC17XX, RTC_DSRTC } rtc_type_t;

static rtc_type_t current_rtc = RTC_NONE;

void rtc_init(void) {
#ifdef CONFIG_RTC_DSRTC
  dsrtc_init();
  if (rtc_state != RTC_NOT_FOUND) {
    current_rtc = RTC_DSRTC;
    return;
  }
#endif

#ifdef CONFIG_RTC_PCF8583
  pcf8583_init();
  if (rtc_state != RTC_NOT_FOUND) {
    current_rtc = RTC_PCF8583;
    return;
  }
#endif

#ifdef CONFIG_RTC_LPC17XX
  lpcrtc_init();
  if (rtc_state != RTC_NOT_FOUND) {
    current_rtc = RTC_LPC17XX;
    return;
  }
#endif

#ifdef CONFIG_RTC_SOFTWARE
  softrtc_init();
  /* This is the fallback RTC that will always work */
  return;
#endif

  /* none of the enabled RTCs were found */
  rtc_state = RTC_NOT_FOUND;
}

void read_rtc(struct tm *time) {
  switch (current_rtc) {

#ifdef CONFIG_RTC_DSRTC
  case RTC_DSRTC:
    dsrtc_read(time);
    break;
#endif

#ifdef CONFIG_RTC_PCF8583
  case RTC_PCF8583:
    pcf8583_read(time);
    break;
#endif

#ifdef CONFIG_RTC_LPC17XX
  case RTC_LPC17XX:
    lpcrtc_read(time);
    break;
#endif

#ifdef CONFIG_RTC_SOFTWARE
  case RTC_SOFTWARE:
    softrtc_read(time);
    break;
#endif

  case RTC_NONE:
  default:
    return;
  }
}

void set_rtc(struct tm *time) {
  switch (current_rtc) {

#ifdef CONFIG_RTC_DSRTC
  case RTC_DSRTC:
    dsrtc_set(time);
    break;
#endif

#ifdef CONFIG_RTC_PCF8583
  case RTC_PCF8583:
    pcf8583_set(time);
    break;
#endif

#ifdef CONFIG_RTC_LPC17XX
  case RTC_LPC17XX:
    lpcrtc_set(time);
    break;
#endif

#ifdef CONFIG_RTC_SOFTWARE
  case RTC_SOFTWARE:
    softrtc_set(time);
    break;
#endif

  case RTC_NONE:
  default:
    return;
  }
}

#endif
