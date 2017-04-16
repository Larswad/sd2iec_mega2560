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


   m2iops.c: M2I operations

*/

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "dirent.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "led.h"
#include "parser.h"
#include "progmem.h"
#include "ustring.h"
#include "wrapops.h"
#include "m2iops.h"


#define M2I_ENTRY_LEN      33
#define M2I_ENTRY_OFFSET   18
#define M2I_CBMNAME_OFFSET 15
#define M2I_FATNAME_OFFSET 2
#define M2I_FATNAME_LEN    12

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * name_repad - changes CBM name padding in ops_scratch
 * @oldchar: char to be substituted
 * @newchar: char to be substituded with
 *
 * This function changes the padding of the CBM name in ops_scratch from oldchar
 * to newchar. Please note that strnsubst is not a straigt replacement for
 * this function.
 */
static void name_repad(uint8_t oldchar, uint8_t newchar) {
  uint8_t i = CBM_NAME_LENGTH-1;
  uint8_t *str;

  str = ops_scratch + M2I_CBMNAME_OFFSET;
  while (i > 0 && str[i] == oldchar)
    str[i--] = newchar;
}

/**
 * parsetype - change type letter to CBM filetype in ops_scratch
 *
 * This function replaces the type letter in the M2I line in ops_scratch
 * with a CBM file type. Returns 1 for deleted or unknown type letters,
 * 0 otherwise.
 */
static uint8_t parsetype(void) {
  switch (ops_scratch[0] | 0x20) { /* Lowercase the letter */
  case 'd':
    ops_scratch[0] = TYPE_DEL;
    return 0;

  case 's':
    ops_scratch[0] = TYPE_SEQ;
    return 0;

  case 'p':
    ops_scratch[0] = TYPE_PRG;
    return 0;

  case 'u':
    ops_scratch[0] = TYPE_USR;
    return 0;

  default:
    return 1;
  }
}

/**
 * load_entry - load M2I entry at offset into ops_scratch
 * @part  : partition number
 * @offset: offset in M2I file to be loaded
 *
 * This function loads the M2I line at offset into ops_scratch and zero-
 * terminates the FAT name within.  Returns 0 if successful, 1 at
 * end of file or 255 on error.
 */
static uint8_t load_entry(uint8_t part, uint16_t offset) {
  uint8_t i;

  i = image_read(part, offset, ops_scratch, M2I_ENTRY_LEN);

  if (i > 1)
    return 255;

  if (i == 1)
    /* Incomplete entry read, assuming end of file */
    return 1;

  /* Be nice and zero-terminate the FAT filename */
  i = 0;
  while (ops_scratch[M2I_FATNAME_OFFSET + i] != ' ' && i < M2I_FATNAME_LEN) i++;
  ops_scratch[M2I_FATNAME_OFFSET + i] = 0;

  return 0;
}

/**
 * find_empty_entry - returns the offset of the first empty M2I entry
 * @part: partition number
 *
 * This function looks for a deleted entry in an M2I file and returns
 * it offset. Returns 1 on error or an offset if successful. The
 * offset may point to a position beyond the end of file if there
 * were no free entries available.
 */
static uint16_t find_empty_entry(uint8_t part) {
  uint16_t pos = M2I_ENTRY_OFFSET;
  uint8_t i;

  while (1) {
    i = load_entry(part, pos);

    if (i) {
      if (i == 255)
        return 1;
      else
        return pos;
    }

    if (ops_scratch[0] == '-')
      return pos;

    pos += M2I_ENTRY_LEN;
  }
}

/**
 * open_existing - open an existing file
 * @path      : path handle
 * @dent      : pointer to cbmdirent struct with name of the file
 * @type      : type of the file (not checked)
 * @buf       : buffer to be used
 * @appendflag: Flags if the file should be opened for appending
 *
 * This function searches the file name in an M2I file and opens it
 * either in read or append mode according to appendflag by calling
 * the appropiate fat_* functions.
 */
