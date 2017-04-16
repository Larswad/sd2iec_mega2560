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


   llfl-common.c: Common subroutines for low-level fastloader code

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "fastloader.h"
#include "iec-bus.h"
#include "llfl-common.h"

#ifdef IEC_OUTPUTS_INVERTED
#  define EMR_LOW  2
#  define EMR_HIGH 1
#else
#  define EMR_LOW  1
#  define EMR_HIGH 2
#endif

uint32_t llfl_reference_time;
static uint32_t timer_a_ccr, timer_b_ccr;

/* ---------- utility functions ---------- */

void llfl_setup(void) {
  /*** set up timer A+B to count synchronized 100ns-units ***/

  /* Reset timers */
  BITBAND(IEC_TIMER_A->TCR, 1) = 1;
  BITBAND(IEC_TIMER_B->TCR, 1) = 1;

  /* Move both timers out of reset */
  BITBAND(IEC_TIMER_A->TCR, 1) = 0;
  BITBAND(IEC_TIMER_B->TCR, 1) = 0;

  /* compensate for timer B offset */
  //FIXME: Values are wrong for 10MHz timers
  //LPC_TIM2->PC = 0;
  //LPC_TIM3->PC = 22;

  /* disable IEC interrupts */
  NVIC_DisableIRQ(IEC_TIMER_A_IRQn);
  NVIC_DisableIRQ(IEC_TIMER_B_IRQn);

  /* Clear all capture/match functions except interrupts */
  timer_a_ccr = IEC_TIMER_A->CCR;
  timer_b_ccr = IEC_TIMER_B->CCR;
  IEC_TIMER_A->CCR = 0b100100;
  IEC_TIMER_B->CCR = 0b100100;
  IEC_TIMER_A->MCR = 0b001001001001;
  IEC_TIMER_B->MCR = 0b001001001001;

  /* Enable both timers */
  BITBAND(IEC_TIMER_A->TCR, 0) = 1;
  BITBAND(IEC_TIMER_B->TCR, 0) = 1;
}

void llfl_teardown(void) {
  /* Reenable stop-on-match for timer A */
  BITBAND(IEC_TIMER_A->MCR, 2) = 1;

  /* Reset all match conditions */
  IEC_TIMER_A->EMR &= 0b1111;
  IEC_TIMER_B->EMR &= 0b1111;

  /* clear capture/match interrupts */
  IEC_TIMER_A->MCR = 0;
  IEC_TIMER_B->MCR = 0;
  IEC_TIMER_A->CCR = timer_a_ccr;
  IEC_TIMER_B->CCR = timer_b_ccr;
  IEC_TIMER_A->IR  = 0b111111;
  IEC_TIMER_B->IR  = 0b111111;

  /* reenable IEC interrupts */
  NVIC_EnableIRQ(IEC_TIMER_A_IRQn);
  NVIC_EnableIRQ(IEC_TIMER_B_IRQn);
}

/**
 * llfl_wait_atn - wait until ATN has the specified level and capture time
 * @state: line level to wait for (0 low, 1 high)
 *
 * This function waits until the ATN line has the specified level and captures
 * the time when its level changed.
 */
void llfl_wait_atn(unsigned int state) {
  /* set up capture */
  BITBAND(IEC_TIMER_ATN->CCR, 3*IEC_CAPTURE_ATN + IEC_IN_COND_INV(!state)) = 1;

  /* clear interrupt flag */
  IEC_TIMER_ATN->IR = BV(4 + IEC_CAPTURE_ATN);

  /* wait until interrupt flag is set */
  while (!BITBAND(IEC_TIMER_ATN->IR, 4+IEC_CAPTURE_ATN)) ;

  /* read event time */
  if (IEC_CAPTURE_ATN == 0) {
    llfl_reference_time = IEC_TIMER_ATN->CR0;
  } else {
    llfl_reference_time = IEC_TIMER_ATN->CR1;
  }

  /* reset capture mode */
  IEC_TIMER_ATN->CCR = 0b100100;
}

