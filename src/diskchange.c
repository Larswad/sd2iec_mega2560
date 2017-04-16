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


   diskchange.c: Disk image changer

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "display.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fatops.h"
#include "flags.h"
#include "ff.h"
#include "led.h"
#include "parser.h"
#include "progmem.h"
#include "timer.h"
#include "utils.h"
#include "ustring.h"
#include "diskchange.h"

static const char PROGMEM autoswap_lst_name[] = "AUTOSWAP.LST";
static const char PROGMEM autoswap_gen_name[] = "AUTOSWAP.GEN"; // FIXME: must be 15 chars or less
static const char PROGMEM petscii_marker[8]   = "#PETSCII";

static FIL     swaplist;
static path_t  swappath;
static uint8_t linenum;

#define BLINK_BACKWARD 1
#define BLINK_FORWARD  2
#define BLINK_HOME     3

static void confirm_blink(uint8_t type) {
  uint8_t i;

  for (i=0;i<2;i++) {
    tick_t targettime;

#ifdef SINGLE_LED
    set_dirty_led(1);
#else
    if (!i || type & 1)
      set_dirty_led(1);
    if (!i || type & 2)
      set_busy_led(1);
#endif
    targettime = ticks + MS_TO_TICKS(100);
    while (time_before(ticks,targettime)) ;

    set_dirty_led(0);
    set_busy_led(0);
    targettime = ticks + MS_TO_TICKS(100);
    while (time_before(ticks,targettime)) ;
  }
}

static uint8_t mount_line(void) {
  FRESULT res;
  UINT bytesread;
  uint8_t i,*str,*strend, *buffer_start;
  uint16_t curpos;
  bool got_colon = false;
  uint8_t olderror = current_error;
  current_error = ERROR_OK;

  /* Kill all buffers */
  free_multiple_buffers(FMB_USER_CLEAN);

  curpos = 0;
  strend = NULL;
  buffer_start = command_buffer + 1;
  globalflags |= SWAPLIST_ASCII;

  for (i=0;i<=linenum;i++) {
    str = buffer_start;

    res = f_lseek(&swaplist,curpos);
    if (res != FR_OK) {
      parse_error(res,1);
      return 0;
    }

    res = f_read(&swaplist, str, CONFIG_COMMAND_BUFFER_SIZE - 1, &bytesread);
    if (res != FR_OK) {
      parse_error(res,1);
      return 0;
    }

    /* Terminate string in buffer */
    if (bytesread < CONFIG_COMMAND_BUFFER_SIZE - 1)
      str[bytesread] = 0;

    if (bytesread == 0) {
      if (linenum == 255) {
        /* Last entry requested, found it */
        linenum = i-1;
      } else {
        /* End of file - restart loop to read the first entry */
        linenum = 0;
      }
      i = -1; /* I could've used goto instead... */
      curpos = 0;
      continue;
    }

    /* Skip name */
    got_colon = false;

    while (*str != '\r' && *str != '\n') {
      if (*str == ':')
        got_colon = true;
      str++;
    }

    strend = str;

    /* Skip line terminator */
    while (*str == '\r' || *str == '\n') str++;

    /* check for PETSCII marker */
    if (curpos == 0) {
      if (!memcmp_P(buffer_start, petscii_marker, sizeof(petscii_marker))) {
        /* swaplist is in PETSCII, ignore this line */
        globalflags &= ~SWAPLIST_ASCII;
        i--;
      }
    }

    curpos += str - buffer_start;
  }

  /* Terminate file name */
  *strend = 0;

  if (partition[swappath.part].fop != &fatops)
    image_unmount(swappath.part);

  /* Start in the partition+directory of the swap list */
  current_part = swappath.part;
  display_current_part(current_part);
  partition[current_part].current_dir = swappath.dir;

  /* add a colon if neccessary */
  if (!got_colon && buffer_start[0] != '/') {
    command_buffer[0] = ':';
    buffer_start = command_buffer;
  }

  /* recode entry if neccessary */
  if (globalflags & SWAPLIST_ASCII)
    asc2pet(buffer_start);

  /* parse and change */
  do_chdir(buffer_start);

  if (current_error != 0 && current_error != ERROR_DOSVERSION) {
    current_error = olderror;
    return 0;
  }

  return 1;
}

/**
 * create_changelist - create a swap list in a directory
 * @path    : path where the swap list should be created
 * @filename: name of the swap list file
 *
 * This function creates a swap list in @path by scanning that
 * directory and writing the names of all disk images it finds
 * into @filename. Returns nonzero if at least one image was found.
 */
