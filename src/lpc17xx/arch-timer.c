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


   arch-timer.c: Architecture-specific timer functions

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "timer.h"

/* bit definitions */
#define RITINT 0
#define RITEN  3
#define PCRIT 16

void timer_init(void) {
  /* turn on power to RIT */
  BITBAND(LPC_SC->PCONP, PCRIT) = 1;

  /* clear RIT mask */
  LPC_RIT->RIMASK = 0; //xffffffff;

  /* PCLK = CCLK */
  BITBAND(LPC_SC->PCLKSEL1, 26) = 1;

  /* enable SysTick */
  SysTick_Config(SysTick->CALIB & SysTick_CALIB_TENMS_Msk);

  /*** set up IEC timers to count microseconds ***/

  /* enable power to timers */
  BITBAND(LPC_SC->PCONP, IEC_TIMER_A_PCONBIT)   = 1;
  BITBAND(LPC_SC->PCONP, IEC_TIMER_B_PCONBIT)   = 1;
  BITBAND(LPC_SC->PCONP, TIMEOUT_TIMER_PCONBIT) = 1;

  /* Use full 100MHz clock */
  BITBAND(LPC_SC->IEC_TIMER_A_PCLKREG, IEC_TIMER_A_PCLKBIT)     = 1;
  BITBAND(LPC_SC->IEC_TIMER_B_PCLKREG, IEC_TIMER_B_PCLKBIT)     = 1;
  BITBAND(LPC_SC->TIMEOUT_TIMER_PCLKREG, TIMEOUT_TIMER_PCLKBIT) = 1;

  /* keep timers reset */
  BITBAND(IEC_TIMER_A->TCR, 1)   = 1;
  BITBAND(IEC_TIMER_B->TCR, 1)   = 1;
  BITBAND(TIMEOUT_TIMER->TCR, 1) = 1;

  /* prescale 100MHz down to 10 */
  IEC_TIMER_A->PR   = 10-1;
  IEC_TIMER_B->PR   = 10-1;
  TIMEOUT_TIMER->PR = 10-1;

  /* enable all capture interrupts for IEC */
  IEC_TIMER_A->CCR = 0b100100;
  IEC_TIMER_B->CCR = 0b100100;

  /* Move timers out of reset */
  BITBAND(IEC_TIMER_A->TCR, 1)   = 0;
  BITBAND(IEC_TIMER_B->TCR, 1)   = 0;
  BITBAND(TIMEOUT_TIMER->TCR, 1) = 0;

  /* stop timeout-timer on match */
  BITBAND(TIMEOUT_TIMER->MCR, 2) = 1; // MR0S - stop timer on match

  /* Enable timers */
  BITBAND(IEC_TIMER_A->TCR, 0)   = 1;
  BITBAND(IEC_TIMER_B->TCR, 0)   = 1;
  BITBAND(TIMEOUT_TIMER->TCR, 0) = 1;

  /* enable interrupts for the IEC timers */
  NVIC_EnableIRQ(IEC_TIMER_A_IRQn);
  NVIC_EnableIRQ(IEC_TIMER_B_IRQn);
}

void delay_us(unsigned int time) {
  /* Prepare RIT */
  LPC_RIT->RICOUNTER = 0;
  LPC_RIT->RICOMPVAL = (CONFIG_MCU_FREQ / 1000000) * time;
  LPC_RIT->RICTRL    = BV(RITEN) | BV(RITINT);

  /* Wait until RIT signals an interrupt */
  while (!(BITBAND(LPC_RIT->RICTRL, RITINT))) ;

  /* Disable RIT */
  LPC_RIT->RICTRL = 0;
}

void delay_ms(unsigned int time) {
  /* Prepare RIT */
  LPC_RIT->RICOUNTER = 0;
  LPC_RIT->RICTRL    = BV(RITEN) | BV(RITINT);
  LPC_RIT->RICOMPVAL = (CONFIG_MCU_FREQ / 1000) * time;

  /* Wait until RIT signals an interrupt */
  while (!(BITBAND(LPC_RIT->RICTRL, RITINT))) ;

  /* Disable RIT */
  LPC_RIT->RICTRL = 0;
}

/**
 * start_timeout - start a timeout
 * @usecs: number of microseconds before timeout
 *
 * This function sets up a timer so it times out after the specified
 * number of microseconds.
 */
void start_timeout(unsigned int usecs) {
  TIMEOUT_TIMER->TC  = 0;
  TIMEOUT_TIMER->MR0 = usecs*10;
  BITBAND(TIMEOUT_TIMER->TCR, 0) = 1; // start timer
}

/**
 * has_timed_out - returns true if timeout was reached
 *
 * This function returns true if the timer started by start_timeout
 * has reached its timeout value.
 */
unsigned int has_timed_out(void) {
  return !BITBAND(TIMEOUT_TIMER->TCR, 0);
}
