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


   arch-timer.h: Architecture-specific system timer definitions

*/

#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H

#include <util/delay.h>

#define delay_ms(x) _delay_ms(x)
#define delay_us(x) _delay_us(x)

/* Types for unsigned and signed tick values */
typedef uint16_t tick_t;
typedef int16_t stick_t;

/**
 * start_timeout - start a timeout using timer0
 * @usecs: number of microseconds before timeout (maximum 256 for 8MHz clock)
 *
 * This function sets timer 0 so it will time out after the specified number
 * of microseconds. DON'T use a variable as parameter because it would cause
 * run-time floating point calculations (slow and huge).
 */
static inline __attribute__((always_inline)) void start_timeout(uint16_t usecs) {
  TCNT0  = 256 - ((float)F_CPU/8000000.0) * usecs;
  TIFR0 |= _BV(TOV0);
}

/**
 * has_timed_out - returns true if timeout was reached
 *
 * This function returns true if the overflow flag of timer 0 is set which
 * (together with start_timeout and TIMEOUT_US) will happen when the
 * specified time has elapsed.
 */
static inline uint8_t has_timed_out(void) {
  return TIFR0 & _BV(TOV0);
}

#endif
