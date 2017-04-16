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


   timer.h: System timer (and button-debouncer)

*/

#ifndef TIMER_H
#define TIMER_H

#include "arch-timer.h"
#include "atomic.h"

// Bit masks for the (simulated) keys
#define KEY_NEXT    (1<<0)
#define KEY_PREV    (1<<1)
#define KEY_HOME    (1<<2)
#define KEY_SLEEP   (1<<3)
/* Remote display service request */
#define KEY_DISPLAY (1<<4)

#define IGNORE_KEYS (1<<7)

/// Logical keys that were pressed - must be reset by the reader.
extern volatile uint8_t active_keys;

#define key_pressed(x) (active_keys & (x))
#define reset_key(x)   active_keys &= (uint8_t)~(x)
#define ignore_keys()  active_keys = IGNORE_KEYS;

/// Global timing variable, 100 ticks per second
/// Use getticks() !
extern volatile tick_t ticks;

/**
 * getticks - return the current system tick count
 *
 * This inline function returns the current system tick count.
 */
static inline tick_t getticks(void) {
  tick_t tmp;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    tmp = ticks;
  }
  return tmp;
}

#define HZ 100

#define MS_TO_TICKS(x) (x/10)

/* Adapted from Linux 2.6 include/linux/jiffies.h:
 *
 *      These inlines deal with timer wrapping correctly. You are
 *      strongly encouraged to use them
 *      1. Because people otherwise forget
 *      2. Because if the timer wrap changes in future you won't have to
 *         alter your driver code.
 *
 * time_after(a,b) returns true if the time a is after time b.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 * (">=0" refers to the time_after_eq macro which wasn't copied)
 */
#define time_after(a,b)         \
         ((stick_t)(b) - (stick_t)(a) < 0)
#define time_before(a,b)        time_after(b,a)


/* Timer initialisation - defined in $ARCH/timer-init.c */
void timer_init(void);

#endif
