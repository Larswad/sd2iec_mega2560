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


   ieee.c: IEEE-488 handling code by Nils Eilers <nils.eilers@gmx.de>

*/

#include "config.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/atomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "timer.h"
#include "uart.h"
#include "buffers.h"
#include "d64ops.h"
#include "diskchange.h"
#include "diskio.h"
#include "doscmd.h"
#include "fatops.h"
#include "fileops.h"
#include "filesystem.h"
#include "led.h"
#include "ieee.h"
#include "fastloader.h"
#include "errormsg.h"
#include "ctype.h"
#include "display.h"
#include "system.h"

/*
  Debug output:

  AXX   : ATN 0xXX
  c     : listen_handler cancelled
  C     : CLOSE
  l     : UNLISTEN
  L     : LISTEN
  D     : DATA 0x60
  O     : OPEN 0xfX
  ?XX   : unknown cmd 0xXX
  .     : timeout after ATN

*/

#define FLAG_EOI 256
#define FLAG_ATN 512

#define IEEE_TIMEOUT_MS 64

#define uart_puts_p(__s) uart_puts_P(PSTR(__s))
#define EOI_RECVD       (1<<0)
#define COMMAND_RECVD   (1<<1)
#define ATN_POLLED      -3      // 0xfd
#define TIMEOUT_ABORT   -4      // 0xfc

/* ------------------------------------------------------------------------- */
/*  Global variables                                                         */
/* ------------------------------------------------------------------------- */

uint8_t detected_loader = FL_NONE;      /* Workaround serial fastloader */
uint8_t device_address;                 /* Current device address */
static tick_t timeout;                  /* timeout on getticks()=timeout */

/**
 * struct ieeeflags_t - Bitfield of various flags, mostly IEEE-related
 * @eoi_recvd      : Received EOI with the last byte read
 * @command_recvd  : Command or filename received
 *
 * This is a bitfield for a number of boolean variables used
 */

struct {
  uint8_t ieeeflags;
  enum {    BUS_IDLE = 0,
            BUS_FOUNDATN,
            BUS_ATNPROCESS,
            BUS_SLEEP
  } bus_state;

  enum {    DEVICE_IDLE = 0,
            DEVICE_LISTEN,
            DEVICE_TALK
  } device_state;
  uint8_t secondary_address;
} ieee_data;

/* ------------------------------------------------------------------------- */
/*  Initialization and very low-level bus handling                           */
/* ------------------------------------------------------------------------- */

static inline void set_eoi_state(uint8_t x)
{
  if(x) {                                           // Set EOI high
    IEEE_DDR_EOI &= (uint8_t)~_BV(IEEE_PIN_EOI);    // EOI as input
    IEEE_PORT_EOI |= (uint8_t)_BV(IEEE_PIN_EOI);    // Enable pull-up
  } else {                                          // Set EOI low
    IEEE_PORT_EOI &= (uint8_t)~_BV(IEEE_PIN_EOI);   // EOI low
    IEEE_DDR_EOI |= (uint8_t) _BV(IEEE_PIN_EOI);    // EOI as output
  }
}

/* Read port bits */
# define IEEE_ATN        (IEEE_INPUT_ATN  & _BV(IEEE_PIN_ATN))
# define IEEE_NDAC       (IEEE_INPUT_NDAC & _BV(IEEE_PIN_NDAC))
# define IEEE_NRFD       (IEEE_INPUT_NRFD & _BV(IEEE_PIN_NRFD))
# define IEEE_DAV        (IEEE_INPUT_DAV  & _BV(IEEE_PIN_DAV))
# define IEEE_EOI        (IEEE_INPUT_EOI  & _BV(IEEE_PIN_EOI))

#ifdef HAVE_7516X
# define ddr_change_by_atn() \
    if(ieee_data.device_state == DEVICE_TALK) ieee_ports_listen()
