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


	 sfs-ops.c: serialfs operations

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "serial-fs.h"
#include "errormsg.h"
#include "fatops.h"
#include "led.h"
#include "parser.h"
#include "ustring.h"
#include "sfs-ops.h"
#include "ops_common.h"

/* ------------------------------------------------------------------------- */
/*  data structures, constants, global variables                             */
/* ------------------------------------------------------------------------- */

//                                           1234567890123456
static const PROGMEM uint8_t s_diskLabel[] = "SERIALFS        ";
static const PROGMEM uint8_t s_diskId[]    = "SL 2A";

uint8_t sfs_partition;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */


/**
 * translate_error - translate an SFS error code into a Commodore error message
 * @res: SFS error code to be translated
 *
 * This function sets the error channel according to the problem given in
 * @res.
 */
static void translate_error(sfs_error_t res)
{
	switch (res) {
	//  case SFS_ERROR_OK:
	//    set_error(ERROR_OK);
	//    break;

	//  case SFS_ERROR_FILENOTFOUND:
	//    set_error(ERROR_FILE_NOT_FOUND);
	//    break;

	//  case SFS_ERROR_FILEEXISTS:
	//    set_error(ERROR_FILE_EXISTS);
	//    break;

	//  case SFS_ERROR_DIRFULL:
	//  case SFS_ERROR_DISKFULL:
	//    set_error_ts(ERROR_DISK_FULL, res, 0);
	//    break;

	//  case SFS_ERROR_INVALID:
	//    set_error(ERROR_SYNTAX_UNABLE);
	//    break;

	//  case SFS_ERROR_UNIMPLEMENTED:
	//    set_error(ERROR_SYNTAX_UNKNOWN);
	//    break;

	default:
		set_error_ts(ERROR_SYNTAX_UNABLE, res, 99);
		break;
	}
}

/**
 * sfs_refill_read - refill-callback for reading
 * @buf: target buffer
 *
 * This is the refill-callback function for files opened for reading.
 */
static uint8_t sfs_refill_read(buffer_t* buf)
{
	sfs_error_t res;
	uint16_t bytes_read;

	res = serialfs_read(&buf->pvt.sffh, buf->data + 2, 254, &bytes_read);
	if(res not_eq SFS_ERROR_OK) {
		translate_error(res);
		free_buffer(buf);
		return 1;
	}

	buf->position = 2;
	buf->lastused = bytes_read + 1;

	// check if the last byte of file is in the buffer
	if(bytes_read < 254 or buf->pvt.sffh.cur_offset == buf->pvt.sffh.size) {
		buf->sendeoi = 1;
	} else {
		buf->sendeoi = 0;
	}

	return 0;
}


/**
 * sfs_refill_write - refill-callback for writing
 * @buf: target buffer
 *
 * This is the refill-callback function for files opened for writing.
 */
static uint8_t sfs_refill_write(buffer_t* buf)
{
	sfs_error_t res;
	uint16_t byteswritten;

	// fix up lastused for incomplete blocks
	if(not buf->mustflush)
		buf->lastused = buf->position - 1;

	res = serialfs_write(&buf->pvt.sffh, buf->data + 2, buf->lastused - 1, &byteswritten);
	if(res not_eq SFS_ERROR_OK) { // serialfs never returns OK if the write was incomplete
		translate_error(res);
		serialfs_close(&buf->pvt.sffh);
		free_buffer(buf);
		return 1;
	}

	mark_buffer_clean(buf);
	buf->mustflush = 0;
	buf->position  = 2;
	buf->lastused  = 2;

	return 0;
}


/**
 * sfs_cleanup_write - cleanup-callback for writing
 * @buf: target buffer
 *
 * This is the cleanup-callback function for files opened for writing.
 */
static uint8_t sfs_cleanup_write(buffer_t* buf)
{
	if(not buf->allocated)
		return 0;

	// write remaining data
	if(buf->refill(buf))
		return 1;

	serialfs_close(&buf->pvt.sffh);
	buf->cleanup = callback_dummy;
	return 0;
}


/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

void sfsops_init(void)
{
	sfs_partition = 255;

	// do not add if all partitions are already in use
	if(max_part >= CONFIG_MAX_PARTITIONS)
		return;

	sfs_partition = max_part;
	partition[sfs_partition].fop = &sfs_ops;
	++max_part;

	//serialfs_init();
}

