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


   main.c: Lots of init calls for the submodules

*/

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "bus.h"
#include "diskchange.h"
#include "diskio.h"
#include "display.h"
#include "eeprom-conf.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "filesystem.h"
#include "i2c.h"
#include "led.h"
#include "time.h"
#include "rtc.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "uart.h"
#include "ustring.h"
#include "utils.h"


#if defined(__AVR__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1))
int main(void) __attribute__((OS_main));
#endif
int main(void) {
  /* Early system initialisation */
  board_init();
  system_init_early();
  leds_init();

  set_busy_led(1);
  set_dirty_led(0);

  /* Due to an erratum in the LPC17xx chips anything that may change */
  /* peripheral clock scalers must come before system_init_late()    */
  uart_init();
#ifndef SPI_LATE_INIT
  spi_init(SPI_SPEED_SLOW);
#endif
  timer_init();
  bus_interface_init();
  i2c_init();

  /* Second part of system initialisation, switches to full speed on ARM */
  system_init_late();
  enable_interrupts();

  /* Internal-only initialisation, called here because it's faster */
  buffers_init();
  buttons_init();

  /* Anything that does something which needs the system clock */
  /* should be placed after system_init_late() */
  bus_init();    // needs delay
  rtc_init();    // accesses I2C
  disk_init();   // accesses card
  read_configuration();

  filesystem_init(0);
  change_init();

  uart_puts_P(PSTR("\r\nsd2iec " VERSION " #"));
  uart_puthex(device_address);
  uart_putcrlf();

#ifdef CONFIG_REMOTE_DISPLAY
  /* at this point all buffers should be free, */
  /* so just use the data area of the first to build the string */
  uint8_t *strbuf = buffers[0].data;
  ustrcpy_P(strbuf, versionstr);
  ustrcpy_P(strbuf+ustrlen(strbuf), longverstr);
  if (display_init(ustrlen(strbuf), strbuf)) {
    display_address(device_address);
    display_current_part(0);
  }
#endif

  set_busy_led(0);

#if defined(HAVE_SD) && BUTTON_PREV != 0
  /* card switch diagnostic aid - hold down PREV button to use */
  if (!(buttons_read() & BUTTON_PREV)) {
    while (buttons_read() & BUTTON_NEXT) {
      set_dirty_led(sdcard_detect());
# ifndef SINGLE_LED
      set_busy_led(sdcard_wp());
# endif
    }
    reset_key(0xff);
  }
#endif

  bus_mainloop();

  while (1);
}