static uint8_t create_changelist(path_t *path, uint8_t *filename) {
  FRESULT res;
  FILINFO finfo;
  DIR dh;
  FIL fh;
  UINT byteswritten;
  uint8_t *name;
  uint8_t found = 0;

  /* open directory */
  res = l_opendir(&partition[path->part].fatfs, path->dir.fat, &dh);
  if (res != FR_OK)
    return 0;

  /* open file */
  res = f_open(&partition[path->part].fatfs, &fh, filename, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK)
    return 0;

  /* scan directory */
  set_busy_led(1);
  finfo.lfn = ops_scratch;

  while (1) {
    res = f_readdir(&dh, &finfo);
    if (res != FR_OK)
      break;

    if (finfo.fname[0] == 0)
      break;

    if (!(finfo.fattrib & AM_DIR)) {
      if (check_imageext(finfo.fname) == IMG_IS_DISK) {
        /* write the name of disk image to file */
        found = 1;

        if (ops_scratch[0] != 0)
          name = ops_scratch;
        else
          name = finfo.fname;

        res = f_write(&fh, name, ustrlen(name), &byteswritten);
        if (res != FR_OK || byteswritten == 0)
          break;

        /* add line terminator */
        finfo.fname[0] = 0x0d;
        finfo.fname[1] = 0x0a;
        res = f_write(&fh, finfo.fname, 2, &byteswritten);
        if (res != FR_OK || byteswritten == 0)
          break;
      }
    }
  }

  f_close(&fh);

  set_busy_led(0);

  return found;
}

static void set_changelist_internal(path_t *path, uint8_t *filename, uint8_t at_end) {
  FRESULT res;

  /* Assume this isn't the auto-swap list */
  globalflags &= (uint8_t)~AUTOSWAP_ACTIVE;

  /* Remove the old swaplist */
  if (swaplist.fs != NULL) {
    f_close(&swaplist);
    memset(&swaplist,0,sizeof(swaplist));
  }

  if (ustrlen(filename) == 0)
    return;

  /* Open a new swaplist */
  partition[path->part].fatfs.curr_dir = path->dir.fat;
  res = f_open(&partition[path->part].fatfs, &swaplist, filename, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    return;
  }

  /* Remember its directory so relative paths work */
  swappath = *path;

  if (at_end)
    linenum = 255;
  else
    linenum = 0;

  if (mount_line())
    confirm_blink(BLINK_HOME);
}

void set_changelist(path_t *path, uint8_t *filename) {
  set_changelist_internal(path, filename, 0);
}

void change_disk(void) {
  path_t path;

  if (swaplist.fs == NULL) {
    /* No swaplist active, try using AUTOSWAP.LST */
    /* change_disk is called from the IEC idle loop, so ops_scratch is free */
    ustrcpy_P(ops_scratch, autoswap_lst_name);
    path.dir  = partition[current_part].current_dir;
    path.part = current_part;
    if (key_pressed(KEY_PREV))
      set_changelist_internal(&path, ops_scratch, 1);
    else
      set_changelist_internal(&path, ops_scratch, 0);

    if (swaplist.fs == NULL) {
      /* No swap list found, create one if key was "home" */
      if (key_pressed(KEY_HOME)) {
        uint8_t swapname[16]; // FIXME: magic constant

        ustrcpy_P(swapname, autoswap_gen_name);

        if (create_changelist(&path, swapname)) {
          set_changelist_internal(&path, swapname, 0);
          globalflags |= AUTOSWAP_ACTIVE;
        }
      }

      /* reset error and exit */
      set_error(ERROR_OK);
      reset_key(0xff); // <- lazy
      return;
    } else {
      /* Autoswaplist found, mark it as active                */
      /* and exit because the first image is already mounted. */
      globalflags |= AUTOSWAP_ACTIVE;
      reset_key(0xff); // <- lazy
      return;
    }
  }

  /* Mount the next image in the list */
  if (key_pressed(KEY_NEXT)) {
    linenum++;
    reset_key(KEY_NEXT);
    if (mount_line())
      confirm_blink(BLINK_FORWARD);
  } else if (key_pressed(KEY_PREV)) {
    linenum--;
    reset_key(KEY_PREV);
    if (mount_line())
      confirm_blink(BLINK_BACKWARD);
  } else if (key_pressed(KEY_HOME)) {
    linenum = 0;
    reset_key(KEY_HOME);
    if (mount_line())
      confirm_blink(BLINK_HOME);
  }
}

void change_init(void) {
  memset(&swaplist,0,sizeof(swaplist));
  globalflags &= (uint8_t)~AUTOSWAP_ACTIVE;
}
