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


	 fileops.c: Generic file operations

*/

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "d64ops.h"
#include "dirent.h"
#include "display.h"
#include "doscmd.h"
#include "eefs-ops.h"
#include "sfs-ops.h"
#include "errormsg.h"
#include "fatops.h"
#include "flags.h"
#include "ff.h"
#include "m2iops.h"
#include "parser.h"
#include "progmem.h"
#include "uart.h"
#include "ustring.h"
#include "utils.h"
#include "wrapops.h"
#include "fileops.h"

/* ------------------------------------------------------------------------- */
/*  global variables                                                         */
/* ------------------------------------------------------------------------- */

uint8_t       image_as_dir;
cbmdirent_t   previous_file_dirent;
static path_t previous_file_path;


/* ------------------------------------------------------------------------- */
/*  Some constants used for directory generation                             */
/* ------------------------------------------------------------------------- */

#define HEADER_OFFSET_DRIVE 4
#define HEADER_OFFSET_NAME  8
#define HEADER_OFFSET_ID   26

/* offsets within a D64 BAM sector for raw directory emulation */
#define BAM_OFFSET_NAME  0x90
#define BAM_OFFSET_ID    0xa2
#define BAM_A0_AREA_SIZE (0xaa - 0x90 + 1)

/* NOTE: I wonder if RLE-packing would save space in flash? */
const PROGMEM uint8_t dirheader[] = {
	1, 4,                            /* BASIC start address */
	1, 1,                            /* next line pointer */
	0, 0,                            /* line number 0 */
	0x12, 0x22,                      /* Reverse on, quote */
	'S','D','2','I','E','C',' ',' ', /* 16 spaces as the disk name */
	' ',' ',' ',' ',' ',' ',' ',' ', /* will be overwritten if needed */
	0x22,0x20,                       /* quote, space */
	'I','K',' ','2','A',             /* id IK, shift-space, dosmarker 2A */
	00                               /* line-end marker */
};

const PROGMEM uint8_t syspart_line[] = {
		1, 1, /* next line pointer */
		0, 0, /* number of free blocks (to be filled later) */
		' ',' ',' ',
		'"','S','Y','S','T','E','M','"',
		' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
		'S','Y','S',
		0x20, 0x20, 0x00 /* Filler and end marker */
	};

const PROGMEM uint8_t dirfooter[] = {
	1, 1, /* next line pointer */
	0, 0, /* number of free blocks (to be filled later */
	'B','L','O','C','K','S',' ','F','R','E','E','.',
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, /* Filler and end marker */
	0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00
};

const PROGMEM uint8_t filetypes[] = {
	'D','E','L', // 0
	'S','E','Q', // 1
	'P','R','G', // 2
	'U','S','R', // 3
	'R','E','L', // 4
	'C','B','M', // 5
	'D','I','R', // 6
	'?','?','?', // 7
	'N','A','T', // 8
	'4','1',' ', // 9
	'7','1',' ', // 10
	'8','1',' ', // 11
};

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * createentry - create a single directory entry in buf
 * @dent  : directory entry to be added
 * @buf   : buffer to be used
 * @format: entry format
 *
 * This function creates a directory entry for dent in the selected format
 * in the given buffer.
 */