# define set_ieee_data(data) IEEE_D_PORT = (uint8_t) ~ data

  static inline void set_te_state(uint8_t x)
  {
    if(x) IEEE_PORT_TE |= _BV(IEEE_PIN_TE);
    else  IEEE_PORT_TE &= ~_BV(IEEE_PIN_TE);
  }

  static inline void set_ndac_state(uint8_t x)
  {
    if (x) IEEE_PORT_NDAC |= _BV(IEEE_PIN_NDAC);
    else   IEEE_PORT_NDAC &= ~_BV(IEEE_PIN_NDAC);
  }

  static inline void set_nrfd_state(uint8_t x)
  {
    if(x) IEEE_PORT_NRFD |= _BV(IEEE_PIN_NRFD);
    else  IEEE_PORT_NRFD &= ~_BV(IEEE_PIN_NRFD);
  }

  static inline void set_dav_state(uint8_t x)
  {
    if(x) IEEE_PORT_DAV |= _BV(IEEE_PIN_DAV);
    else  IEEE_PORT_DAV &= ~_BV(IEEE_PIN_DAV);
  }

  // Configure bus to passive/listen or talk
  // Toogle direction of I/O pins and safely avoid connecting two outputs

  static inline void ieee_ports_listen (void)
  {
    IEEE_D_DDR = 0;                                 // data ports as inputs
    IEEE_D_PORT = 0xff;                     // enable pull-ups for data lines
    IEEE_DDR_DAV &= (uint8_t) ~_BV(IEEE_PIN_DAV);   // DAV as input
    IEEE_DDR_EOI &= (uint8_t) ~_BV(IEEE_PIN_EOI);   // EOI as input
    set_te_state(0);                                // 7516x listen
    IEEE_DDR_NDAC |= _BV(IEEE_PIN_NDAC);            // NDAC as output
    IEEE_DDR_NRFD |= _BV(IEEE_PIN_NRFD);            // NRFD as output
    IEEE_PORT_DAV |= _BV(IEEE_PIN_DAV);             // Enable pull-up for DAV
    IEEE_PORT_EOI |= _BV(IEEE_PIN_EOI);             // Enable pull-up for EOI
  }

  static inline void ieee_ports_talk (void)
  {
    IEEE_DDR_NDAC &= (uint8_t)~_BV(IEEE_PIN_NDAC);  // NDAC as input
    IEEE_DDR_NRFD &= (uint8_t)~_BV(IEEE_PIN_NRFD);  // NRFD as input
    IEEE_PORT_NDAC |= _BV(IEEE_PIN_NDAC);           // Enable pull-up for NDAC
    IEEE_PORT_NRFD |= _BV(IEEE_PIN_NRFD);           // Enable pull-up for NRFD
    set_te_state(1);                                // 7516x talk enable
    IEEE_D_PORT = 0xff;                             // all data lines high
    IEEE_D_DDR = 0xff;                              // data ports as outputs
    set_dav_state(1);                               // Set DAV high
    IEEE_DDR_DAV |= _BV(IEEE_PIN_DAV);              // DAV as output
    set_eoi_state(1);                               // Set EOI high
    IEEE_DDR_EOI |= _BV(IEEE_PIN_EOI);              // EOI as output
  }

  static void inline ieee_bus_idle (void)
  {
    ieee_ports_listen();
    set_ndac_state(1);
    set_nrfd_state(1);
  }

#else   /* HAVE_7516X */
  /* ----------------------------------------------------------------------- */
  /*  Poor men's variant without IEEE bus drivers                            */
  /* ----------------------------------------------------------------------- */

  static inline void set_ndac_state (uint8_t x)
  {
    if(x) {                                             // Set NDAC high
      IEEE_DDR_NDAC &= (uint8_t)~_BV(IEEE_PIN_NDAC);    // NDAC as input
      IEEE_PORT_NDAC |= _BV(IEEE_PIN_NDAC);             // Enable pull-up
    } else {                                            // Set NDAC low
      IEEE_PORT_NDAC &= (uint8_t)~_BV(IEEE_PIN_NDAC);   // NDAC low
      IEEE_DDR_NDAC |= _BV(IEEE_PIN_NDAC);              // NDAC as output
    }
  }

  static inline void set_nrfd_state (uint8_t x)
  {
    if(x) {                                             // Set NRFD high
      IEEE_DDR_NRFD &= (uint8_t)~_BV(IEEE_PIN_NRFD);    // NRFD as input
      IEEE_PORT_NRFD |= _BV(IEEE_PIN_NRFD);             // Enable pull-up
    } else {                                            // Set NRFD low
      IEEE_PORT_NRFD &= (uint8_t)~_BV(IEEE_PIN_NRFD);   // NRFD low
      IEEE_DDR_NRFD |= (uint8_t) _BV(IEEE_PIN_NRFD);    // NRFD as output
    }
  }

  static inline void set_dav_state (uint8_t x)
  {
    if(x) {                                             // Set DAV high
      IEEE_DDR_DAV &= (uint8_t)~_BV(IEEE_PIN_DAV);      // DAV as input
      IEEE_PORT_DAV |= _BV(IEEE_PIN_DAV);               // Enable pull-up
    } else {                                            // Set DAV low
      IEEE_PORT_DAV &= (uint8_t)~_BV(IEEE_PIN_DAV);     // DAV low
      IEEE_DDR_DAV |= _BV(IEEE_PIN_DAV);                // DAV as output
    }
  }

