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


   llfl-geos.c: Low level handling of GEOS/Wheels loaders

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "iec-bus.h"
#include "llfl-common.h"
#include "timer.h"
#include "fastloader-ll.h"


/* ------------ */
/* --- GEOS --- */
/* ------------ */

/* --- reception --- */
static const generic_2bit_t geos_1mhz_get_def = {
  .pairtimes = {150, 290, 430, 590},
  .clockbits = {4, 6, 3, 2},
  .databits  = {5, 7, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t geos_2mhz_get_def = {
  .pairtimes = {150, 290, 395, 505},
  .clockbits = {4, 6, 3, 2},
  .databits  = {5, 7, 1, 0},
  .eorvalue  = 0xff
};

static uint8_t geos_get_generic(const generic_2bit_t *timingdef, uint8_t holdtime_us) {
  uint8_t result;

  llfl_setup();

  /* initial handshake */
  /* wait_* expects edges, so waiting for clock high isn't needed */
  llfl_wait_clock(0, NO_ATNABORT);

  /* receive data */
  result = llfl_generic_save_2bit(timingdef);
  delay_us(holdtime_us);

  llfl_teardown();
  return result;
}

uint8_t geos_get_byte_1mhz(void) {
  return geos_get_generic(&geos_1mhz_get_def, 12);
}

uint8_t geos_get_byte_2mhz(void) {
  return geos_get_generic(&geos_2mhz_get_def, 12);
}

/* --- transmission --- */
static const generic_2bit_t geos_1mhz_send_def = {
  .pairtimes = {180, 280, 390, 510},
  .clockbits = {3, 2, 4, 6},
  .databits  = {1, 0, 5, 7},
  .eorvalue  = 0x0f
};

static const generic_2bit_t geos_2mhz_send_def = {
  .pairtimes = {90, 200, 320, 440},
  .clockbits = {3, 2, 4, 6},
  .databits  = {1, 0, 5, 7},
  .eorvalue  = 0x0f
};

static const generic_2bit_t geos_1581_21_send_def = {
  .pairtimes = {70, 140, 240, 330},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

static void geos_send_generic(uint8_t byte, const generic_2bit_t *timingdef, unsigned int holdtime_us) {
  llfl_setup();

  /* initial handshake */
  set_clock(1);
  set_data(1);
  llfl_wait_clock(0, NO_ATNABORT);

  /* send data */
  llfl_generic_load_2bit(timingdef, byte);

  /* hold time */
  delay_us(holdtime_us);

  llfl_teardown();
}

uint8_t geos_send_byte_1mhz(uint8_t byte) {
  geos_send_generic(byte, &geos_1mhz_send_def, 19);
  return 0;
}

uint8_t geos_send_byte_2mhz(uint8_t byte) {
  geos_send_generic(byte, &geos_2mhz_send_def, 22);
  return 0;
}

uint8_t geos_send_byte_1581_21(uint8_t byte) {
  geos_send_generic(byte, &geos_1581_21_send_def, 12);
  return 0;
}


/* -------------- */
/* --- Wheels --- */
/* -------------- */

/* --- reception --- */
static const generic_2bit_t wheels_1mhz_get_def = {
  .pairtimes = {160, 260, 410, 540},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t wheels44_1mhz_get_def = {
  .pairtimes = {170, 280, 450, 610},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t wheels44_2mhz_get_def = {
  .pairtimes = {150, 260, 370, 480},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0xff
};

uint8_t wheels_get_byte_1mhz(void) {
  return geos_get_generic(&wheels_1mhz_get_def, 20);
}

uint8_t wheels44_get_byte_1mhz(void) {
  return geos_get_generic(&wheels44_1mhz_get_def, 20);
}

uint8_t wheels44_get_byte_2mhz(void) {
  return geos_get_generic(&wheels44_2mhz_get_def, 12);
}

/* --- transmission --- */
static const generic_2bit_t wheels_1mhz_send_def = {
  .pairtimes = {90, 230, 370, 510},
  .clockbits = {3, 2, 7, 6},
  .databits  = {1, 0, 5, 4},
  .eorvalue  = 0xff
};

static const generic_2bit_t wheels44_2mhz_send_def = {
  .pairtimes = {70, 150, 260, 370},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

uint8_t wheels_send_byte_1mhz(uint8_t byte) {
  geos_send_generic(byte, &wheels_1mhz_send_def, 22);
  return 0;
}

uint8_t wheels44_send_byte_2mhz(uint8_t byte) {
  geos_send_generic(byte, &wheels44_2mhz_send_def, 15);
  return 0;
}