static void open_existing(path_t *path, cbmdirent_t *dent, uint8_t type, buffer_t *buf, uint8_t appendflag) {
  if (load_entry(path->part, dent->pvt.m2i.offset)) {
    set_error(ERROR_FILE_NOT_FOUND);
    return;
  }

  if (parsetype()) {
    set_error(ERROR_FILE_NOT_FOUND);
    return;
  }

  ustrcpy(dent->pvt.fat.realname, ops_scratch + M2I_FATNAME_OFFSET);

  if (appendflag)
    fat_open_write(path, dent, type, buf, 1);
  else
    fat_open_read(path, dent, buf);
}

/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t m2i_opendir(dh_t *dh, path_t *path) {
  dh->part    = path->part;
  dh->dir.m2i = M2I_ENTRY_OFFSET;
  return 0;
}

static int8_t m2i_readdir(dh_t *dh, cbmdirent_t *dent) {
  uint8_t i;

  while (1) {
    i = load_entry(dh->part, dh->dir.m2i);
    if (i) {
      if (i == 255)
        return 1;
      else
        return -1;
    }

    memset(dent, 0, sizeof(cbmdirent_t));
    dent->pvt.m2i.offset = dh->dir.m2i;

    dh->dir.m2i += M2I_ENTRY_LEN;

    /* Check file type */
    if (parsetype())
      continue;

    dent->opstype   = OPSTYPE_M2I;
    dent->typeflags = ops_scratch[0];

    /* Copy CBM file name */
    name_repad(' ', 0);
    memcpy(dent->name, ops_scratch + M2I_CBMNAME_OFFSET, CBM_NAME_LENGTH);

    /* Get file size */
    if (dent->typeflags != TYPE_DEL) {
      /* Let's annoy some users */
      FILINFO finfo;

      FRESULT res = f_stat(&partition[dh->part].fatfs, ops_scratch + M2I_FATNAME_OFFSET, &finfo);
      if (res != FR_OK) {
        if (res == FR_NO_FILE)
          continue;
        else {
          parse_error(res,1);
          return 1;
        }
      }

      if (finfo.fsize > 16255746)
        /* File too large -> size 63999 blocks */
        dent->blocksize = 63999;
      else
        dent->blocksize = (finfo.fsize+253) / 254;

      dent->remainder = finfo.fsize % 254;
    } else
      dent->blocksize = 0;

    /* Fake Date/Time */
    dent->date.year  = 82;
    dent->date.month = 8;
    dent->date.day   = 31;

    return 0;
  }
}

static uint8_t m2i_getdisklabel(uint8_t part, uint8_t *label) {
  label[16] = 0;
  return image_read(part, 0, label, 16);
}

static uint8_t m2i_getdirlabel(path_t *path, uint8_t *label) {
  return image_read(path->part, 0, label, 16);
}

static void m2i_open_read(path_t *path, cbmdirent_t *dent, buffer_t *buf) {
  open_existing(path, dent, TYPE_RAW, buf, 0);
}

