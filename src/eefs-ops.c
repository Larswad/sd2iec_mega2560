#pragma GCC diagnostic warning "-Wunused-function"

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


	 eefs-ops.c: eepromfs operations

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "eeprom-fs.h"
#include "errormsg.h"
#include "fatops.h"
#include "led.h"
#include "parser.h"
#include "ustring.h"
#include "eefs-ops.h"
#include "ops_common.h"

/* ------------------------------------------------------------------------- */
/*  data structures, constants, global variables                             */
/* ------------------------------------------------------------------------- */

//                                           1234567890123456
static const PROGMEM uint8_t disk_label[] = "EEPROMFS        ";
static const PROGMEM uint8_t disk_id[]    = "EE 2A";

uint8_t eefs_partition;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * translate_error - translate an EEFS error code into a Commodore error message
 * @res: EEFS error code to be translated
 *
 * This function sets the error channel according to the problem given in
 * @res.
 */
static void translate_error(eefs_error_t res) {
	switch (res) {
	case EEFS_ERROR_OK:
		set_error(ERROR_OK);
		break;

	case EEFS_ERROR_FILENOTFOUND:
		set_error(ERROR_FILE_NOT_FOUND);
		break;

	case EEFS_ERROR_FILEEXISTS:
		set_error(ERROR_FILE_EXISTS);
		break;

	case EEFS_ERROR_DIRFULL:
	case EEFS_ERROR_DISKFULL:
		set_error_ts(ERROR_DISK_FULL, res, 0);
		break;

	case EEFS_ERROR_INVALID:
		set_error(ERROR_SYNTAX_UNABLE);
		break;

	case EEFS_ERROR_UNIMPLEMENTED:
		set_error(ERROR_SYNTAX_UNKNOWN);
		break;

	default:
		set_error_ts(ERROR_SYNTAX_UNABLE, res, 99);
		break;
	}
}

/**
 * eefs_refill_read - refill-callback for reading
 * @buf: target buffer
 *
 * This is the refill-callback function for files opened for reading.
 */
static uint8_t eefs_refill_read(buffer_t *buf) {
	eefs_error_t res;
	uint16_t bytes_read;

	res = eepromfs_read(&buf->pvt.eefh, buf->data + 2, 254, &bytes_read);
	if (res != EEFS_ERROR_OK) {
		translate_error(res);
		free_buffer(buf);
		return 1;
	}

	buf->position = 2;
	buf->lastused = bytes_read + 1;

	/* check if the last byte of file is in the buffer */
	if (bytes_read < 254 ||
			buf->pvt.eefh.cur_offset == buf->pvt.eefh.size) {
		buf->sendeoi = 1;
	} else {
		buf->sendeoi = 0;
	}

	return 0;
}

/**
 * eefs_refill_write - refill-callback for writing
 * @buf: target buffer
 *
 * This is the refill-callback function for files opened for writing.
 */
static uint8_t eefs_refill_write(buffer_t *buf) {
	eefs_error_t res;
	uint16_t byteswritten;

	/* fix up lastused for incomplete blocks */
	if (!buf->mustflush)
		buf->lastused = buf->position - 1;

	res = eepromfs_write(&buf->pvt.eefh, buf->data + 2, buf->lastused - 1, &byteswritten);
	if (res != EEFS_ERROR_OK) { // eepromfs never returns OK if the write was incomplete
		translate_error(res);
		eepromfs_close(&buf->pvt.eefh);
		free_buffer(buf);
		return 1;
	}

	mark_buffer_clean(buf);
	buf->mustflush = 0;
	buf->position  = 2;
	buf->lastused  = 2;

	return 0;
}

// note: no cleanup-callback for reading

/**
 * eefs_cleanup_write - cleanup-callback for writing
 * @buf: target buffer
 *
 * This is the cleanup-callback function for files opened for writing.
 */
static uint8_t eefs_cleanup_write(buffer_t *buf) {
	if (!buf->allocated)
		return 0;

	/* write remaining data */
	if (buf->refill(buf))
		return 1;

	eepromfs_close(&buf->pvt.eefh);
	buf->cleanup = callback_dummy;
	return 0;
}

/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

void eefsops_init(void) {
	eefs_partition = 255;

	/* do not add if all partitions are already in use */
	if (max_part >= CONFIG_MAX_PARTITIONS)
		return;

	eefs_partition = max_part;
	partition[eefs_partition].fop = &eefs_ops;
	max_part++;

	eepromfs_init();
}

static void eefs_open_read(path_t *path, cbmdirent_t *dent, buffer_t *buf) {
	eefs_error_t res;

	repad_filename(dent->name);

	res = eepromfs_open(dent->name, &buf->pvt.eefh, EEFS_MODE_READ);
	translate_error(res);

	if (res != EEFS_ERROR_OK)
		return;

	/* set up the buffer */
	buf->read   = 1;
	buf->refill = eefs_refill_read;
	// no cleanup/close function needed for read-only files
	stick_buffer(buf);

	buf->refill(buf);
}