/**
 * fat_file_seek - callback for seek
 * @buf     : buffer to be worked on
 * @position: offset to seek to
 * @index   : offset within the record to seek to
 *
 * This function seeks to the offset position in the file associated
 * with the given buffer and sets the read pointer to the byte given
 * in index, effectively seeking to (position+index) for normal files.
 * Returns 1 if an error occured, 0 otherwise.
 */
uint8_t sfs_file_seek(buffer_t *buf, uint32_t position, uint8_t index)
{
	VAR_UNUSED(buf);
	VAR_UNUSED(position);
	VAR_UNUSED(index);
	// TODO: Process the seek over serialfs
	/*
	uint32_t pos = position + buf->pvt.fat.headersize;

	if (buf->dirty)
		if (sfs_file_write(buf))
			return 1;

	if (buf->pvt.fat.fh.fsize >= pos) {
		FRESULT res = f_lseek(&buf->pvt.fat.fh, pos);
		if (res != FR_OK) {
			parse_error(res,0);
			f_close(&buf->pvt.fat.fh);
			free_buffer(buf);
			return 1;
		}

		if (sfs_file_read(buf))
			return 1;
	} else {
		buf->data[2]  = (buf->recordlen ? 255:13);
		buf->lastused = 2;
		buf->fptr     = position;
		set_error(ERROR_RECORD_MISSING);
	}

	buf->position = index + 2;
	if(index + 2 > buf->lastused)
		buf->position = buf->lastused;
*/
	return 0;
}

/**
 * sfs_file_close - close the file associated with a buffer
 * @buf: buffer to be worked on
 *
 * This function closes the file associated with the given buffer. If the buffer
 * was opened for writing the data contents will be stored if required.
 * Additionally the buffer will be marked as free.
 * Used as a cleanup-callback for reading and writing.
 */
static uint8_t sfs_file_close(buffer_t *buf)
{
	FRESULT res;

	if(!buf->allocated) return 0;

	if(buf->write) {
		// Write the remaining data using the callback
		if(buf->refill(buf))
			return 1;
	}

	// TODO: process remote close of buf->pvt.sffh
	//res = f_close(&buf->pvt.sffh);
	res = 0;


	parse_error(res,1);
	buf->cleanup = callback_dummy;

	return res == FR_OK ? 1 : 0;
}


static void sfs_open_read(path_t* path, cbmdirent_t *dent, buffer_t *buf)
{
	VAR_UNUSED(path);

	repad_filename(dent->name);

	sfs_error_t res = serialfs_open(dent->name, &buf->pvt.sffh, SFS_MODE_READ);
	translate_error(res);

	if(res != SFS_ERROR_OK)
		return;

	// set up the buffer
	buf->read   = 1;
	buf->cleanup   = sfs_file_close;
	buf->refill = sfs_refill_read;
	buf->seek      = sfs_file_seek;
	// no cleanup/close function needed for read-only files
	stick_buffer(buf);

	buf->refill(buf);
}

static void sfs_open_write(path_t *path, cbmdirent_t *dent, uint8_t type,
														buffer_t *buf, uint8_t append)
{
	sfs_error_t res;

	repad_filename(dent->name);

	if(append) {
		res = serialfs_open(dent->name, &buf->pvt.sffh, SFS_MODE_APPEND);
	} else {
		res = serialfs_open(dent->name, &buf->pvt.sffh, SFS_MODE_WRITE);
	}
	translate_error(res);

	if(SFS_ERROR_OK not_eq res)
		return;

	// set up buffer fields for writing
	mark_write_buffer(buf);
	buf->position = 2;
	buf->lastused = 2;
	buf->data[2]  = 0x0d;
	buf->refill   = sfs_refill_write;
	buf->cleanup  = sfs_cleanup_write;
}

static void sfs_open_rel(path_t* path, cbmdirent_t* dent, buffer_t* buf,
													uint8_t recordlen, uint8_t mode) {
	VAR_UNUSED(path);
	VAR_UNUSED(dent);
	VAR_UNUSED(recordlen);
	VAR_UNUSED(mode);
	VAR_UNUSED(buf);
	set_error(ERROR_SYNTAX_UNABLE);
}

static uint8_t sfs_delete(path_t* path, cbmdirent_t *dent)
{
	VAR_UNUSED(path);
	sfs_error_t res;

	set_dirty_led(1);

	repad_filename(dent->name);
	res = serialfs_delete(dent->name);
	translate_error(res);

	update_leds();

	// check result, can only be not_found or ok
	if(res == SFS_ERROR_OK)
		return 1;
	else
		return 0;
}

