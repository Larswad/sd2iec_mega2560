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


   iec-bus.h: A few wrappers around the port definitions

*/

#ifndef IEC_BUS_H
#define IEC_BUS_H

/* output functions are defined in arch-config.h */

/*** Input definitions (generic versions) ***/
#ifndef IEC_ATN
#  define IEC_ATN   (IEC_INPUT & IEC_BIT_ATN)
#  define IEC_DATA  (IEC_INPUT & IEC_BIT_DATA)
#  define IEC_CLOCK (IEC_INPUT & IEC_BIT_CLOCK)
#  define IEC_SRQ   (IEC_INPUT & IEC_BIT_SRQ)
#endif

#ifdef IEC_INPUTS_INVERTED
static inline iec_bus_t iec_bus_read(void) {
  return (~IEC_INPUT) & (IEC_BIT_ATN | IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_SRQ);
}
#else
static inline iec_bus_t iec_bus_read(void) {
  return IEC_INPUT & (IEC_BIT_ATN | IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_SRQ);
}
#endif

void iec_interface_init(void);

#endif
