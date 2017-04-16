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


   fastloader.h: Definitions for the high level fast loader handling

*/

#ifndef FASTLOADER_H
#define FASTLOADER_H

/* these two values are needed in the assembler implementation for AVR */
#define FLCODE_DREAMLOAD     1
#define FLCODE_DREAMLOAD_OLD 2

#ifndef __ASSEMBLER__

#define UNUSED_PARAMETER uint8_t __attribute__((unused)) unused__

typedef enum {
  FL_NONE          = 0,
  FL_DREAMLOAD     = FLCODE_DREAMLOAD,
  FL_DREAMLOAD_OLD = FLCODE_DREAMLOAD_OLD,
  FL_TURBODISK,
  FL_FC3_LOAD,
  FL_FC3_SAVE,
  FL_FC3_FREEZED,
  FL_ULOAD3,
  FL_GI_JOE,
  FL_EPYXCART,
  FL_GEOS_S1_64,
  FL_GEOS_S1_128,
  FL_GEOS_S23_1541,
  FL_GEOS_S23_1571,
  FL_GEOS_S23_1581,
  FL_WHEELS_S1_64,
  FL_WHEELS_S1_128,
  FL_WHEELS_S2,
  FL_WHEELS44_S2,
  FL_WHEELS44_S2_1581,
  FL_NIPPON,
  FL_AR6_1581_LOAD,
  FL_AR6_1581_SAVE,
  FL_ELOAD1,
  FL_FC3_OLDFREEZED,
  FL_MMZAK,
  FL_N0SDOS_FILEREAD
} fastloaderid_t;

extern fastloaderid_t detected_loader;
extern volatile uint8_t fl_track;
extern volatile uint8_t fl_sector;
extern uint8_t (*fast_send_byte)(uint8_t byte);
extern uint8_t (*fast_get_byte)(void);

uint8_t check_keys(void);

/* per-loader functions, located in separate fl-*.c files */
void load_turbodisk(uint8_t);
void load_fc3(uint8_t freezed);
void load_fc3oldfreeze(uint8_t);
void save_fc3(uint8_t);
void load_dreamload(uint8_t);
void load_uload3(uint8_t);
void load_eload1(uint8_t);
void load_gijoe(uint8_t);
void load_epyxcart(uint8_t);
void load_geos(uint8_t);
void load_geos_s1(uint8_t version);
void load_wheels_s1(uint8_t version);
void load_wheels_s2(uint8_t);
void load_nippon(uint8_t);
void load_ar6_1581(uint8_t);
void save_ar6_1581(uint8_t);
void load_mmzak(uint8_t);
void load_n0sdos_fileread(uint8_t);

int16_t dolphin_getc(void);
uint8_t dolphin_putc(uint8_t data, uint8_t with_eoi);
void load_dolphin(void);
void save_dolphin(void);

/* functions that are shared between multiple loaders */
/* currently located in fastloader.c                  */
int16_t gijoe_read_byte(void);

# ifdef PARALLEL_ENABLED
extern volatile uint8_t parallel_rxflag;
static inline void parallel_clear_rxflag(void) { parallel_rxflag = 0; }
# else
#  define parallel_rxflag 0
static inline void parallel_clear_rxflag(void) {}
# endif

#endif // not assembler
#endif