static uint8_t sfs_disk_label(uint8_t part, uint8_t* label)
{
	VAR_UNUSED(part);
	// copy with zero-termination
	memcpy_P(label, s_diskLabel, 17);
	return 0;
}

static uint8_t sfs_dir_label(path_t* path, uint8_t* label)
{
	VAR_UNUSED(path);
	// copy without zero-termination
	memcpy_P(label, s_diskLabel, 16);
	return 0;
}

static uint8_t sfs_disk_id(path_t* path, uint8_t* id)
{
	VAR_UNUSED(path);
	memcpy_P(id, s_diskId, 5);
	return 0;
}

#define SFS_SECTORSIZE 256

static uint16_t sfs_disk_free(uint8_t part)
{
	VAR_UNUSED(part);
	// This is a meaningless operation since the host will have almost unlimited storage for this purpose.
	// converted to 256-byte-blocks as a rough CBM block approximation
	return 65535 / (256 / SFS_SECTORSIZE);
}

static void sfs_read_sector(buffer_t* buf, uint8_t part, uint8_t track, uint8_t sector)
{
	VAR_UNUSED(buf);
	VAR_UNUSED(part);
	set_error_ts(ERROR_READ_NOHEADER, track, sector);
}

static void sfs_write_sector(buffer_t *buf, uint8_t part, uint8_t track, uint8_t sector)
{
	VAR_UNUSED(buf);
	VAR_UNUSED(part);
	set_error_ts(ERROR_READ_NOHEADER, track, sector);
}

// Dummy function for format, we don't allow it.
static void sfs_format(uint8_t drv, uint8_t *name, uint8_t *id)
{
	VAR_UNUSED(drv);
	VAR_UNUSED(name);
	VAR_UNUSED(id);
	set_error(ERROR_SYNTAX_UNKNOWN);
}

static uint8_t sfs_opendir(dh_t* dh, path_t* path)
{
	dh->part = path->part;
	eepromfs_opendir(&dh->dir.eefs);
	return 0;
}


static int8_t sfs_readdir(dh_t *dh, cbmdirent_t *dent)
{
	sfs_dirent_t sfsdent;
	uint8_t       res;

	/* clear sfs-dirent to ensure a zero-terminated file name */
	memset(&sfsdent, 0, sizeof(sfs_dirent_t));

	res = serialfs_readdir(&dh->dir.sffs, &sfsdent);

	if(res)
		return -1;

	// clear result buffer
	memset(dent, 0, sizeof(cbmdirent_t));

	dent->opstype   = OPSTYPE_SFS;
	dent->typeflags = TYPE_PRG;
	dent->blocksize = (sfsdent.size + 255) / 256; // FIXME: Maybe change to 254-byte blocks?
	terminate_filename(sfsdent.name);
	memcpy(dent->name, sfsdent.name, CBM_NAME_LENGTH);

	// FIXME: add fake date/time fields, add a clear_dent() function to do the memset and this

	return 0;
}

/**
 * sfs_chdir - chdir for sfs
 * @path   : path object of the location of dirname
 * @dirname: directory to be changed into
 *
 * Always returns 0 for success.
 */
static uint8_t sfs_chdir(path_t* path, cbmdirent_t* dent)
{
	VAR_UNUSED(path);
	VAR_UNUSED(dent);
	return 0;
}

static void sfs_rename(path_t* path, cbmdirent_t* oldname, uint8_t* newname)
{
	VAR_UNUSED(path);
	sfs_error_t res;

	repad_filename(oldname->name);
	repad_filename(newname);

	res = serialfs_rename(oldname->name, newname);
	translate_error(res);
}

static void sfs_mkdir(path_t* path, uint8_t* dirname)
{
	VAR_UNUSED(path);
	VAR_UNUSED(dirname);
	set_error(ERROR_SYNTAX_UNABLE);
	return;
}

/* ------------------------------------------------------------------------- */
/*  ops struct                                                               */
/* ------------------------------------------------------------------------- */

const PROGMEM fileops_t sfs_ops = {
	sfs_open_read,
	sfs_open_write,
	sfs_open_rel,
	sfs_delete,
	sfs_disk_label,
	sfs_dir_label,
	sfs_disk_id,
	sfs_disk_free,
	sfs_read_sector,
	sfs_write_sector,
	sfs_format,
	sfs_opendir,
	sfs_readdir,
	sfs_mkdir,
	sfs_chdir,
	sfs_rename
};
