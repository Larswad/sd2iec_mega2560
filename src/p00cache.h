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


   p00cache.h: Data structures for the [PSUR]00 name cache

*/

#ifndef P00CACHE_H
#define P00CACHE_H

#include <stdint.h>

#ifdef CONFIG_P00CACHE

void     p00cache_invalidate(void);
uint8_t *p00cache_lookup(uint8_t part, uint32_t cluster);
void     p00cache_add(uint8_t part, uint32_t cluster, uint8_t *name);

#else

#  define p00cache_invalidate() do {} while (0)
#  define p00cache_lookup(p,c)  NULL
#  define p00cache_add(p,c,n)   do {} while (0)

#endif

#endif
