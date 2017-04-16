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


   i2c_lpc17xx.c: Hardware I2C bus master for LPC17xx

*/

#include <stdbool.h>
#include <stddef.h>
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "config.h"
#include "i2c.h"

#if I2C_NUMBER == 0
#  define I2C_REGS    LPC_I2C0
#  define I2C_PCLKREG PCLKSEL0
#  define I2C_PCLKBIT 14
#  define I2C_HANDLER I2C0_IRQHandler
#  define I2C_IRQ     I2C0_IRQn
#elif I2C_NUMBER == 1
#  define I2C_REGS    LPC_I2C1
#  define I2C_PCLKREG PCLKSEL1
#  define I2C_PCLKBIT 6
#  define I2C_HANDLER I2C1_IRQHandler
#  define I2C_IRQ     I2C1_IRQn
#elif I2C_NUMBER == 2
#  define I2C_REGS    LPC_I2C2
#  define I2C_PCLKREG PCLKSEL1
#  define I2C_PCLKBIT 20
#  define I2C_HANDLER I2C2_IRQHandler
#  define I2C_IRQ     I2C2_IRQn
#else
#  error I2C_NUMBER is not set or has an invalid value!
#endif

#define I2CEN  6
#define I2CSTA 5
#define I2CSTO 4
#define I2CSI  3
#define I2CAA  2

#define RESULT_NONE      0
#define RESULT_ADDR_NACK 1
#define RESULT_DATA_NACK 2
#define RESULT_BUSERROR  3
#define RESULT_DONE      4

/* internal buffers for read/write_register(s) */
static uint8_t    reg_buffer;
static i2cblock_t data_block;
static i2cblock_t reg_block = {
  1, &reg_buffer, &data_block
};

static i2cblock_t *current_block;
static unsigned int count;
static unsigned char address, write_buffers;
static bool read_mode;
static volatile unsigned char *bufferptr;
static volatile char result;

/* returns true if no more data is available */
static bool data_available(void) {
  /* if count is 0, there was no new buffer to switch to */
  return (count != 0);
}

/* returns true if only one more byte can be stored/read */
static bool on_last_byte(void) {
  if (count == 1 && current_block->next == NULL)
    return true;
  else
    return false;
}

/* returns the next byte from the buffer chain */
static uint8_t read_byte(void) {
  uint8_t b = *bufferptr++;
  count--;

  if (count == 0 && current_block->next != NULL) {
    current_block = current_block->next;
    count     = current_block->length;
    bufferptr = current_block->data;
    write_buffers--;
  }

  return b;
}

/* write one byte to the buffer chain */
static void write_byte(uint8_t v) {
  *bufferptr++ = v;
  count--;

  if (count == 0 && current_block->next) {
    /* at least one more buffer */
    current_block = current_block->next;
    count         = current_block->length;
    bufferptr     = current_block->data;
  }
}

void I2C_HANDLER(void) {
  unsigned int tmp = I2C_REGS->I2STAT;

  switch (tmp) { //I2C_REGS->I2STAT) {
  case 0x08: // START transmitted
  case 0x10: // repeated START transmitted
    /* send address */
    I2C_REGS->I2DAT = address;
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x18: // SLA+W transmitted, ACK received
    /* send first data byte */
    I2C_REGS->I2DAT = read_byte();
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x20: // SLA+W transmitted, no ACK
  case 0x30: // byte transmitted, no ACK
  case 0x48: // SLA+R transmitted, no ACK
    /* send stop */
    result = RESULT_ADDR_NACK;
    I2C_REGS->I2CONSET = BV(I2CSTO);
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x28: // byte transmitted, ACK received
    /* send next data byte or stop */
    if (read_mode && write_buffers == 0) {
      /* switch to master receiver mode */
      address |= 1;
      I2C_REGS->I2CONSET = BV(I2CSTA);
      I2C_REGS->I2CONCLR = BV(I2CSI);
    } else {
      if (data_available()) {
        I2C_REGS->I2DAT = read_byte();
        I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
      } else {
        I2C_REGS->I2CONSET = BV(I2CSTO);
        I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
        result = RESULT_DONE;
      }
    }
    break;

  case 0x38: // arbitration lost
    /* try to send start again (assumes arbitration was not lost in data transmission */
    I2C_REGS->I2CONSET = BV(I2CSTA);
    I2C_REGS->I2CONCLR = BV(I2CSTO) | BV(I2CSI);
    break;

  case 0x40: // SLA+R transmitted, ACK received
    /* prepare read cycle */
    if (on_last_byte())
      I2C_REGS->I2CONCLR = BV(I2CAA);
    else
      I2C_REGS->I2CONSET = BV(I2CAA);

    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x50: // data byte received, ACK sent
    /* decide to ACK/NACK next cycle, read current byte */
    write_byte(I2C_REGS->I2DAT);

    if (on_last_byte())
      I2C_REGS->I2CONCLR = BV(I2CAA);
    else
      I2C_REGS->I2CONSET = BV(I2CAA);

    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x58: // data byte received, no ACK sent
    /* read last byte, send stop */
    write_byte(I2C_REGS->I2DAT);
    I2C_REGS->I2CONSET = BV(I2CSTO);
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    result = RESULT_DONE;
    break;

  case 0x00: // bus error
    I2C_REGS->I2CONSET = BV(I2CSTO);
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    result = RESULT_BUSERROR;
    break;

  default:
    //printf("i2c:%02x\n",tmp);
    break;
  }
}