/* llfl_wait_clock - see llfl_wait_atn, aborts on ATN low if atnabort is true */
void llfl_wait_clock(unsigned int state, llfl_atnabort_t atnabort) {
  /* set up capture */
  BITBAND(IEC_TIMER_CLOCK->CCR, 3*IEC_CAPTURE_CLOCK + IEC_IN_COND_INV(!state)) = 1;

  /* clear interrupt flag */
  IEC_TIMER_CLOCK->IR = BV(4 + IEC_CAPTURE_CLOCK);

  /* wait until interrupt flag is set */
  while (!BITBAND(IEC_TIMER_CLOCK->IR, 4+IEC_CAPTURE_CLOCK))
    if (atnabort && !IEC_ATN)
      break;

  if (atnabort && !IEC_ATN) {
    /* read current time */
    llfl_reference_time = IEC_TIMER_CLOCK->TC;
  } else {
    /* read event time */
    if (IEC_CAPTURE_CLOCK == 0) {
      llfl_reference_time = IEC_TIMER_CLOCK->CR0;
    } else {
      llfl_reference_time = IEC_TIMER_CLOCK->CR1;
    }
  }

  /* reset capture mode */
  IEC_TIMER_CLOCK->CCR = 0b100100;
}

/* llfl_wait_data - see llfl_wait_atn */
void llfl_wait_data(unsigned int state, llfl_atnabort_t atnabort) {
  /* set up capture */
  BITBAND(IEC_TIMER_DATA->CCR, 3*IEC_CAPTURE_DATA + IEC_IN_COND_INV(!state)) = 1;

  /* clear interrupt flag */
  IEC_TIMER_DATA->IR = BV(4 + IEC_CAPTURE_DATA);

  /* wait until interrupt flag is set */
  while (!BITBAND(IEC_TIMER_DATA->IR, 4+IEC_CAPTURE_DATA))
    if (atnabort && !IEC_ATN)
      break;

  if (atnabort && !IEC_ATN) {
    /* read current time */
    llfl_reference_time = IEC_TIMER_DATA->TC;
  } else {
    /* read event time */
    if (IEC_CAPTURE_DATA == 0) {
      llfl_reference_time = IEC_TIMER_DATA->CR0;
    } else {
      llfl_reference_time = IEC_TIMER_DATA->CR1;
    }
  }

  /* reset capture mode */
  IEC_TIMER_CLOCK->CCR = 0b100100;
}

/**
 * llfl_set_clock_at - sets clock line at a specified time offset
 * @time : change time in 100ns after llfl_reference_time
 * @state: new line state (0 low, 1 high)
 * @wait : wait until change happened if 1
 *
 * This function sets the clock line to a specified state at a defined time
 * after the llfl_reference_time set by a previous wait_* function.
 */
void llfl_set_clock_at(uint32_t time, unsigned int state, llfl_wait_t wait) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_MTIMER_CLOCK->TC > llfl_reference_time+time)
    set_test_led(1);

  /* set match time */
  IEC_MTIMER_CLOCK->IEC_MATCH_CLOCK = llfl_reference_time + time;

  /* reset match interrupt flag */
  IEC_MTIMER_CLOCK->IR = BV(IEC_OPIN_CLOCK);

  /* set match action */
  if (state) {
    IEC_MTIMER_CLOCK->EMR = (IEC_MTIMER_CLOCK->EMR & ~(3 << (4+IEC_OPIN_CLOCK*2))) |
                             EMR_HIGH << (4+IEC_OPIN_CLOCK*2);
  } else {
    IEC_MTIMER_CLOCK->EMR = (IEC_MTIMER_CLOCK->EMR & ~(3 << (4+IEC_OPIN_CLOCK*2))) |
                             EMR_LOW << (4+IEC_OPIN_CLOCK*2);
  }

  /* optional: wait for match */
  if (wait)
    while (!BITBAND(IEC_MTIMER_CLOCK->IR, IEC_OPIN_CLOCK)) ;
}

