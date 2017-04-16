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


   fastloader-ll.h: Definitions for low-level fastloader routines

*/

#ifndef FASTLOADERLL_H
#define FASTLOADERLL_H

#ifdef CONFIG_HAVE_IEC
void turbodisk_byte(uint8_t value);
void turbodisk_buffer(uint8_t *data, uint8_t length);

uint8_t jiffy_receive(iec_bus_t *busstate);
uint8_t jiffy_send(uint8_t value, uint8_t eoi, uint8_t loadflags);

void clk_data_handshake(void);
void fastloader_fc3_send_block(uint8_t *data);
uint8_t fc3_get_byte(void);
uint8_t fc3_oldfreeze_pal_send(uint8_t byte);
uint8_t fc3_oldfreeze_ntsc_send(uint8_t byte);

uint8_t dreamload_get_byte(void);
void dreamload_send_byte(uint8_t byte);

int16_t uload3_get_byte(void);
void uload3_send_byte(uint8_t byte);

uint8_t epyxcart_send_byte(uint8_t byte);

uint8_t geos_get_byte_1mhz(void);
uint8_t geos_get_byte_2mhz(void);
uint8_t geos_send_byte_1mhz(uint8_t byte);
uint8_t geos_send_byte_2mhz(uint8_t byte);
uint8_t geos_send_byte_1581_21(uint8_t byte);

uint8_t wheels_send_byte_1mhz(uint8_t byte);
uint8_t wheels_get_byte_1mhz(void);

uint8_t wheels44_get_byte_1mhz(void);
uint8_t wheels44_get_byte_2mhz(void);
uint8_t wheels44_send_byte_2mhz(uint8_t byte);

void ar6_1581_send_byte(uint8_t byte);
uint8_t ar6_1581p_get_byte(void);

void n0sdos_send_byte(uint8_t byte);

typedef enum { PARALLEL_DIR_IN = 0,
               PARALLEL_DIR_OUT } parallel_dir_t;

uint8_t parallel_read(void);
void parallel_write(uint8_t value);
void parallel_send_handshake(void);

#ifdef HAVE_PARALLEL
void parallel_set_dir(parallel_dir_t direction);
#else
# define parallel_set_dir(x) do {} while (0)
#endif

#endif

#endif
