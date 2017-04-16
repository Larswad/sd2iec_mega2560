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


   spi.c: Low-level SPI routines, AVR version

*/

#include <avr/io.h>
#include "config.h"
#include "avrcompat.h"
#include "spi.h"

/* interrupts disabled, SPI enabled, MSB first, master mode */
/* leading edge rising, sample on leading edge, clock bits cleared */
#define SPCR_VAL 0b01010000

/* set up SPSR+SPCR according to the divisor */
/* compiles to 3-4 instructions */
static inline __attribute__((always_inline)) void spi_set_divisor(const uint8_t div) {
  if (div == 2 || div == 8 || div == 32) {
    SPSR = _BV(SPI2X);
  } else {
    SPSR = 0;
  }

  if (div == 2 || div == 4) {
    SPCR = SPCR_VAL;
  } else if (div == 8 || div == 16) {
    SPCR = SPCR_VAL | _BV(SPR0);
  } else if (div == 32 || div == 64) {
    SPCR = SPCR_VAL | _BV(SPR1);
  } else { // div == 128
    SPCR = SPCR_VAL | _BV(SPR0) | _BV(SPR1);
  }
}

void spi_set_speed(spi_speed_t speed) {
  if (speed == SPI_SPEED_FAST) {
    spi_set_divisor(SPI_DIVISOR_FAST);
  } else {
    spi_set_divisor(SPI_DIVISOR_SLOW);
  }
}

void spi_init(spi_speed_t speed) {
  /* set up SPI I/O pins */
  SPI_PORT = (SPI_PORT & ~SPI_MASK) | SPI_SCK | SPI_SS | SPI_MISO;
  SPI_DDR  = (SPI_DDR  & ~SPI_MASK) | SPI_SCK | SPI_SS | SPI_MOSI;

  /* enable and initialize SPI */
  if (speed == SPI_SPEED_FAST) {
    spi_set_divisor(SPI_DIVISOR_FAST);
  } else {
    spi_set_divisor(SPI_DIVISOR_SLOW);
  }

  /* Clear buffers, just to be sure */
  (void) SPSR;
  (void) SPDR;
}

/* Simple and braindead, just like the AVR's SPI unit */
static uint8_t spi_exchange_byte(uint8_t output) {
  SPDR = output;
  loop_until_bit_is_set(SPSR, SPIF);
  return SPDR;
}

void spi_tx_byte(uint8_t data) {
  spi_exchange_byte(data);
}

uint8_t spi_rx_byte(void) {
  return spi_exchange_byte(0xff);
}

void spi_exchange_block(void *vdata, unsigned int length, uint8_t write) {
  uint8_t *data = (uint8_t*)vdata;

  while (length--) {
    if (!write)
      SPDR = *data;
    else
      SPDR = 0xff;

    loop_until_bit_is_set(SPSR, SPIF);

    if (write)
      *data = SPDR;
    else
      (void) SPDR;
    data++;
  }
}
