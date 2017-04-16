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


   fileops.h: Definitions for file operations

*/

#ifndef FILEOPS_H
#define FILEOPS_H

#include "buffers.h"
#include "dirent.h"
#include "progmem.h"

enum open_modes { OPEN_READ, OPEN_WRITE, OPEN_APPEND, OPEN_MODIFY };
extern const PROGMEM uint8_t filetypes[];

/* saved dirent of the last opened file */
extern cbmdirent_t previous_file_dirent;

/* Refill-callback for large buffers, only used for comparision */
uint8_t directbuffer_refill(buffer_t *buf);

/* reopen the last opened file on secondary 0 */
void file_open_previous(void);

/* Parses a filename in command_buffer and opens that file */
void file_open(uint8_t secondary);

#endif
