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


   config.h: The main configuration header file which actually doesn't have
             any configuration options in it anymore.

*/

#ifndef CONFIG_H
#define CONFIG_H

/* disable assertions */
#ifndef CONFIG_ASSERTIONS
#  define NDEBUG
#endif

#include "autoconf.h"
#include "arch-config.h"

/* Disable COMMAND_CHANNEL_DUMP if UART_DEBUG is disabled */
#ifndef CONFIG_UART_DEBUG
#  undef CONFIG_COMMAND_CHANNEL_DUMP
#endif

/* An interrupt for detecting card changes implies hotplugging capability */
#if defined(SD_CHANGE_HANDLER) || defined (CF_CHANGE_HANDLER)
#  define HAVE_HOTPLUG
#endif

/* Generate a dummy function if there is no board-specific initialisation */
#ifndef HAVE_BOARD_INIT
static inline void board_init(void) {
  return;
}
#endif

/* ----- Translate CONFIG_ADD symbols to HAVE symbols ----- */
/* By using two symbols for this purpose it's easier to determine if */
/* support was enabled by default or added in the config file.       */
#if defined(CONFIG_ADD_SD) && !defined(HAVE_SD)
#  define HAVE_SD
#endif

#if defined(CONFIG_ADD_ATA) && !defined(HAVE_ATA)
#  define HAVE_ATA
#endif

/* Enable the diskmux if more than one storage device is enabled. */
#if !defined(NEED_DISKMUX) && (defined(HAVE_SD) + defined(HAVE_ATA)) > 1
#  define NEED_DISKMUX
#endif

/* Hardcoded maximum - reducing this won't save any ram */
#define MAX_DRIVES 8

/* SD access LED dummy */
#ifndef HAVE_SD_LED
# define set_sd_led(x) do {} while (0)
#endif

/* Sanity checks */
#if defined(CONFIG_LOADER_WHEELS) && !defined(CONFIG_LOADER_GEOS)
#  error "CONFIG_LOADER_GEOS must be enabled for Wheels support!"
#endif

#if defined(CONFIG_PARALLEL_DOLPHIN)
#  if !defined(HAVE_PARALLEL)
#    error "CONFIG_PARALLEL_DOLPHIN enabled on a hardware without parallel port!"
#  else
#    define PARALLEL_ENABLED
#  endif
#endif

/* ----- Translate CONFIG_RTC_* symbols to HAVE_RTC symbol ----- */
#if defined(CONFIG_RTC_SOFTWARE) || \
    defined(CONFIG_RTC_PCF8583)  || \
    defined(CONFIG_RTC_LPC17XX)  || \
    defined(CONFIG_RTC_DSRTC)
#  define HAVE_RTC

/* calculate the number of enabled RTCs */
#  if defined(CONFIG_RTC_SOFTWARE) + \
      defined(CONFIG_RTC_PCF8583)  + \
      defined(CONFIG_RTC_LPC17XX)  + \
      defined(CONFIG_RTC_DSRTC)  > 1
#    define NEED_RTCMUX
#  endif
#endif

#endif
