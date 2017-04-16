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


   iec-bus.c: Architecture-specific IEC bus initialisation

   This is not in arch-config.h becaue using the set_* functions
   from iec-bus.h simplifies the code and the ARM version isn't
   space-constrained yet.
*/

#include "config.h"
#include "iec-bus.h"

void iec_interface_init(void) {
  /* Set up outputs before switching the pins */
  set_atn(1);
  set_data(1);
  set_clock(1);
  set_srq(1);

  iec_pins_connect();
  parallel_init();
}
void bus_interface_init(void) __attribute__ ((weak, alias("iec_interface_init")));
