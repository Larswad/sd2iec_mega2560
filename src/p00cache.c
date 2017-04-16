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


   p00cache.c: [PSUR]00 name cache

*/

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "dirent.h"
#include "p00cache.h"

#include "uart.h"

typedef struct {
  uint32_t cluster;
  uint8_t  name[CBM_NAME_LENGTH];
} p00name_t;

static P00CACHE_ATTRIB p00name_t p00cache[CONFIG_P00CACHE_SIZE / sizeof(p00name_t)];
static uint8_t      cache_part;
static unsigned int entries;

void p00cache_invalidate(void) {
  cache_part = -1;
  entries    = 0;
}

uint8_t *p00cache_lookup(uint8_t part, uint32_t cluster) {
  /* fail if it's for a different partition */
  if (part != cache_part)
    return NULL;

  /* linear search for the correct cluster number */
  /* (binary search was only 6-8% faster overall) */
  for (unsigned int i=0; i<entries; i++) {
    if (p00cache[i].cluster == cluster)
      return p00cache[i].name;
  }

  /* noting found */
  return NULL;
}

void p00cache_add(uint8_t part, uint32_t cluster, uint8_t *name) {
  /* don't add if the cache is full */
  if (entries == sizeof(p00cache)/sizeof(p00cache[0]))
    return;

  /* invalidate if it's for a different partition */
  // FIXME: ignore instead and add invalidate to CP?
  if (part != cache_part) {
    p00cache_invalidate();
    cache_part = part;
  }

  /* add entry at end of list */
  p00cache[entries].cluster = cluster;
  memcpy(p00cache[entries].name, name, CBM_NAME_LENGTH);
  entries++;
}