# define set_te_state(dummy) do { } while (0)       // ignore TE

  static inline void set_ieee_data (uint8_t data)
  {
    IEEE_D_DDR = data;
    IEEE_D_PORT = (uint8_t) ~ data;
  }

  static inline void ieee_bus_idle (void)
  {
    IEEE_D_DDR = 0;                 // Data ports as input
    IEEE_D_PORT = 0xff;             // Enable pull-ups for data lines
    IEEE_DDR_DAV  &= (uint8_t) ~_BV(IEEE_PIN_DAV);  // DAV as input
    IEEE_DDR_EOI  &= (uint8_t) ~_BV(IEEE_PIN_EOI);  // EOI as input
    IEEE_DDR_NDAC &= (uint8_t) ~_BV(IEEE_PIN_NDAC); // NDAC as input
    IEEE_DDR_NRFD &= (uint8_t) ~_BV(IEEE_PIN_NRFD); // NRFD as input
    IEEE_PORT_DAV |= _BV(IEEE_PIN_DAV);             // Enable pull-up for DAV
    IEEE_PORT_EOI |= _BV(IEEE_PIN_EOI);             // Enable pull-up for EOI
    IEEE_PORT_NDAC |= _BV(IEEE_PIN_NDAC);           // Enable pull-up for NDAC
    IEEE_PORT_NRFD |= _BV(IEEE_PIN_NRFD);           // Enable pull-up for NRFD
  }

  static void ieee_ports_listen (void) {
    ieee_bus_idle();
  }

  static void ieee_ports_talk (void) {
    ieee_bus_idle();
  }

# define ddr_change_by_atn() do { } while (0)

#endif  /* HAVE_7516X */

/* Init IEEE bus */
void ieee_init(void) {
  ieee_bus_idle();

  /* Prepare IEEE interrupts */
  ieee_interrupts_init();

  /* Read the hardware-set device address */
  device_hw_address_init();
  delay_ms(1);
  device_address = device_hw_address();

  /* Init vars and flags */
  command_length = 0;
  ieee_data.ieeeflags &= (uint8_t) ~ (COMMAND_RECVD || EOI_RECVD);
}
void bus_init(void) __attribute__((weak, alias("ieee_init")));

/* Interrupt routine that simulates the hardware-auto-acknowledge of ATN
   at falling edge of ATN. If pin change interrupts are used, we have to
   check for rising or falling edge in software first! */