/* llfl_set_data_at - see llfl_set_clock_at */
void llfl_set_data_at(uint32_t time, unsigned int state, llfl_wait_t wait) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_MTIMER_DATA->TC > llfl_reference_time+time)
    set_test_led(1);

  /* set match time */
  IEC_MTIMER_DATA->IEC_MATCH_DATA = llfl_reference_time + time;

  /* reset match interrupt flag */
  IEC_MTIMER_DATA->IR = BV(IEC_OPIN_DATA);

  /* set match action */
  if (state) {
    IEC_MTIMER_DATA->EMR = (IEC_MTIMER_DATA->EMR & ~(3 << (4+IEC_OPIN_DATA*2))) |
                            EMR_HIGH << (4+IEC_OPIN_DATA*2);
  } else {
    IEC_MTIMER_DATA->EMR = (IEC_MTIMER_DATA->EMR & ~(3 << (4+IEC_OPIN_DATA*2))) |
                            EMR_LOW << (4+IEC_OPIN_DATA*2);
  }

  /* optional: wait for match */
  if (wait)
    while (!BITBAND(IEC_MTIMER_DATA->IR, IEC_OPIN_DATA)) ;
}

/* llfl_set_srq_at - see llfl_set_clock_at (nice for debugging) */
void llfl_set_srq_at(uint32_t time, unsigned int state, llfl_wait_t wait) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_MTIMER_SRQ->TC > llfl_reference_time+time)
    set_test_led(1);

  /* set match time */
  IEC_MTIMER_SRQ->IEC_MATCH_SRQ = llfl_reference_time + time;

  /* reset match interrupt flag */
  IEC_MTIMER_SRQ->IR = BV(IEC_OPIN_SRQ);

  /* set match action */
  if (state) {
    IEC_MTIMER_SRQ->EMR = (IEC_MTIMER_SRQ->EMR & ~(3 << (4+IEC_OPIN_SRQ*2))) |
                           EMR_HIGH << (4+IEC_OPIN_SRQ*2);
  } else {
    IEC_MTIMER_SRQ->EMR = (IEC_MTIMER_SRQ->EMR & ~(3 << (4+IEC_OPIN_SRQ*2))) |
                           EMR_LOW << (4+IEC_OPIN_SRQ*2);
  }

  /* optional: wait for match */
  if (wait)
    while (!BITBAND(IEC_MTIMER_SRQ->IR, IEC_OPIN_SRQ)) ;
}

/**
 * llfl_read_bus_at - reads the IEC bus at a certain time
 * @time: read time in 100ns after llfl_reference_time
 *
 * This function returns the current IEC bus state at a certain time
 * after the llfl_reference_time set by a previous wait_* function.
 */
uint32_t llfl_read_bus_at(uint32_t time) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_TIMER_A->TC >= llfl_reference_time+time)
    set_test_led(1);

  // FIXME: This could be done in hardware using DMA
  /* Wait until specified time */
  while (IEC_TIMER_A->TC < llfl_reference_time+time) ;

  return iec_bus_read();
}

/**
 * llfl_now - returns current timer count
 *
 * This function returns the current timer value
 */
uint32_t llfl_now(void) {
  return IEC_TIMER_A->TC;
}

/**
 * llfl_generic_load_2bit - generic 2-bit fastloader transmit
 * @def : pointer to fastloader definition struct
 * @byte: data byte
 *
 * This function implements generic 2-bit fastloader
 * transmission based on a generic_2bit_t struct.
 */
void llfl_generic_load_2bit(const generic_2bit_t *def, uint8_t byte) {
  unsigned int i;

  byte ^= def->eorvalue;

  for (i=0;i<4;i++) {
    llfl_set_clock_at(def->pairtimes[i], byte & (1 << def->clockbits[i]), NO_WAIT);
    llfl_set_data_at (def->pairtimes[i], byte & (1 << def->databits[i]),  WAIT);
  }
}

/**
 * llfl_generic_save_2bit - generic 2-bit fastsaver receive
 * @def: pointer to fastloader definition struct
 *
 * This function implements genereic 2-bit fastsaver reception
 * based on a generic_2bit_t struct.
 */
uint8_t llfl_generic_save_2bit(const generic_2bit_t *def) {
  unsigned int i;
  uint8_t result = 0;

  for (i=0;i<4;i++) {
    uint32_t bus = llfl_read_bus_at(def->pairtimes[i]);

    result |= (!!(bus & IEC_BIT_CLOCK)) << def->clockbits[i];
    result |= (!!(bus & IEC_BIT_DATA))  << def->databits[i];
  }

  return result ^ def->eorvalue;
}