void i2c_init(void) {
  /* Set up I2C clock prescaler */
  if (I2C_PCLKDIV == 1) {
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 1;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 0;
  } else if (I2C_PCLKDIV == 2) {
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 0;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 1;
  } else if (I2C_PCLKDIV == 4) {
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 0;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 0;
  } else { // Fallback: Divide by 8
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 1;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 1;
  }

  /* Set I2C clock (symmetric) */
  I2C_REGS->I2SCLH = CONFIG_MCU_FREQ / I2C_CLOCK / I2C_PCLKDIV / 2;
  I2C_REGS->I2SCLL = CONFIG_MCU_FREQ / I2C_CLOCK / I2C_PCLKDIV / 2;

  /* Enable I2C interrupt */
  NVIC_EnableIRQ(I2C_IRQ);

  /* Enable I2C */
  BITBAND(I2C_REGS->I2CONSET, I2CEN) = 1;
  I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI) | BV(I2CAA);

  /* connect to I/O pins */
  i2c_pins_connect();
}

uint8_t i2c_write_registers(uint8_t address_, uint8_t startreg, uint8_t count_, const void *data) {
  reg_buffer        = startreg;
  data_block.length = count_;
  data_block.data   = (void *)data;

  return i2c_write_blocks(address_, &reg_block);
}

uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) {
  return i2c_write_registers(address, reg, 1, &val);
}

uint8_t i2c_read_registers(uint8_t address_, uint8_t startreg, uint8_t count_, void *data) {
  reg_buffer        = startreg;
  data_block.length = count_;
  data_block.data   = data;

  uint8_t res = i2c_read_blocks(address_, &reg_block, 1);

  /* tell gcc that the contents of data have changed */
  asm volatile ("" : "=m" (*(char *)data));

  return res;
}

int16_t i2c_read_register(uint8_t address, uint8_t reg) {
  uint8_t tmp = 0;

  if (i2c_read_registers(address, reg, 1, &tmp))
    return -1;
  else
    return tmp;
}

uint8_t i2c_write_blocks(uint8_t addr, i2cblock_t *head) {
  result        = RESULT_NONE;
  address       = addr;
  bufferptr     = head->data;
  count         = head->length;
  read_mode     = false;
  write_buffers = 0;
  current_block = head;

  /* send start condition */
  BITBAND(I2C_REGS->I2CONSET, I2CSTA) = 1;

  /* wait until ISR is done */
  while (result == RESULT_NONE)
    __WFI();

  return (result != RESULT_DONE);
}

uint8_t i2c_read_blocks(uint8_t addr, i2cblock_t *head, unsigned char writeblocks) {
  result        = RESULT_NONE;
  address       = addr & 0xfe;
  bufferptr     = head->data;
  count         = head->length;
  read_mode     = true;
  write_buffers = writeblocks;
  current_block = head;

  /* send start condition */
  BITBAND(I2C_REGS->I2CONSET, I2CSTA) = 1;

  /* wait until ISR is done */
  while (result == RESULT_NONE)
    __WFI();

  return (result != RESULT_DONE);
}