static void m2i_open_write(path_t *path, cbmdirent_t *dent, uint8_t type, buffer_t *buf, uint8_t append) {
  uint16_t offset;
  uint8_t *str;
  uint8_t *nameptr;
  uint8_t i;
  FRESULT res;

  /* Check for read-only image file */
  if (!(partition[path->part].imagehandle.flag & FA_WRITE)) {
    set_error(ERROR_WRITE_PROTECT);
    return;
  }

  if (append) {
    open_existing(path, dent, type, buf, 1);
  } else {
    if (check_invalid_name(dent->name)) {
      set_error(ERROR_SYNTAX_JOKER);
      return;
    }

    /* Find an empty entry */
    offset = find_empty_entry(path->part);
    if (offset < M2I_ENTRY_OFFSET)
      return;

    memset(ops_scratch, ' ', sizeof(ops_scratch));
    str = ops_scratch;

    switch (type & TYPE_MASK) {
    case TYPE_DEL:
      *str++ = 'D';
      break;

    case TYPE_SEQ:
      *str++ = 'S';
      break;

    case TYPE_PRG:
      *str++ = 'P';
      break;

    case TYPE_USR:
      *str++ = 'U';
      break;

    default:
      /* Unknown type - play it safe, don't create a file */
      return;
    }

    *str++ = ':';

    /* Generate a FAT name */
    for (i=0;i<8;i++) {
      *str++ = '0';
    }
    *str = 0;

    do {
      FILINFO finfo;

      finfo.lfn = NULL;
      /* See if it's already there */
      res = f_stat(&partition[path->part].fatfs, ops_scratch + M2I_FATNAME_OFFSET, &finfo);
      if (res == FR_OK) {
        str = ops_scratch + M2I_FATNAME_OFFSET+7;
        /* Increment name */
        while (1) {
          if (++(*str) > '9') {
            *str-- = '0';
            continue;
          }
          break;
        }
      }
    } while (res == FR_OK);

    if (res != FR_NO_FILE)
      return;

    /* Copy the CBM file name */
    nameptr = dent->name;
    str = ops_scratch + M2I_CBMNAME_OFFSET;
    while (*nameptr)
      *str++ = *nameptr++;

    /* Update dent with the new FAT name */
    ustrcpy(dent->pvt.fat.realname, ops_scratch + M2I_FATNAME_OFFSET);

    /* Finish creating the M2I entry */
    ops_scratch[M2I_FATNAME_OFFSET + 8]  = ' ';
    ops_scratch[M2I_FATNAME_OFFSET + 12] = ':';
    ops_scratch[M2I_CBMNAME_OFFSET + CBM_NAME_LENGTH] = 13;
    ops_scratch[M2I_CBMNAME_OFFSET + CBM_NAME_LENGTH + 1] = 10;

    /* Write it */
    if (image_write(path->part, offset, ops_scratch, M2I_ENTRY_LEN, 1))
      return;

    /* Write the actual file - always without P00 header */
    fat_open_write(path, dent, TYPE_RAW, buf, append);

    /* Abort on error */
    if (current_error) {
      /* No error checking here. Either it works or everything has failed. */
      ops_scratch[0] = '-';
      image_write(path->part, offset, ops_scratch, 1, 1);
    }
  }
}

static void m2i_open_rel(path_t *path, cbmdirent_t *dent, buffer_t *buf, uint8_t length, uint8_t mode) {
  set_error(ERROR_SYNTAX_UNABLE);
}

static uint8_t m2i_delete(path_t *path, cbmdirent_t *dent) {
  uint16_t offset;

  offset = dent->pvt.m2i.offset;// find_entry
  if (load_entry(path->part, offset))
    return 255;

  /* Ignore the result, we'll have to delete the entry anyway */
  ustrcpy(dent->name, ops_scratch + M2I_FATNAME_OFFSET);
  fat_delete(path, dent);

  ops_scratch[0] = '-';
  if (image_write(path->part, offset, ops_scratch, 1, 1))
    return 0;
  else
    return 1;
}

static void m2i_rename(path_t *path, cbmdirent_t *dent, uint8_t *newname) {
  uint16_t offset;
  uint8_t *ptr;

  set_busy_led(1);
  /* Locate entry in the M2I file */
  offset = dent->pvt.m2i.offset;
  if (load_entry(path->part, offset)) {
    update_leds();
    return;
  }

  /* Re-load the entry because load_entry modifies it */
  /* Assume this never fails because load_entry was successful */
  image_read(path->part, offset, ops_scratch, M2I_ENTRY_LEN);

  /* Copy the new filename */
  ptr = ops_scratch + M2I_CBMNAME_OFFSET;
  memset(ptr, ' ', CBM_NAME_LENGTH);
  while (*newname)
    *ptr++ = *newname++;

  /* Write new entry */
  image_write(path->part, offset, ops_scratch, M2I_ENTRY_LEN, 1);

  update_leds();
}

const PROGMEM fileops_t m2iops = {
  m2i_open_read,
  m2i_open_write,
  m2i_open_rel,
  m2i_delete,
  m2i_getdisklabel,
  m2i_getdirlabel,
  fat_getid,
  fat_freeblocks,
  fat_read_sector,
  fat_write_sector,
  format_dummy,
  m2i_opendir,
  m2i_readdir,
  image_mkdir,
  image_chdir,
  m2i_rename
};