IEEE_ATN_HANDLER {
#ifdef IEEE_PCMSK
  if(!IEEE_ATN) {
#else
  {
#endif
    ddr_change_by_atn();        /* Switch NDAC+NRFD to outputs */
    set_ndac_state(0);          /* Poll NDAC and NRFD low */
    set_nrfd_state(0);
  }
}

/* ------------------------------------------------------------------------- */
/*  Byte transfer routines                                                   */
/* ------------------------------------------------------------------------- */


/**
 * ieee_getc - receive one byte from the IEEE-488 bus
 *
 * This function tries receives one byte from the IEEE-488 bus and returns it
 * if successful. Flags (EOI, ATN) are passed in the more significant byte.
 * Returns TIMEOUT_ABORT if a timeout occured
 */

int ieee_getc(void) {
  int c = 0;

  /* PET waits for NRFD high */
  set_ndac_state(0);            /* data not yet accepted */
  set_nrfd_state(1);            /* ready for new data */

  /* Wait for DAV low, check timeout */
  timeout = getticks() + MS_TO_TICKS(IEEE_TIMEOUT_MS);
  do {                          /* wait for data valid */
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (IEEE_DAV);

  set_nrfd_state(0);    /* not ready for new data, data not yet read */

  c = (uint8_t) ~ IEEE_D_PIN;   /* read data */
  if(!IEEE_EOI) c |= FLAG_EOI;  /* end of transmission? */
  if(!IEEE_ATN) c |= FLAG_ATN;  /* data or command? */

  set_ndac_state(1);            /* data accepted, read complete */

  /* Wait for DAV high, check timeout */
  timeout = getticks() + MS_TO_TICKS(IEEE_TIMEOUT_MS);
  do {              /* wait for controller to remove data from bus */
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (!IEEE_DAV);
  set_ndac_state(0);            /* next data not yet accepted */

  return c;
}


/**
 * ieee_putc - send a byte
 * @data    : byte to be sent
 * @with_eoi: Flags if the byte should be send with an EOI condition
 *
 * This function sends the byte data over the IEEE-488 bus and pulls
 * EOI if it is the last byte.
 * Returns
 *  0 normally,
 * ATN_POLLED if ATN was set or
 * TIMEOUT_ABORT if a timeout occured
 * On negative returns, the caller should return to the IEEE main loop.
 */

static uint8_t ieee_putc(uint8_t data, const uint8_t with_eoi) {
  ieee_ports_talk();
  set_eoi_state (!with_eoi);
  set_ieee_data (data);
  if(!IEEE_ATN) return ATN_POLLED;
  _delay_us(11);    /* Allow data to settle */
  if(!IEEE_ATN) return ATN_POLLED;

  /* Wait for NRFD high , check timeout */
  timeout = getticks() + MS_TO_TICKS(IEEE_TIMEOUT_MS);
  do {
    if(!IEEE_ATN) return ATN_POLLED;
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (!IEEE_NRFD);
  set_dav_state(0);

  /* Wait for NRFD low, check timeout */
  timeout = getticks() + MS_TO_TICKS(IEEE_TIMEOUT_MS);
  do {
    if(!IEEE_ATN) return ATN_POLLED;
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (IEEE_NRFD);

  /* Wait for NDAC high , check timeout */
  timeout = getticks() + MS_TO_TICKS(IEEE_TIMEOUT_MS);
  do {
    if(!IEEE_ATN) return ATN_POLLED;
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (!IEEE_NDAC);
  set_dav_state(1);
  return 0;
}

/* ------------------------------------------------------------------------- */
/*  Listen+Talk-Handling                                                     */
/* ------------------------------------------------------------------------- */

static int16_t ieee_listen_handler (uint8_t cmd)
/* Receive characters from IEEE-bus and write them to the
   listen buffer adressed by ieee_data.secondary_address.
   If a new command is received (ATN set), return it
*/
{
  buffer_t *buf;
  int16_t c;

  ieee_data.secondary_address = cmd & 0x0f;
  buf = find_buffer(ieee_data.secondary_address);

  /* Abort if there is no buffer or it's not open for writing */
  /* and it isn't an OPEN command                             */
  if ((buf == NULL || !buf->write) && (cmd & 0xf0) != 0xf0) {
    uart_putc('c');
    return -1;
  }

  switch(cmd & 0xf0) {
    case 0x60:
      uart_puts_p("DATA L ");
      break;
    case 0xf0:
      uart_puts_p("OPEN ");
      break;
    default:
      uart_puts_p("Unknown LH! ");
      break;
  }
  uart_puthex(ieee_data.secondary_address);
  uart_putcrlf();

  c = -1;
  for(;;) {
    /* Get a character ignoring timeout but but watching ATN */
    while((c = ieee_getc()) < 0);
    if (c  & FLAG_ATN) return c;

    uart_putc('<');
    if (c & FLAG_EOI) {
      uart_puts_p("EOI ");
      ieee_data.ieeeflags |= EOI_RECVD;
    } else ieee_data.ieeeflags &= ~EOI_RECVD;

    uart_puthex(c); uart_putc(' ');
    c &= 0xff; /* needed for isprint */
    if(isprint(c)) uart_putc(c); else uart_putc('?');
    uart_putcrlf();

    if((cmd & 0x0f) == 0x0f || (cmd & 0xf0) == 0xf0) {
      if (command_length < CONFIG_COMMAND_BUFFER_SIZE)
        command_buffer[command_length++] = c;
      if (ieee_data.ieeeflags & EOI_RECVD)
        /* Filenames are just a special type of command =) */
        ieee_data.ieeeflags |= COMMAND_RECVD;
    } else {
      /* Flush buffer if full */
      if (buf->mustflush) {
        if (buf->refill(buf)) return -2;
        /* Search the buffer again,                     */
        /* it can change when using large buffers       */
        buf = find_buffer(ieee_data.secondary_address);
      }

      buf->data[buf->position] = c;
      mark_buffer_dirty(buf);

      if (buf->lastused < buf->position) buf->lastused = buf->position;
      buf->position++;

      /* Mark buffer for flushing if position wrapped */
      if (buf->position == 0) buf->mustflush = 1;

      /* REL files must be syncronized on EOI */
      if(buf->recordlen && (ieee_data.ieeeflags & EOI_RECVD)) {
        if (buf->refill(buf)) return -2;
      }
    }   /* else-buffer */
  }     /* for(;;) */
}

static uint8_t ieee_talk_handler (void)
{
  buffer_t *buf;
  uint8_t finalbyte;
  uint8_t c;
  uint8_t res;

  buf = find_buffer(ieee_data.secondary_address);
  if(buf == NULL) return -1;

  while (buf->read) {
    do {
      finalbyte = (buf->position == buf->lastused);
      c = buf->data[buf->position];
      if (finalbyte && buf->sendeoi) {
        /* Send with EOI */
        res = ieee_putc(c, 1);
        if(!res) uart_puts_p("EOI: ");
      } else {
        /* Send without EOI */
        res = ieee_putc(c, 0);
      }
      if(res) {
        if(res==0xfc) {
          uart_puts_P(PSTR("*** TIMEOUT ABORT***")); uart_putcrlf();
        }
        if(res!=0xfd) {
          uart_putc('c'); uart_puthex(res);
        }
        return 1;
      } else {
        uart_putc('>');
        uart_puthex(c); uart_putc(' ');
        if(isprint(c)) uart_putc(c); else uart_putc('?');
        uart_putcrlf();
      }
    } while (buf->position++ < buf->lastused);

    if(buf->sendeoi && ieee_data.secondary_address != 0x0f &&
      !buf->recordlen && buf->refill != directbuffer_refill) {
      buf->read = 0;
      break;
    }

    if (buf->refill(buf)) {
      return -1;
    }

    /* Search the buffer again, it can change when using large buffers */
    buf = find_buffer(ieee_data.secondary_address);
  }
  return 0;
}

static void cmd_handler (void)
{
  /* Handle commands and filenames */
  if (ieee_data.ieeeflags & COMMAND_RECVD) {
# ifdef HAVE_HOTPLUG
    /* This seems to be a nice point to handle card changes */
    if (disk_state != DISK_OK) {
      set_busy_led(1);
      /* If the disk was changed the buffer contents are useless */
      if (disk_state == DISK_CHANGED || disk_state == DISK_REMOVED) {
        free_multiple_buffers(FMB_ALL);
        change_init();
        filesystem_init(0);
      } else {
        /* Disk state indicated an error, try to recover by initialising */
        filesystem_init(1);
      }
      update_leds();
    }
# endif
    if (ieee_data.secondary_address == 0x0f) {
      parse_doscommand();                   /* Command channel */
    } else {
      datacrc = 0xffff;                     /* Filename in command buffer */
      file_open(ieee_data.secondary_address);
    }
    command_length = 0;
    ieee_data.ieeeflags &= (uint8_t) ~COMMAND_RECVD;
  } /* COMMAND_RECVD */

  /* We're done, clean up unused buffers */
  free_multiple_buffers(FMB_UNSTICKY);
  d64_bam_commit();
}

/* ------------------------------------------------------------------------- */
/*  Main loop                                                                */
/* ------------------------------------------------------------------------- */

void ieee_mainloop(void) {
  int16_t cmd = 0;

  set_error(ERROR_DOSVERSION);

  ieee_data.bus_state = BUS_IDLE;
  ieee_data.device_state = DEVICE_IDLE;
  for(;;) {
    switch(ieee_data.bus_state) {
      case BUS_SLEEP:                               /* BUS_SLEEP */
        set_atn_irq(0);
        ieee_bus_idle();
        set_error(ERROR_OK);
        set_busy_led(0);
        uart_puts_P(PSTR("ieee.c/sleep ")); set_dirty_led(1);

        /* Wait until the sleep key is used again */
        while (!key_pressed(KEY_SLEEP))
          system_sleep();
        reset_key(KEY_SLEEP);

        set_atn_irq(1);
        update_leds();

        ieee_data.bus_state = BUS_IDLE;
        break;

      case BUS_IDLE:                                /* BUS_IDLE */
        ieee_bus_idle();
        while(IEEE_ATN) {   ;               /* wait for ATN */
          if (key_pressed(KEY_NEXT | KEY_PREV | KEY_HOME)) {
            change_disk();
          } else if (key_pressed(KEY_SLEEP)) {
            reset_key(KEY_SLEEP);
            ieee_data.bus_state = BUS_SLEEP;
            break;
          } else if (display_found && key_pressed(KEY_DISPLAY)) {
            display_service();
            reset_key(KEY_DISPLAY);
          }
          system_sleep();
      }

      if (ieee_data.bus_state != BUS_SLEEP)
        ieee_data.bus_state = BUS_FOUNDATN;
      break;

      case BUS_FOUNDATN:                            /* BUS_FOUNDATN */
        ieee_data.bus_state = BUS_ATNPROCESS;
        cmd = ieee_getc();
      break;

      case BUS_ATNPROCESS:                          /* BUS_ATNPROCESS */
        if(cmd < 0) {
          uart_putc('c');
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else cmd &= 0xFF;
        uart_puts_p("ATN "); uart_puthex(cmd);
        uart_putcrlf();

        if (cmd == 0x3f) {                                  /* UNLISTEN */
          if(ieee_data.device_state == DEVICE_LISTEN) {
            ieee_data.device_state = DEVICE_IDLE;
            uart_puts_p("UNLISTEN\r\n");
          }
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if (cmd == 0x5f) {                           /* UNTALK */
          if(ieee_data.device_state == DEVICE_TALK) {
            ieee_data.device_state = DEVICE_IDLE;
            uart_puts_p("UNTALK\r\n");
          }
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if (cmd == (0x40 + device_address)) {        /* TALK */
          uart_puts_p("TALK ");
          uart_puthex(device_address); uart_putcrlf();
          ieee_data.device_state = DEVICE_TALK;
          /* disk drives never talk immediatly after TALK, so stay idle
             and wait for a secondary address given by 0x60-0x6f DATA */
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if (cmd == (0x20 + device_address)) {        /* LISTEN */
          ieee_data.device_state = DEVICE_LISTEN;
          uart_puts_p("LISTEN ");
          uart_puthex(device_address); uart_putcrlf();
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if ((cmd & 0xf0) == 0x60) {                  /* DATA */
          /* 8250LP sends data while ATN is still active, so wait
             for bus controller to release ATN or we will misinterpret
             data as a command */
          while(!IEEE_ATN);
          if(ieee_data.device_state == DEVICE_LISTEN) {
            cmd = ieee_listen_handler(cmd);
            cmd_handler();
            break;
          } else if (ieee_data.device_state == DEVICE_TALK) {
            ieee_data.secondary_address = cmd & 0x0f;
            uart_puts_p("DATA T ");
            uart_puthex(ieee_data.secondary_address);
            uart_putcrlf();
            if(ieee_talk_handler() == TIMEOUT_ABORT) {
              ieee_data.device_state = DEVICE_IDLE;
            }
            ieee_data.bus_state = BUS_IDLE;
            break;
          } else {
            ieee_data.bus_state = BUS_IDLE;
            break;
          }
        } else if (ieee_data.device_state == DEVICE_IDLE) {
          ieee_data.bus_state = BUS_IDLE;
          break;
          /* ----- if we reach this, we're LISTENer or TALKer ----- */
        } else if ((cmd & 0xf0) == 0xe0) {                  /* CLOSE */
          ieee_data.secondary_address = cmd & 0x0f;
          uart_puts_p("CLOSE ");
          uart_puthex(ieee_data.secondary_address);
          uart_putcrlf();
          /* Close all buffers if sec. 15 is closed */
          if(ieee_data.secondary_address == 15) {
            free_multiple_buffers(FMB_USER_CLEAN);
          } else {
            /* Close a single buffer */
            buffer_t *buf;
            buf = find_buffer (ieee_data.secondary_address);
            if (buf != NULL) {
              buf->cleanup(buf);
              free_buffer(buf);
            }
          }
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if ((cmd & 0xf0) == 0xf0) {                  /* OPEN */
          cmd = ieee_listen_handler(cmd);
          cmd_handler();
          break;
        } else {
          /* Command for other device or unknown command */
          ieee_data.bus_state = BUS_IDLE;
        }
      break;
    }   /* switch   */
  }     /* for()    */
}
void bus_mainloop(void) __attribute__ ((weak, alias("ieee_mainloop")));
