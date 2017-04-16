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


   display.h: Remote display interface

*/

#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef CONFIG_REMOTE_DISPLAY

extern uint8_t display_found;

#include "i2c.h"

void display_send_prefixed(uint8_t cmd, uint8_t prefixbyte, uint8_t len, const uint8_t *buffer);
uint8_t display_init(uint8_t len, uint8_t *message);
void display_service(void);
void display_send_cmd(uint8_t cmd, uint8_t len, const void *buf);
void display_send_cmd_byte(uint8_t cmd, uint8_t val);

void display_filename_write(uint8_t part, uint8_t len, const unsigned char* buf);
void display_menu_show(uint8_t start);
void display_address(uint8_t dev);
void display_current_part(uint8_t part);
void display_menu_add(const unsigned char* string);
void display_menu_reset(void);
void display_current_directory(uint8_t part, const unsigned char* name);

#else // CONFIG_REMOTE_DISPLAY

# define display_found 0
# define display_send_prefixed(a,b,c,d) do { } while (0)
# define display_init(a,b)              0
# define display_service()              do {} while (0)
# define display_send_cmd(cmd,len,buf)  do {} while (0)
# define display_send_cmd_byte(cmd,v)   do {} while (0)

# define display_filename_write(a,b,c)  do {} while (0)
# define display_menu_show(a)           do {} while (0)
# define display_address(a)             do {} while (0)
# define display_current_part(a)        do {} while (0)
# define display_menu_add(a)            do {} while (0)
# define display_menu_reset()           do {} while (0)
# define display_current_directory(a,b) do {} while (0)

#endif // CONFIG_REMOTE_DISPLAY

#define DISPLAY_I2C_ADDR 0x64

// Note: All partitions are 0-based internal partiton numbers!
enum display_commands {
  DISPLAY_INIT = 0,          // PETSCII version string
  DISPLAY_ADDRESS,           // uint8_t device address
  DISPLAY_FILENAME_READ,     // uint8_t part, PETSCII file name
  DISPLAY_FILENAME_WRITE,    // uint8_t part, PETSCII file name
  DISPLAY_DOSCOMMAND,        // PETSCII command string
  DISPLAY_ERRORCHANNEL,      // PETSCII error channel message
  DISPLAY_CURRENT_DIR,       // uint8_t part, PETSCII directory name
  DISPLAY_CURRENT_PART,      // uint8_t part
  DISPLAY_MENU_RESET = 0x40, // -
  DISPLAY_MENU_ADD,          // zero-terminated PETSCII string
  DISPLAY_MENU_SHOW,         // uint8_t startentry
  DISPLAY_MENU_GETSELECTION, // returns the number of the selected menu entry
  DISPLAY_MENU_GETENTRY,     // returns the text of the selected menu entry
};

#define display_filename_read(part,len,buf)     display_send_prefixed(DISPLAY_FILENAME_READ,part,len,buf)
#define display_doscommand(len,buf)             display_send_cmd(DISPLAY_DOSCOMMAND,len,buf)
#define display_errorchannel(len,buf)           display_send_cmd(DISPLAY_ERRORCHANNEL,len,buf)

#endif // DISPLAY_H
