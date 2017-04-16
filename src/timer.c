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


   timer.c: System timer (and button debouncer)

*/

#include "config.h"
#include "diskchange.h"
#include "display.h"
#include "led.h"
#include "time.h"
#include "rtc.h"
#include "softrtc.h"
#include "timer.h"

#define DEBOUNCE_TICKS 4
#define SLEEP_TICKS    2*HZ

volatile tick_t ticks;
// Logical buttons
volatile uint8_t active_keys;

// Physical buttons
rawbutton_t buttonstate;
tick_t      lastbuttonchange;

/* Called by the timer interrupt when the button state has changed */
static void buttons_changed(void) {
  /* Check if the previous state was stable for two ticks */
  if (time_after(ticks, lastbuttonchange + DEBOUNCE_TICKS)) {
    if (active_keys & IGNORE_KEYS) {
      active_keys &= ~IGNORE_KEYS;
    } else if (BUTTON_PREV && /* match only if PREV exists */
               !(buttonstate & (BUTTON_PREV|BUTTON_NEXT))) {
      /* Both buttons held down */
        active_keys |= KEY_HOME;
    } else if (!(buttonstate & BUTTON_NEXT) &&
               (buttons_read() & BUTTON_NEXT)) {
      /* "Next" button released */
      active_keys |= KEY_NEXT;
    } else if (BUTTON_PREV && /* match only if PREV exists */
               !(buttonstate & BUTTON_PREV) &&
               (buttons_read() & BUTTON_NEXT)) {
      active_keys |= KEY_PREV;
    }
  }

  lastbuttonchange = ticks;
  buttonstate = buttons_read();
}

/* The main timer interrupt */
SYSTEM_TICK_HANDLER {
  rawbutton_t tmp = buttons_read();

  if (tmp != buttonstate) {
    buttons_changed();
  }

  ticks++;

#ifdef SINGLE_LED
  if (led_state & LED_ERROR) {
    if ((ticks & 15) == 0)
      toggle_led();
  } else {
    set_led((led_state & LED_BUSY) || (led_state & LED_DIRTY));
  }
#else
  if (led_state & LED_ERROR)
    if ((ticks & 15) == 0)
      toggle_dirty_led();
#endif

  /* Sleep button triggers when held down for 2sec */
  if (time_after(ticks, lastbuttonchange + DEBOUNCE_TICKS)) {
    if (!(buttonstate & BUTTON_NEXT) &&
        (!BUTTON_PREV || (buttonstate & BUTTON_PREV)) &&
        time_after(ticks, lastbuttonchange + SLEEP_TICKS) &&
        !key_pressed(KEY_SLEEP)) {
      /* Set ignore flag so the release doesn't trigger KEY_NEXT */
      active_keys |= KEY_SLEEP | IGNORE_KEYS;
      /* Avoid triggering for the next two seconds */
      lastbuttonchange = ticks;
    }
  }

  /* send tick to the software RTC emulation */
  softrtc_tick();

#ifdef CONFIG_REMOTE_DISPLAY
  /* Check if the display wants to be queried */
  if (display_intrq_active()) {
    active_keys |= KEY_DISPLAY;
  }
#endif
}
