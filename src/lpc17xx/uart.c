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


   uart.c: UART access routines for LPC17xx

*/

#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "config.h"
#include "uart.h"

/* clock prescaler - static for now */
#define CONFIG_UART_PCLKDIV 1

/* A few symbols to make this code work for all four UARTs */
#if defined(UART_NUMBER) && UART_NUMBER == 0
#  define UART_PCONBIT 3
#  define UART_PCLKREG PCLKSEL0
#  define UART_PCLKBIT 6
#  define UART_REGS    LPC_UART0
#  define UART_HANDLER UART0_IRQHandler
#  define UART_IRQ     UART0_IRQn
#elif UART_NUMBER == 1
#  define UART_PCONBIT 4
#  define UART_PCLKREG PCLKSEL0
#  define UART_PCLKBIT 8
#  define UART_REGS    LPC_UART1
#  define UART_HANDLER UART1_IRQHandler
#  define UART_IRQ     UART1_IRQn
#elif UART_NUMBER == 2
#  define UART_PCONBIT 24
#  define UART_PCLKREG PCLKSEL1
#  define UART_PCLKBIT 16
#  define UART_REGS    LPC_UART2
#  define UART_HANDLER UART2_IRQHandler
#  define UART_IRQ     UART2_IRQn
#elif UART_NUMBER == 3
#  define UART_PCONBIT 25
#  define UART_PCLKREG PCLKSEL1
#  define UART_PCLKBIT 18
#  define UART_REGS    LPC_UART3
#  define UART_HANDLER UART3_IRQHandler
#  define UART_IRQ     UART3_IRQn
#else
#  error UART_NUMBER is not set or has an invalid value!
#endif

static unsigned int baud2divisor(unsigned int baudrate) {
  return CONFIG_MCU_FREQ / CONFIG_UART_PCLKDIV / 16 / baudrate;
}

static char txbuf[1 << CONFIG_UART_TX_BUF_SHIFT];
static volatile unsigned int read_idx,write_idx;

void UART_HANDLER(void) {
  int iir = UART_REGS->IIR;

  if (!(iir & 1)) {
    /* Interrupt is pending */
    switch (iir & 14) {
#if UART_NUMBER == 1
    case 0: /* modem status */
      (void) UART_REGS->MSR; // dummy read to clear
      break;
#endif

    case 2: /* THR empty - send */
      if (read_idx != write_idx) {
        int maxchars = 16;
        while (read_idx != write_idx && --maxchars > 0) {
          UART_REGS->THR = (unsigned char)txbuf[read_idx];
          read_idx = (read_idx+1) & (sizeof(txbuf)-1);
        }
        if (read_idx == write_idx) {
          /* buffer empty - turn off THRE interrupt */
          BITBAND(UART_REGS->IER, 1) = 0;
        }
      }
      break;

    case 12: /* RX timeout */
    case  4: /* data received - not implemented yet */
      (void) UART_REGS->RBR; // dummy read to clear
      break;

    case 6: /* RX error */
      (void) UART_REGS->LSR; // dummy read to clear

    default: break;
    }
  }
}

void uart_putc(char c) {
  unsigned int tmp = (write_idx+1) & (sizeof(txbuf)-1) ;

  if (read_idx == write_idx && (BITBAND(UART_REGS->LSR, 5))) {
    /* buffer empty, THR empty -> send immediately */
    UART_REGS->THR = (unsigned char)c;
  } else {
#ifdef CONFIG_DEADLOCK_ME_HARDER
    while (tmp == read_idx) ;
#endif
    BITBAND(UART_REGS->IER, 1) = 0; // turn off UART interrupt
    txbuf[write_idx] = c;
    write_idx = tmp;
    BITBAND(UART_REGS->IER, 1) = 1;
  }
}

/* Just for printf */
void uart_putchar(char c) {
  if (c == '\n')
    uart_putc('\r');
  uart_putc(c);
}

/* Polling version only */
unsigned char uart_getc(void) {
  /* wait for character */
  while (!(BITBAND(UART_REGS->LSR, 0))) ;

  return UART_REGS->RBR;
}

/* Returns true if a char is ready */
unsigned char uart_gotc(void) {
  return BITBAND(UART_REGS->LSR, 0);
}

void uart_init(void) {
  /* Turn on power to UART */
  BITBAND(LPC_SC->PCONP, UART_PCONBIT) = 1;

  /* UART clock = CPU clock - this block is reduced at compile-time */
  if (CONFIG_UART_PCLKDIV == 1) {
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT  ) = 1;
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT+1) = 0;
  } else if (CONFIG_UART_PCLKDIV == 2) {
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT  ) = 0;
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT+1) = 1;
  } else if (CONFIG_UART_PCLKDIV == 4) {
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT  ) = 0;
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT+1) = 0;
  } else { // Fallback: Divide by 8
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT  ) = 1;
    BITBAND(LPC_SC->UART_PCLKREG, UART_PCLKBIT+1) = 1;
  }

  /* set baud rate - no fractional stuff for now */
  UART_REGS->LCR = BV(7) | 3; // always 8n1
  UART_REGS->DLL = baud2divisor(CONFIG_UART_BAUDRATE) & 0xff;
  UART_REGS->DLM = (baud2divisor(CONFIG_UART_BAUDRATE) >> 8) & 0xff;
  BITBAND(UART_REGS->LCR, 7) = 0;

  /* reset and enable FIFO */
  UART_REGS->FCR = BV(0);

  /* enable transmit interrupt */
  BITBAND(UART_REGS->IER, 1) = 1;
  NVIC_EnableIRQ(UART_IRQ);

  /* connect to I/O pins */
  uart_pins_connect();
}

/* --- generic code below --- */
void uart_puthex(uint8_t num) {
  uint8_t tmp;
  tmp = (num & 0xf0) >> 4;
  if (tmp < 10)
    uart_putc('0'+tmp);
  else
    uart_putc('a'+tmp-10);

  tmp = num & 0x0f;
  if (tmp < 10)
    uart_putc('0'+tmp);
  else
    uart_putc('a'+tmp-10);
}

void uart_trace(void *ptr, uint16_t start, uint16_t len) {
  uint16_t i;
  uint8_t j;
  uint8_t ch;
  uint8_t *data = ptr;

  data+=start;
  for(i=0;i<len;i+=16) {

    uart_puthex(start>>8);
    uart_puthex(start&0xff);
    uart_putc('|');
    uart_putc(' ');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data + j);
        uart_puthex(ch);
      } else {
        uart_putc(' ');
        uart_putc(' ');
      }
      uart_putc(' ');
    }
    uart_putc('|');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data++);
        if(ch<32 || ch>0x7e)
          ch='.';
        uart_putc(ch);
      } else {
        uart_putc(' ');
      }
    }
    uart_putc('|');
    uart_putcrlf();
    start+=16;
  }
}

void uart_flush(void) {
  while (read_idx != write_idx) ;
}

void uart_puts(const char *text) {
  while (*text) {
    uart_putc(*text++);
  }
}

void uart_puts_P(const char *text) {
  uart_puts(text);
}

void uart_putcrlf(void) {
  uart_putc(13);
  uart_putc(10);
}
