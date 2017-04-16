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

/* Types for unsigned and signed tick values */
typedef uint32_t tick_t;
typedef int32_t stick_t;

/* Delay functions */
// FIXME: Is delay_us accurate enough as function?
void delay_us(unsigned int time);
void delay_ms(unsigned int time);

/* Timeout functions */
// FIXME: Accurate enough as function?
void start_timeout(unsigned int usecs);
unsigned int has_timed_out(void);

#endif