static void eefs_open_write(path_t *path, cbmdirent_t *dent, uint8_t type,
														buffer_t *buf, uint8_t append) {
	eefs_error_t res;

	repad_filename(dent->name);

	if (append) {
		res = eepromfs_open(dent->name, &buf->pvt.eefh, EEFS_MODE_APPEND);
	} else {
		res = eepromfs_open(dent->name, &buf->pvt.eefh, EEFS_MODE_WRITE);
	}
	translate_error(res);

	if (res != EEFS_ERROR_OK)
		return;

	/* set up buffer fields for writing */
	mark_write_buffer(buf);
	buf->position = 2;
	buf->lastused = 2;
	buf->data[2]  = 0x0d;
	buf->refill   = eefs_refill_write;
	buf->cleanup  = eefs_cleanup_write;
}

static void eefs_open_rel(path_t *path, cbmdirent_t *dent, buffer_t *buf,
													uint8_t recordlen, uint8_t mode) {
	set_error(ERROR_SYNTAX_UNABLE);
}

static uint8_t eefs_delete(path_t *path, cbmdirent_t *dent) {
	eefs_error_t res;

	set_dirty_led(1);

	repad_filename(dent->name);
	res = eepromfs_delete(dent->name);
	translate_error(res);

	update_leds();

	/* check result, can only be not_found or ok */
	if (res == EEFS_ERROR_OK)
		return 1;
	else
		return 0;
}

static uint8_t eefs_disk_label(uint8_t part, uint8_t *label) {
	// copy with zero-termination
	memcpy_P(label, disk_label, 17);
	return 0;
}

static uint8_t eefs_dir_label(path_t *path, uint8_t *label) {
	// copy without zero-termination
	memcpy_P(label, disk_label, 16);
	return 0;
}

static uint8_t eefs_disk_id(path_t *path, uint8_t *id) {
	memcpy_P(id, disk_id, 5);
	return 0;
}

static uint16_t eefs_disk_free(uint8_t part) {
	// converted to 256-byte-blocks as a rough CBM block approximation
	return eepromfs_free_sectors() / (256 / EEPROMFS_SECTORSIZE);
}

static void eefs_read_sector(buffer_t *buf, uint8_t part, uint8_t track, uint8_t sector) {
	set_error_ts(ERROR_READ_NOHEADER, track, sector);
}

static void eefs_write_sector(buffer_t *buf, uint8_t part, uint8_t track, uint8_t sector) {
	set_error_ts(ERROR_READ_NOHEADER, track, sector);
}

static void eefs_format(uint8_t drv, uint8_t *name, uint8_t *id) {
	eepromfs_format();
}

static uint8_t eefs_opendir(dh_t *dh, path_t *path) {
	dh->part = path->part;
	eepromfs_opendir(&dh->dir.eefs);
	return 0;
}

static int8_t eefs_readdir(dh_t *dh, cbmdirent_t *dent) {
	eefs_dirent_t eedent;
	uint8_t       res;

	/* clear eefs-dirent to ensure a zero-terminated file name */
	memset(&eedent, 0, sizeof(eefs_dirent_t));

	res = eepromfs_readdir(&dh->dir.eefs, &eedent);

	if (res)
		return -1;

	/* clear result buffer */
	memset(dent, 0, sizeof(cbmdirent_t));

	dent->opstype   = OPSTYPE_EEFS;
	dent->typeflags = TYPE_PRG;
	dent->blocksize = (eedent.size + 255) / 256; // FIXME: Maybe change to 254-byte blocks?
	terminate_filename(eedent.name);
	memcpy(dent->name, eedent.name, CBM_NAME_LENGTH);

	// FIXME: add fake date/time fields, add a clear_dent() function to do the memset and this

	return 0;
}

/**
 * eefs_chdir - chdir for eefs
 * @path   : path object of the location of dirname
 * @dirname: directory to be changed into
 *
 * Always returns 0 for success.
 */
static uint8_t eefs_chdir(path_t *path, cbmdirent_t *dent) {
	// always ignore
	return 0;
}

static void eefs_rename(path_t *path, cbmdirent_t *oldname, uint8_t *newname) {
	eefs_error_t res;

	repad_filename(oldname->name);
	repad_filename(newname);

	res = eepromfs_rename(oldname->name, newname);
	translate_error(res);
}

/* ------------------------------------------------------------------------- */
/*  ops struct                                                               */
/* ------------------------------------------------------------------------- */

const PROGMEM fileops_t eefs_ops = {
	eefs_open_read,
	eefs_open_write,
	eefs_open_rel,
	eefs_delete,
	eefs_disk_label,
	eefs_dir_label,
	eefs_disk_id,
	eefs_disk_free,
	eefs_read_sector,
	eefs_write_sector,
	eefs_format,
	eefs_opendir,
	eefs_readdir,
	image_mkdir,
	eefs_chdir,
	eefs_rename
};
