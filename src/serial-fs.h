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


	 serial-fs.h: Host file system exposed over serial wire (Desktop PC, Mobile, Tablet...).

*/

#ifndef SERIALFS_H
#define SERIALFS_H

#include <stdint.h>
#include <stdbool.h>

// NOTE: Not tested with anything but 16
#define SFS_NAME_LENGTH 16

/* flags for serialfs_open */
#define SFS_MODE_READ   0
#define SFS_MODE_WRITE  1
#define SFS_MODE_APPEND 2

/* error return values */
typedef enum {
	SFS_ERROR_OK = 0,
	SFS_ERROR_FILENOTFOUND,
	SFS_ERROR_FILEEXISTS,
	SFS_ERROR_DIRFULL,
	SFS_ERROR_DISKFULL,
	SFS_ERROR_INVALID,
	SFS_ERROR_UNIMPLEMENTED,
} sfs_error_t;

typedef struct {
	uint8_t entry;
} sfs_dir_t;

typedef struct {
	uint16_t size;
	uint8_t  name[SFS_NAME_LENGTH+1]; // note: last byte is not touched
	uint8_t  flags;
} sfs_dirent_t;

typedef struct {
	uint16_t size;        // current file size
	uint16_t cur_offset;  // current offset within file
	uint8_t  filemode;    // read/write/append - FIXME: read-only?
} sfs_fh_t;

bool serialfs_init(void);
void serialfs_format(void);
uint8_t serialfs_free_sectors(void);
void serialfs_opendir(sfs_dir_t *dh);
uint8_t serialfs_readdir(sfs_dir_t *dh, sfs_dirent_t *entry);
sfs_error_t serialfs_open(uint8_t *name, sfs_fh_t *fh, uint8_t flags);
sfs_error_t serialfs_write(sfs_fh_t *fh, void *data, uint16_t length, uint16_t *bytes_written);
sfs_error_t serialfs_read(sfs_fh_t *fh, void *data, uint16_t length, uint16_t *bytes_read);
void serialfs_close(sfs_fh_t *fh);
sfs_error_t serialfs_rename(uint8_t *oldname, uint8_t *newname);
sfs_error_t serialfs_delete(uint8_t *name);

#endif // SERIALFS_H