static void createentry(cbmdirent_t *dent, buffer_t *buf, dirformat_t format) {
	uint8_t i;
	uint8_t *data = buf->data;

	if(format == DIR_FMT_CMD_LONG)
		i=63;
	else if(format == DIR_FMT_CMD_SHORT)
		i=41;
	else
		i=31;

	buf->lastused  = i;
	/* Clear the line */
	memset(data, ' ', i);
	/* Line end marker */
	data[i] = 0;

	/* Next line pointer, 1571-compatible =) */
	if (dent->remainder != 0xff)
		/* store remainder in low byte of link pointer          */
		/* +2 so it is never 0 (end-marker) or 1 (normal value) */
		*data++ = dent->remainder+2;
	else
		*data++ = 1;
	*data++ = 1;

	*data++ = dent->blocksize & 0xff;
	*data++ = dent->blocksize >> 8;

	/* Filler before file name */
	data++;
	if (dent->blocksize < 100)
		data++;
	if (dent->blocksize < 10)
		data++;
	*data++ = '"';

	/* copy and adjust the filename - C783 */
	memcpy(data, dent->name, CBM_NAME_LENGTH);
	for (i=0;i<=CBM_NAME_LENGTH;i++)
		if (dent->name[i] == 0x22 || dent->name[i] == 0 || i == 16) {
			data[i] = '"';
			while (i<=CBM_NAME_LENGTH) {
				if (data[i] == 0)
					data[i] = ' ';
				else
					data[i] &= 0x7f;
				i++;
			}
		}

	/* Skip name and final quote */
	data += CBM_NAME_LENGTH+1;

	if (dent->typeflags & FLAG_SPLAT)
		*data = '*';

	/* File type */
	memcpy_P(data+1, filetypes + TYPE_LENGTH * (dent->typeflags & EXT_TYPE_MASK),
					 (format & DIR_FMT_CMD_SHORT) ? 1 : TYPE_LENGTH);

	/* RO marker */
	if (dent->typeflags & FLAG_RO)
		data[4] = '<';

	if(format & DIR_FMT_CMD_LONG) {
		data += 7;
		data = appendnumber(data,dent->date.month);
		*data++ = '/';
		data = appendnumber(data,dent->date.day);
		*data++ = '/';
		data = appendnumber(data,dent->date.year % 100) + 3;
		data = appendnumber(data,(dent->date.hour>12?dent->date.hour-12:dent->date.hour));
		*data++ = '.';
		data = appendnumber(data,dent->date.minute) + 1;
		*data++ = (dent->date.hour>11?'P':'A');
		*data++ = 'M';
		while (*data)
			*data++ = 1;
	} else if(format == DIR_FMT_CMD_SHORT) {
		/* Add date/time stamp */
		data+=3;
		data = appendnumber(data,dent->date.month);
		*data++ = '/';
		data = appendnumber(data,dent->date.day) + 1;
		data = appendnumber(data,(dent->date.hour>12?dent->date.hour-12:dent->date.hour));
		*data++ = '.';
		data = appendnumber(data,dent->date.minute) + 1;
		*data++ = (dent->date.hour>11?'P':'A');
		while(*data)
			*data++ = 1;
	} else {
		/* Extension: Hidden marker */
		if (dent->typeflags & FLAG_HIDDEN)
			data[5] = 'H';
	}
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/**
 * dir_footer - generate the directory footer
 * @buf: buffer to be used
 *
 * This is the final callback used during directory generation. It generates
 * the "BLOCKS FREE" message and indicates that this is the final buffer to
 * be sent. Always returns 0 for success.
 */
static uint8_t dir_footer(buffer_t *buf) {
	uint16_t blocks;

	/* Copy the "BLOCKS FREE" message */
	memcpy_P(buf->data, dirfooter, sizeof(dirfooter));

	blocks = disk_free(buf->pvt.dir.dh.part);
	buf->data[2] = blocks & 0xff;
	buf->data[3] = blocks >> 8;

	buf->position = 0;
	buf->lastused = 31;
	buf->sendeoi  = 1;

	return 0;
}

/* Callback for the partition directory */
static uint8_t pdir_refill(buffer_t* buf) {
	cbmdirent_t dent;

	buf->position = 0;

	/* read volume name */
	while(buf->pvt.pdir.part < max_part) {
		if (disk_label(buf->pvt.pdir.part, dent.name)) {
			free_buffer(buf);
			return 1;
		}

		dent.blocksize = buf->pvt.pdir.part+1;

		if (partition[buf->pvt.pdir.part].fop == &d64ops) {
			/* Use the correct partition type for Dxx images */
			dent.typeflags = (partition[buf->pvt.pdir.part].imagetype & D64_TYPE_MASK)
											 + TYPE_NAT - 1;
		} else {
			/* Anything else is "native" */
			dent.typeflags = TYPE_NAT;
		}

		buf->pvt.pdir.part++;

		/* Parse the name pattern */
		if (buf->pvt.pdir.matchstr &&
				!match_name(buf->pvt.pdir.matchstr, &dent, 0))
			continue;

		createentry(&dent, buf, DIR_FMT_CBM);
		return 0;
	}
	buf->lastused = 1;
	buf->sendeoi = 1;
	memset(buf->data,0,2);
	return 0;
}

/**
 * dir_refill - generate the next directory entry
 * @buf: buffer to be used
 *
 * This function generates a single directory entry with the next matching
 * file. If there is no more matching file the footer will be generated
 * instead. Used as a callback during directory generation.
 */
static uint8_t dir_refill(buffer_t *buf) {
	cbmdirent_t dent;

	uart_putc('+');

	buf->position = 0;

	if (buf->pvt.dir.counter) {
		/* Redisplay image file as directory */
		buf->pvt.dir.counter = 0;
		memcpy(&dent, buf->data+256-sizeof(dent), sizeof(dent));
		dent.typeflags = TYPE_DIR;
		createentry(&dent, buf, buf->pvt.dir.format);
		return 0;
	}

	switch (next_match(&buf->pvt.dir.dh,
										 buf->pvt.dir.matchstr,
										 buf->pvt.dir.match_start,
										 buf->pvt.dir.match_end,
										 buf->pvt.dir.filetype,
										 &dent)) {
	case 0:
		if (image_as_dir != IMAGE_DIR_NORMAL &&
				dent.opstype == OPSTYPE_FAT &&
				check_imageext(dent.pvt.fat.realname) != IMG_UNKNOWN) {
			if (image_as_dir == IMAGE_DIR_DIR) {
				dent.typeflags = (dent.typeflags & 0xf0) | TYPE_DIR;
			} else {
				/* Prepare to redisplay image file as directory */
				buf->pvt.dir.counter = 1;
				/* Use the end of the buffer as temporary storage */
				memcpy(buf->data+256-sizeof(dent), &dent, sizeof(dent));
			}
		}
		createentry(&dent, buf, buf->pvt.dir.format);
		return 0;

	case -1:
		return dir_footer(buf);

	default:
		free_buffer(buf);
		return 1;
	}
}

/**
 * rawdir_dummy_refill - generate raw dummy directory entries
 * @buf: buffer to be used
 *
 * This function generates an empty raw directory entry which is
 * used to pad a raw directory to its correct size.
 */
static uint8_t rawdir_dummy_refill(buffer_t *buf) {
	if (buf->pvt.dir.counter++)
		buf->position = 0;
	else
		buf->position = 2;

	if (buf->pvt.dir.counter == 8)
		buf->sendeoi = 1;

	return 0;
}

/**
 * rawdir_refill - generate the next raw directory entry
 * @buf: buffer to be used
 *
 * This function generates a single raw directory entry for the next file.
 * Used as a callback during directory generation.
 */
static uint8_t rawdir_refill(buffer_t *buf) {
	cbmdirent_t dent;

	memset(buf->data, 0, 32);

	if ((buf->pvt.dir.counter & 0x80) == 0) {
		switch (readdir(&buf->pvt.dir.dh, &dent)) {
		case -1:
			/* last entry, switch to dummy entries */
			return rawdir_dummy_refill(buf);

		default:
			/* error in readdir */
			free_buffer(buf);
			return 1;

		case 0:
			/* entry found, creation below */
			break;
		}

		if (image_as_dir != IMAGE_DIR_NORMAL &&
				dent.opstype == OPSTYPE_FAT &&
				check_imageext(dent.pvt.fat.realname) != IMG_UNKNOWN) {
			if (image_as_dir == IMAGE_DIR_DIR) {
				dent.typeflags = (dent.typeflags & 0xf0) | TYPE_DIR;
			} else {
				/* Prepare to redisplay image file as directory */
				buf->pvt.dir.counter |= 0x80;
				memcpy(buf->data+256-sizeof(dent), &dent, sizeof(dent));
			}
		}
	} else {
		/* Redisplay image file as directory */
		buf->pvt.dir.counter &= 0x7f;
		memcpy(&dent, buf->data+256-sizeof(dent), sizeof(dent));
		dent.typeflags = TYPE_DIR;
	}

	buf->data[DIR_OFS_TRACK]     = 1;
	buf->data[DIR_OFS_SIZE_LOW]  = dent.blocksize & 0xff;
	buf->data[DIR_OFS_SIZE_HI ]  = dent.blocksize >> 8;
	buf->data[DIR_OFS_FILE_TYPE] = dent.typeflags ^ FLAG_SPLAT;

	/* Copy file name without 0-byte */
	memset(buf->data + DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH);
	memcpy(buf->data + DIR_OFS_FILE_NAME, dent.name, ustrlen(dent.name));

	/* Every 8th entry is two bytes shorter   */
	/* because the t/s link bytes are skipped */
	if ((buf->pvt.dir.counter++) & 0x7f)
		buf->position = 0;
	else
		buf->position = 2;

	buf->lastused = 31;

	if ((buf->pvt.dir.counter & 0x7f) == 8)
		buf->pvt.dir.counter &= 0x80;

	return 0;
}

/**
 * load_directory - Prepare directory generation and create header
 * @secondary: secondary address used for reading the directory
 *
 * This function prepeares directory reading and fills the buffer
 * with the header line of the directory listing.
 * BUG: There is a not-well-known feature in the 1541/1571 disk
 * drives (and possibly others) that returns unparsed directory
 * sectors if $ is opened with a secondary address != 0. This
 * is not emulated here.
 */
static void load_directory(uint8_t secondary) {
	buffer_t *buf;
	path_t path;
	uint8_t pos=1;

	buf = alloc_buffer();
	if (!buf)
		return;

	uint8_t *name;

	buf->secondary = secondary;
	buf->read      = 1;
	buf->lastused  = 31;

	if (command_length > 2 && secondary == 0) {
		if(command_buffer[1]=='=') {
			if(command_buffer[2]=='P') {
				/* Parse Partition Directory */

				/* copy static header to start of buffer */
				memcpy_P(buf->data, dirheader, sizeof(dirheader));
				memcpy_P(buf->data + 32, syspart_line, sizeof(syspart_line));
				buf->lastused  = 63;

				/* set partition number */
				buf->data[HEADER_OFFSET_DRIVE] = max_part;

				/* Let the refill callback handle everything else */
				buf->refill = pdir_refill;

				if(command_length>3) {
					/* Parse the name pattern */
					if (parse_path(command_buffer+3, &path, &name, 0))
						return;

					buf->pvt.pdir.matchstr = name;
				}
				stick_buffer(buf);

				return;
			} else if(command_buffer[2]=='T') {
				buf->pvt.dir.format = DIR_FMT_CMD_SHORT;
				pos=3;
			}
		}
	}

	if (command_buffer[pos]) { /* do we have a path to scan? */
		if (command_length > 2) {
			/* Parse the name pattern */
			if (parse_path(command_buffer+pos, &path, &name, 0))
				return;

			if (opendir(&buf->pvt.dir.dh, &path))
				return;

			buf->pvt.dir.matchstr = name;

			/* Check for a filetype match */
			name = ustrchr(name, '=');
			if (name != NULL) {
				*name++ = 0;
				switch (*name) {
				case 'S':
					buf->pvt.dir.filetype = TYPE_SEQ;
					break;

				case 'P':
					buf->pvt.dir.filetype = TYPE_PRG;
					break;

				case 'U':
					buf->pvt.dir.filetype = TYPE_USR;
					break;

				case 'R':
					buf->pvt.dir.filetype = TYPE_REL;
					break;

				case 'C': /* This is guessed, not verified */
					buf->pvt.dir.filetype = TYPE_CBM;
					break;

				case 'B': /* CMD compatibility */
				case 'D': /* Specifying DEL matches everything anyway */
					buf->pvt.dir.filetype = TYPE_DIR;
					break;

				case 'H': /* Extension: Also show hidden files */
					buf->pvt.dir.filetype = FLAG_HIDDEN;
					break;
				}
				if(buf->pvt.dir.filetype) {
					name++;
					if(*name++ != ',') {
						goto scandone;
					}
				}
				while(*name) {
					switch(*name++) {
					case '>':
						if(parse_date(&date_match_start,&name))
							goto scandone;
						if(date_match_start.month && date_match_start.day) // ignore 00/00/00
							buf->pvt.dir.match_start = &date_match_start;
						break;
					case '<':
						if(parse_date(&date_match_end,&name))
							goto scandone;
						if(date_match_end.month && date_match_end.day) // ignore 00/00/00
							buf->pvt.dir.match_end = &date_match_end;
						break;
					case 'L':
						/* don't switch to long format if 'N' has already been sent */
						if(buf->pvt.dir.format != DIR_FMT_CBM)
							buf->pvt.dir.format = DIR_FMT_CMD_LONG;
						break;
					case 'N':
						buf->pvt.dir.format=DIR_FMT_CBM; /* turn off extended listing */
						break;
					default:
						goto scandone;
					}
					if(*name && *name++ != ',') {
						goto scandone;
					}
				}
			}
		} else {
			// Command string is two characters long, parse the drive
			if (command_buffer[1] == '0')
				path.part = current_part;
			else if (isdigit(command_buffer[1]))
				path.part = command_buffer[1] - '0' - 1;
#ifdef CONFIG_HAVE_EEPROMFS
			else if (command_buffer[1] == '!' && eefs_partition != 255)
				path.part = eefs_partition;
#endif
#ifdef CONFIG_HAVE_SERIALFS
			else if(command_buffer[1] == '%' && sfs_partition != 255)
				path.part = sfs_partition;
#endif
			else {
				buf->pvt.dir.matchstr = command_buffer + 1;
				path.part = current_part;
			}
			if (path.part >= max_part) {
				set_error(ERROR_DRIVE_NOT_READY);
				return;
			}
			path.dir = partition[path.part].current_dir;
			if (opendir(&buf->pvt.dir.dh, &path))
				return;
		}
	} else {
		path.part = current_part;
		path.dir  = partition[path.part].current_dir;  // if you do not do this, get_label will fail below.
		if (opendir(&buf->pvt.dir.dh, &path))
			return;
	}

scandone:
	if (secondary != 0) {
		/* Raw directory */

		if (partition[path.part].fop == &d64ops) {
			/* No need to fake it for D64 files */
			d64_raw_directory(&path, buf);
			return;
		}

		/* prepare a fake BAM sector */
		memset(buf->data, 0, 256);
		memset(buf->data + BAM_OFFSET_NAME - 2, 0xa0, BAM_A0_AREA_SIZE);

		/* fill label and id */
		if (dir_label(&path, buf->data + BAM_OFFSET_NAME - 2))
			return;

		if (disk_id(&path, buf->data + BAM_OFFSET_ID - 2))
			return;

		/* change padding of label and id to 0xa0 */
		name = buf->data + BAM_OFFSET_NAME - 2 + CBM_NAME_LENGTH;
		while (*--name == ' ')
			*name = 0xa0;

		if (buf->data[BAM_OFFSET_ID+2-2] == 0x20)
			buf->data[BAM_OFFSET_ID+2-2] = 0xa0;

		/* DOS version marker */
		buf->data[0] = 'A';

		buf->refill = rawdir_refill;
		buf->lastused = 253;
	} else {

		/* copy static header to start of buffer */
		memcpy_P(buf->data, dirheader, sizeof(dirheader));

		/* set partition number */
		buf->data[HEADER_OFFSET_DRIVE] = path.part+1;

		/* read directory name */
		if (dir_label(&path, buf->data+HEADER_OFFSET_NAME))
			return;

		/* read id */
		if (disk_id(&path,buf->data+HEADER_OFFSET_ID))
			return;

		/* Let the refill callback handle everything else */
		buf->refill = dir_refill;
	}

	/* Keep the buffer around */
	stick_buffer(buf);

	return;

}

/**
 * directbuffer_refill - refill callback for direct buffers
 * @buf: buffer to be used
 *
 * This function is used as the refill callback for direct buffers ('#')
 * and will switch to the next or the first buffer in the chain. Always
 * returns 0.
 */
uint8_t directbuffer_refill(buffer_t *buf) {
	uint8_t sec = buf->secondary;

	buf->secondary = BUFFER_SEC_CHAIN - sec;

	if (buf->pvt.buffer.next == NULL)
		buf = buf->pvt.buffer.first;
	else
		buf = buf->pvt.buffer.next;

	buf->secondary = sec;
	buf->position  = 0;
	buf->mustflush = 0;
	return 0;
}

/**
 * largebuffer_cleanup - cleanup callback for large buffers
 * @buf: buffer to be cleaned
 *
 * This function is used as the cleanup callback for large buffer and
 * will free all buffers used in the large buffer chain. This does mean
 * that the free_buffer call done after calling cleanup will be
 * passed an already-freed buffer, but that case is accounted for in
 * free_buffer and results in a no-op. Always returns 0.
 */
static uint8_t largebuffer_cleanup(buffer_t *buf) {
	buf = buf->pvt.buffer.first;
	while (buf != NULL) {
		free_buffer(buf);
		buf = buf->pvt.buffer.next;
	}
	return 0;
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

/**
 * open_buffer - function to open a direct-access buffer
 * @secondary: secondary address used
 *
 * This function is called when the computer wants to open a direct
 * access buffer (#).
 */
static void open_buffer(uint8_t secondary) {
	buffer_t *buf,*prev;
	uint8_t count;

	if (command_length == 3 && command_buffer[1] == '#') {
		/* Open a large buffer */
		count = command_buffer[2] - '0';
		if (count == 0)
			return;

		/* Allocate a chain of linked buffers */
		buf = alloc_linked_buffers(count);
		if (buf == NULL)
			return;

		do {
			buf->secondary       = BUFFER_SEC_CHAIN - secondary;
			buf->refill          = directbuffer_refill;
			buf->cleanup         = largebuffer_cleanup;
			buf->read            = 1;
			buf->lastused        = 255;
			buf->pvt.buffer.part = current_part; // for completeness, not needed for large buffers yet
			mark_write_buffer(buf);
			prev = buf;
			buf = buf->pvt.buffer.next;
		} while (buf != NULL);

		prev->sendeoi = 1;
		buf = prev->pvt.buffer.first;

		/* Set the first buffer as active by using the real secondary */
		buf->secondary = secondary;

	} else {
		/* Normal buffer request */
		// FIXME: This command can specify a specific buffer number.
		buf = alloc_buffer();
		if (!buf)
			return;

		buf->secondary        = secondary;
		buf->read             = 1;
		buf->position         = 1;  /* Sic! */
		buf->lastused         = 255;
		buf->sendeoi          = 1;
		buf->pvt.buffer.size  = 1;
		buf->pvt.buffer.part  = current_part;
		/* directbuffer_refill is used to check for # buffers in iec.c */
		buf->refill           = directbuffer_refill;
		buf->pvt.buffer.first = buf;
		mark_write_buffer(buf);
	}
	return;
}

/**
 * file_open_previous - reopens the last opened file
 *
 * This function reopens the last opened file in read mode.
 * The secondary address for this is always 0.
 */
void file_open_previous(void) {
	cbmdirent_t dent = previous_file_dirent;
	path_t      path = previous_file_path;
	buffer_t *buf = alloc_buffer();

	if (!buf)
		return;

	buf->secondary = 0;

	display_filename_read(path.part, CBM_NAME_LENGTH, dent.name);
	open_read(&path, &dent, buf);
}

/**
 * file_open - open a file on given secondary
 * @secondary: secondary address used in OPEN call
 *
 * This function opens the file named in command_buffer on the given
 * secondary address. All special names and prefixes/suffixed are handled
 * here, e.g. $/#/@/,S,W
 */
void file_open(uint8_t secondary) {
	buffer_t *buf;
	uint8_t i = 0;
	uint8_t recordlen = 0;

	/* If the secondary is already in use, close the existing buffer */
	buf = find_buffer(secondary);
	if (buf != NULL) {
		/* FIXME: What should we do if an error occurs? */
		cleanup_and_free_buffer(buf);
	}

	/* Assume everything will go well unless proven otherwise */
	set_error(ERROR_OK);

	/* Strip 0x0d characters from end of name (C2BD-C2CA) */
	if (command_length > 1) {
		if (command_buffer[command_length-1] == 0x0d)
			command_length -= 1;
		else if (command_buffer[command_length-2] == 0x0d)
			command_length -= 2;
	}

	/* Clear the remainder of the command buffer, simplifies parsing */
	memset(command_buffer+command_length, 0, sizeof(command_buffer)-command_length);

	uart_trace(command_buffer,0,command_length);

	/* Direct access? */
	if (command_buffer[0] == '#') {
		open_buffer(secondary);
		return;
	}

	/* Parse type+mode suffixes */
	uint8_t *ptr = command_buffer;
	enum open_modes mode = OPEN_READ;
	uint8_t filetype = TYPE_DEL;

	/* check for a single * to load the previous file */
	if (secondary         == 0 &&
			command_length    == 1 &&
			command_buffer[0] == '*') {
		if (previous_file_dirent.name[0]) {
			/* previous file is valid */
			file_open_previous();
			return;
		}

		/* force file type to PRG - d7cd/d81c */
		filetype = TYPE_PRG;
	}

	/* check file type and mode */
	while(i++ < 2 && *ptr && (ptr = ustrchr(ptr, ','))) {
		*ptr = 0;
		ptr++;
		switch (*ptr) {
		case 0:
			break;

		case 'R': /* Read */
			mode = OPEN_READ;
			break;

		case 'W': /* Write */
			mode = OPEN_WRITE;
			break;

		case 'A': /* Append */
			mode = OPEN_APPEND;
			break;

		case 'M': /* Modify */
			mode = OPEN_MODIFY;
			break;

		case 'D': /* DEL */
			filetype = TYPE_DEL;
			break;

		case 'S': /* SEQ */
			filetype = TYPE_SEQ;
			break;

		case 'P': /* PRG */
			filetype = TYPE_PRG;
			break;

		case 'U': /* USR */
			filetype = TYPE_USR;
			break;

		case 'L': /* REL */
			filetype = TYPE_REL;
			mode = OPEN_WRITE;
			if((ptr = ustrchr(ptr, ',')))
				recordlen = *(++ptr);
			i = 2;  // stop the scan
			break;
		}
	}

	/* Load directory? */
	if (command_buffer[0] == '$') {
		load_directory(secondary);
		return;
	}

	/* Parse path+partition numbers */
	uint8_t *fname;
	int8_t res;
	cbmdirent_t dent;
	path_t path;

	/* Parse path and file name */
	if (parse_path(command_buffer, &path, &fname, 0))
			return;

#ifdef CONFIG_M2I
	/* For M2I only: Remove trailing spaces from name */
	if (partition[path.part].fop == &m2iops) {
		res = ustrlen(fname);
		while (--res && fname[res] == ' ')
			fname[res] = 0;
	}
#endif

	/* Filename matching */
	if (opendir(&matchdh, &path))
		return;

	do {
		res = next_match(&matchdh, fname, NULL, NULL, FLAG_HIDDEN, &dent);
		if (res > 0)
			/* Error, abort */
			return;

		/* Don't match on DEL or DIR */
		if ((dent.typeflags & TYPE_MASK) != TYPE_DEL &&
				(dent.typeflags & TYPE_MASK) != TYPE_DIR)
			break;

		/* But do match if it's for writing */
		if (mode == OPEN_WRITE || secondary == 1)
			break;
	} while (res == 0);

	if(res && filetype == TYPE_REL && !recordlen) {
		set_error(ERROR_SYNTAX_UNABLE);
		return;
	}

	/* If match found is a REL... */
	if(!res && (dent.typeflags & TYPE_MASK) == TYPE_REL) {
		/* requested type must be REL or DEL */
		if(filetype != TYPE_REL && filetype != TYPE_DEL) {
			set_error(ERROR_FILE_TYPE_MISMATCH);
			return;
		}
		filetype = TYPE_REL;
		mode = OPEN_MODIFY;
	}

	/* Force mode+type for secondaries 0/1 */
	switch (secondary) {
	case 0:
		mode = OPEN_READ;
		if (filetype == TYPE_DEL)
			filetype = TYPE_PRG;
		break;

	case 1:
		mode = OPEN_WRITE;
		if (filetype == TYPE_DEL)
			filetype = TYPE_PRG;
		break;

	default:
		if (filetype == TYPE_DEL)
			filetype = TYPE_SEQ;
	}

	if (mode == OPEN_WRITE) {
		if (res == 0) {
			/* Match found */
			if (command_buffer[0] == '@') {
				/* Make sure there is a free buffer to open the new file later */
				if (!check_free_buffers()) {
					set_error(ERROR_NO_CHANNEL);
					return;
				}

				/* Copy dent because file_delete may change it */
				cbmdirent_t dentcopy = dent;

				/* Rewrite existing file: Delete the old one */
				if (file_delete(&path, &dentcopy) == 255)
					return;

#ifdef CONFIG_M2I
				/* Force fatops to create a new name based on the (long) CBM- */
				/* name instead of creating one with the old SFN and no LFN.  */
				if (dent.opstype == OPSTYPE_FAT || dent.opstype == OPSTYPE_FAT_X00)
					dent.pvt.fat.realname[0] = 0;
#endif
			} else {
				/* Write existing file without replacement: Raise error */
				set_error(ERROR_FILE_EXISTS);
				return;
			}
		} else {
			/* Normal write or non-existing rewrite */
			/* Doesn't exist: Copy name to dent */
			memset(&dent, 0, sizeof(dent));
			ustrncpy(dent.name, fname, CBM_NAME_LENGTH);
			set_error(ERROR_OK); // because first_match has set FNF
		}
	} else if (res != 0) {
		/* File not found */
		set_error(ERROR_FILE_NOT_FOUND);
		return;
	}

	/* Grab a buffer */
	buf = alloc_buffer();
	if (!buf)
		return;

	buf->secondary = secondary;

	if(filetype == TYPE_REL) {
		display_filename_write(path.part,CBM_NAME_LENGTH,dent.name);
		open_rel(&path, &dent, buf, recordlen, (mode == OPEN_MODIFY));
		return;
	}

	previous_file_path   = path;
	previous_file_dirent = dent;

	switch (mode) {
	case OPEN_MODIFY:
	case OPEN_READ:
		/* Modify is the same as read, but allows reading *ed files.        */
		/* FAT doesn't have anything equivalent, so both are mapped to READ */
		display_filename_read(path.part,CBM_NAME_LENGTH,dent.name);
		open_read(&path, &dent, buf);
		break;

	case OPEN_WRITE:
	case OPEN_APPEND:
		display_filename_write(path.part,CBM_NAME_LENGTH,dent.name);
		open_write(&path, &dent, filetype, buf, (mode == OPEN_APPEND));
		break;
	}
}
