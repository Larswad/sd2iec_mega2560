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


   eeprom-fs.h: Tiny file system in the unused EEPROM space

*/

#ifndef EEPROM_FS_H
#define EEPROM_FS_H

#include <stdint.h>

// NOTE: Not tested with anything but 16
#define EEFS_NAME_LENGTH 16

/* flags for eepromfs_open */
#define EEFS_MODE_READ   0
#define EEFS_MODE_WRITE  1
#define EEFS_MODE_APPEND 2

/* error return values */
typedef enum {
  EEFS_ERROR_OK = 0,
  EEFS_ERROR_FILENOTFOUND,
  EEFS_ERROR_FILEEXISTS,
  EEFS_ERROR_DIRFULL,
  EEFS_ERROR_DISKFULL,
  EEFS_ERROR_INVALID,
  EEFS_ERROR_UNIMPLEMENTED,
} eefs_error_t;

typedef struct {
  uint8_t entry;
} eefs_dir_t;

typedef struct {
  uint16_t size;
  uint8_t  name[EEFS_NAME_LENGTH+1]; // note: last byte is not touched
  uint8_t  flags;
} eefs_dirent_t;

typedef struct {
  uint16_t size;        // current file size
  uint16_t cur_offset;  // current offset within file
  uint8_t  entry;       // (first) directory entry of the file
  uint8_t  cur_sector;  // current sector
  uint8_t  cur_soffset; // current offset in the sector, points "next" byte on read+write
  uint8_t  cur_entry;   // index of the current directory entry
  uint8_t  cur_sindex;  // index of the current sector in the entry
  uint8_t  filemode;    // read/write/append - FIXME: read-only?
} eefs_fh_t;

void         eepromfs_init(void);
void         eepromfs_format(void);
uint8_t      eepromfs_free_sectors(void);
void         eepromfs_opendir(eefs_dir_t *dh);
uint8_t      eepromfs_readdir(eefs_dir_t *dh, eefs_dirent_t *entry);
eefs_error_t eepromfs_open(uint8_t *name, eefs_fh_t *fh, uint8_t flags);
eefs_error_t eepromfs_write(eefs_fh_t *fh, void *data, uint16_t length, uint16_t *bytes_written);
eefs_error_t eepromfs_read(eefs_fh_t *fh, void *data, uint16_t length, uint16_t *bytes_read);
void         eepromfs_close(eefs_fh_t *fh);
eefs_error_t eepromfs_rename(uint8_t *oldname, uint8_t *newname);
eefs_error_t eepromfs_delete(uint8_t *name);

#endif
