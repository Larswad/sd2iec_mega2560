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


   led.c: Overdesigned LED handling

*/

#include "config.h"
#include "buffers.h"
#include "led.h"

volatile uint8_t led_state;

/* Notice: leds_init() is in config.h because it's hardware-specific */

/**
 * update_leds - set LEDs to correspond to the buffer status
 *
 * This function sets the busy/dirty LEDs to correspond to the current state
 * of the buffers, i.e. busy on of at least one non-system buffer is
 * allocated and dirty on if at least one buffer is dirty.
 * Call if you have manually changed the LEDs and you want to restore the
 * "default" state.
 */
void update_leds(void) {
  set_busy_led(active_buffers != 0);
  set_dirty_led(get_dirty_buffer_count() != 0);
}
