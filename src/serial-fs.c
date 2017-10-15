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

#include "config.h"
#include <alloca.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "buffers.h"
#include "serial-fs.h"
#include "softrtc.h"
#include "timer.h"
#include "uart.h"

/* ------------------------------------------------------------------------- */
/*  data structures, constants, global variables                             */
/* ------------------------------------------------------------------------- */
#define CLOCK_PRESCALE_FACTOR 1
static uint8_t txbuf[1 << CONFIG_UART_BUF_SHIFT];
static volatile uint16_t uartReadIx;
static volatile uint16_t uartWriteIx;

#define DGRAM_MAGIC0 'S'
#define DGRAM_MAGIC1 'F'

typedef enum {
	miHandshakeReq,
	miHandshakeCfm,
	miHandshakeRej,

	miFileOpenReq,
	miFileOpenCfm,
	miFileOpenRej,

	miFileCloseReq,
	miFileCloseCfm,
	miFileCloseRej
} SFSMsgId;


typedef struct tagSFSDGram {
	struct tagHeader {
		uint8_t ident[2]; // must contain DGRAM_MAGIC (for re-sync. in case data transfer has errors).
		uint8_t type;		// message content. For some messages the length field may be omitted since it is not needed.
		uint8_t chksum;		// really a pad, but may be used for checksum.
		uint16_t length;	// length of this complete message including header.
	} Header;
	union {
		struct {
			struct tm tm;
			uint8_t numPartitions;
			uint8_t pad0;
		} HandshakeCfm;
		struct {
			uint8_t name[1];			// name of file to open.
			// TODO: more to add here for this command.
		} OpenReq;
		struct {
			uint8_t handle;			// a virtual handle identifier (the host side know how to map it to a real handle).
			// TODO: more to add here for this command.
		} OpenResp;
		struct {							// used for any operation that contains a handle.
			uint8_t handle;
		} HandleOp;
		struct {	// some operation went wrong
			uint8_t error;			// The error that occurred. See SFS_ERROR definitions.
		} OpError;
	} U;
} SFSDGramT;



/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

// UART handling.

ISR(SFS_USART_UDRE_vect)
{
	if(uartReadIx == uartWriteIx)
		return;

	SFS_UDR = txbuf[uartReadIx];
	uartReadIx = (uartReadIx + 1) bitand (sizeof(txbuf) - 1);
	if(uartReadIx == uartWriteIx)
		SFS_UCSRB and_eq compl _BV(SFS_UDRIE);
}

static void uartWriteByte(uint8_t c)
{
	uint16_t t = (uartWriteIx + 1) bitand (sizeof(txbuf) - 1);
#ifndef CONFIG_DEADLOCK_ME_HARDER // :-)
	SFS_UCSRB and_eq compl _BV(SFS_UDRIE);   // turn off RS232 irq
#else
	while(t == read_idx);   // wait for free space
#endif
	txbuf[uartWriteIx] = c;
	uartWriteIx = t;
	//if (read_idx == write_idx) PORTD or_eq _BV(PD7);
	SFS_UCSRB or_eq _BV(SFS_UDRIE);
}

// 0 in length here means 0x10000 !
static void uartWriteBuf(uint8_t* buf, uint16_t length)
{
	do uartWriteByte(*buf++); while(--length);
}

// 100 ticks per second
static bool uartReadByte(uint8_t* byte, uint8_t timeout)
{
	tick_t end = getticks() + timeout;
	while(bit_is_clear(SFS_UCSRA, SFS_RXC))	{
		if(time_after(getticks(), end))
			return false; // we timed out!
	}
	*byte = SFS_UDR;
	return true;
}

// 100 ticks per second
static bool uartReadBuf(uint8_t* buf, uint16_t length, uint8_t timeout)
{
	do {
		// timeout restarts after every loop.
		tick_t end = getticks() + timeout;
		while(bit_is_clear(SFS_UCSRA, SFS_RXC)) {
			if(time_after(getticks(), end))
				return false; // we timed out!
		}
		*buf++ = SFS_UDR;
	} while(--length);

	return true;
}

static void uartFlush(void)
{
	while(uartReadIx not_eq uartWriteIx);
}

static SFSDGramT s_outDGram;
static SFSDGramT s_inDGram;

static void uartInit(void)
{
	// Configure the serial port for sfs channel.

	//  SFS_UBRRH = (int)((double)F_CPU/(16.0*CONFIG_UART_BAUDRATE) - 1) >> 8;
	//  SFS_UBRRL = (int)((double)F_CPU/(16.0*CONFIG_UART_BAUDRATE) - 1) bitand 0xff;
	uint32_t clock = F_CPU;
#ifdef CLOCK_PRESCALE_FACTOR
	clock /= CLOCK_PRESCALE_FACTOR;
#endif
	uint16_t baudSetting = (clock / 4 / CONFIG_UART_BAUDRATE - 1) / 2;
	SFS_UBRRH = baudSetting >> 8;
	SFS_UBRRL = baudSetting bitand 0xff;
	SFS_UCSRA = _BV(U2X2);

	SFS_UCSRB = _BV(SFS_RXEN) | _BV(SFS_TXEN);
	// I really don't like random #ifdefs in the code =(
#if defined __AVR_ATmega32__
	SFS_UCSRC = _BV(SFS_URSEL) | _BV(SFS_UCSZ1) | _BV(SFS_UCSZ0);
#else
	SFS_UCSRC = _BV(SFS_UCSZ1) | _BV(SFS_UCSZ0);
#endif

	//SFS_UCSRB or_eq _BV(SFS_UDRIE);
	uartReadIx  = uartWriteIx = 0;

	s_outDGram.Header.ident[0] = s_inDGram.Header.ident[0] = DGRAM_MAGIC0;
	s_outDGram.Header.ident[1] = s_inDGram.Header.ident[1] = DGRAM_MAGIC1;
}

// The timeout is given in multiples of 10 ms (0 = don't wait for answer, means null is always returned).
// if the alternateSource pointer is given, the written data (payload) will be transferred from separate memory location.
// if the alternateDest pointer is given, the read answer (payload) will be placed in that separate memory location.
static SFSDGramT* intSFSRequest(SFSDGramT* req, uint8_t timeout, uint8_t* alternateSource, uint8_t* alternateDest)
{
	// TODO: We have cpu-time for some checksumming of outgoing?

	uartWriteBuf((uint8_t*)req, alternateSource ? sizeof(req->Header) : req->Header.length);
	uartFlush();
	if(alternateSource)
		uartWriteBuf(alternateSource, req->Header.length - sizeof(req->Header));
	uartFlush();
	if(0 == timeout)
		return NULL;

	// wait for an answer with the given timeout
	uint8_t ident[2] = { 0 };
	do {
		if(not uartReadByte(&ident[1], timeout))
			return NULL; // timeout
		if(ident[0] not_eq DGRAM_MAGIC0 or ident[1] not_eq DGRAM_MAGIC1)
			ident[0] = ident[1];
	} while(ident[0] not_eq DGRAM_MAGIC0 or ident[1] not_eq DGRAM_MAGIC1);

	// read rest of header.
	if(not uartReadBuf((uint8_t*)&s_inDGram.Header.type, sizeof(req->Header) - sizeof(ident), timeout))
		return NULL; // timeout

	if(req->Header.length > sizeof(SFSDGramT) or req->Header.length < sizeof(req->Header)) // weird! go searching / sync, handle this!
		return NULL;

	if(req->Header.length > sizeof(req->Header.length)) {
		// read rest of message.
		if(not uartReadBuf(alternateDest ? alternateDest : (uint8_t*)(&s_inDGram.U), req->Header.length - sizeof(req->Header), timeout))
			return NULL; // timeout
	}

	// TODO: We have time for some incoming checksum check?

	return &s_inDGram;
}

inline static SFSDGramT* sfsRequest(SFSDGramT* req, uint8_t timeout)
{
	return intSFSRequest(req, timeout, NULL, NULL);
}

inline static SFSDGramT* sfsRequestSourcePtr(SFSDGramT* req, uint8_t timeout, uint8_t* dataPtr)
{
	return intSFSRequest(req, timeout, dataPtr, NULL);
}


inline static SFSDGramT* sfsRequestDestPtr(SFSDGramT* req, uint8_t timeout, uint8_t* dataPtr)
{
	return intSFSRequest(req, timeout, NULL, dataPtr);
}

static SFSDGramT* sfsReqDGram(uint8_t type, uint8_t size)
{
	s_outDGram.Header.type = type;
	s_outDGram.Header.length = size + sizeof(s_outDGram.Header);
	return &s_outDGram;
}


/* ------------------------------------------------------------------------- */
/*  external API                                                             */
/* ------------------------------------------------------------------------- */

bool serialfs_init(void)
{
	uart_puts_P(PSTR("entry: serialfs_init\n"));
	uartInit();

	SFSDGramT* resp = sfsRequest(sfsReqDGram(miHandshakeReq, 0), HZ / 2);
	if(NULL not_eq resp and miHandshakeCfm == resp->Header.type) {
		// we sync our clock to the host time.
		softrtc_set(&resp->U.HandshakeCfm.tm);
		uart_puts_P(PSTR("exit: serialfs_init_success\n"));
		return true;
	}
	uart_puts_P(PSTR("exit: serialfs_init_fail\n"));
	return false;
}


void serialfs_opendir(sfs_dir_t *dh)
{
	VAR_UNUSED(dh);
	uart_puts_P(PSTR("entry: serialfs_opendir"));
//	dh->entry = 0;
}

/**
 * serialfs_readdir - read a directory entry
 * @dh   : pointer to directory handle
 * @entry: pointer to dirent structore for the result
 *
 * This function reads the next entry from the directory
 * handle @dh to the dirent structure @entry. Returns
 * zero if an entry was returned or non-zero if there
 * was no more entry to read.
 */
uint8_t serialfs_readdir(sfs_dir_t *dh, sfs_dirent_t *entry)
{
	uart_puts_P(PSTR("entry: serialfs_readdir"));
	VAR_UNUSED(dh);
	VAR_UNUSED(entry);
#if 0
	/* loop until a valid new entry is found */
	while (dh->entry < EEPROMFS_ENTRIES) {
		/* read current entry */
		read_entry(dh->entry++);

		if (nameptr->flags & EEFS_FLAG_DELETED)
			continue;

		if (curentry_is_listentry())
			continue;

		// copy information
		memcpy(entry->name, nameptr->name, EEFS_NAME_LENGTH);
		entry->flags = nameptr->flags;
		entry->size  = nameptr->size;
		return 0;
	}
#endif

	return 1;
}

/**
 * serialfs_open - open a file
 * @name : pointer to file name
 * @fh   : pointer to file handle
 * @flags: access flags
 *
 * This function sets up the handle give via @fh for access to the
 * file named in @name. Depending on the settings in @flags this
 * can be a read, write or append-access. Returns zero if successful,
 * non-zero if not.
 */
sfs_error_t serialfs_open(uint8_t* name, sfs_fh_t* fh, uint8_t flags)
{
	uart_puts_P(PSTR("entry: serialfs_open"));
	VAR_UNUSED(name);
	VAR_UNUSED(fh);
	VAR_UNUSED(flags);
#if 0
	uint8_t diridx;

	memset(fh, 0, sizeof(eefs_fh_t));

	diridx = find_entry(name);

	if (flags == EEFS_MODE_WRITE) {
		/* write: forbid multiple files with the same name */
		if (diridx != 0xff)
			return EEFS_ERROR_FILEEXISTS;

		/* allocate directory entry */
		fh->entry = next_free_entry(0);
		if (fh->entry == 0xff)
			return EEFS_ERROR_DIRFULL;

		fh->cur_entry  = fh->entry;
		fh->cur_sindex = (uint8_t)-1;
		fh->filemode   = EEFS_MODE_WRITE;

		/* create direntry */
		memset(EEPROMFS_BUFFER, 0xff, sizeof(nameentry_t));
		memcpy(nameptr->name, name, EEFS_NAME_LENGTH);
		nameptr->size  = 0;
		nameptr->flags = 0;
		write_entry(fh->entry);

	} else {
		/* read, append: return error if the file does not exist */
		if (diridx == 0xff)
			return SFS_ERROR_FILENOTFOUND;

		fh->entry = diridx;
		fh->size  = nameptr->size;

		if (flags == EEFS_MODE_APPEND) {
			/* load last direntry */
			while (listptr->nextentry != 0xff) {
				diridx = listptr->nextentry;
				read_entry(diridx);
			}

			/* search for last allocated sector */
			uint8_t sectoridx = 0;
			for (uint8_t i = 0; i < curentry_max_sectors(); i++) {
				if (listptr->sectors[i] == SECTOR_FREE)
					break;

				sectoridx = i;
			}

			fh->cur_sector  = listptr->sectors[sectoridx];
			fh->cur_entry   = diridx;
			fh->cur_offset  = fh->size;
			fh->cur_soffset = fh->size % EEPROMFS_SECTORSIZE;
			fh->filemode    = EEFS_MODE_WRITE;

		} else {
			/* read mode */
			fh->cur_sector = nameptr->sectors[0];
			fh->cur_entry  = diridx;
			fh->filemode   = EEFS_MODE_READ;
		}
	} // non-write
#endif
	return SFS_ERROR_OK;
}

/**
 * serialfs_write - write to a file
 * @fh           : pointer to file handle
 * @data         : pointer to data to be written
 * @length       : number of bytes to write
 * @bytes_written: pointer to output var for number of bytes written
 *
 * This function writes @length bytes from @data to the file opened on @fh.
 * The actual number of bytes written to the file is stored in
 * @bytes_written. Returns an serialfs error code depending on the
 * result.
 */
sfs_error_t serialfs_write(sfs_fh_t *fh, void *data, uint16_t length, uint16_t *bytes_written)
{
	uart_puts_P(PSTR("entry: serialfs_write"));
	VAR_UNUSED(fh);
	VAR_UNUSED(data);
	VAR_UNUSED(length);
	VAR_UNUSED(bytes_written);
#if 0
	uint8_t *bdata = data;

	if (fh->filemode != EEFS_MODE_WRITE)
		return EEFS_ERROR_INVALID;

	*bytes_written = 0;

	while (length > 0) {
		if (fh->cur_soffset == 0) {
			/* need to allocate another sector */
			uint8_t next_sector = next_free_sector(fh->cur_sector);

			if (next_sector == SECTOR_FREE)
				return EEFS_ERROR_DISKFULL;

			fh->cur_sector = next_sector;

			/* mark it in the direntry */
			read_entry(fh->cur_entry);

			fh->cur_sindex++;
			if (fh->cur_sindex >= curentry_max_sectors()) {
				/* need to allocate another direntry */
				uint8_t next_entry = next_free_entry(fh->cur_entry);

				if (next_entry == 0xff)
					return EEFS_ERROR_DIRFULL;

				/* write link */
				listptr->nextentry = next_entry;
				write_entry(fh->cur_entry);

				/* build new entry in buffer */
				memset(listptr, 0xff, sizeof(listentry_t));
				listptr->flags = EEFS_FLAG_LISTENTRY;

				fh->cur_entry  = next_entry;
				fh->cur_sindex = 0;
			}

			/* mark sector as used in internal bookkeeping */
			mark_sector(next_sector, true);

			/* write new sector number to direntry */
			listptr->sectors[fh->cur_sindex] = next_sector;
			write_entry(fh->cur_entry);
		}

		uint8_t bytes_to_write = min(length, EEPROMFS_SECTORSIZE - fh->cur_soffset);
		eeprom_write_block(bdata,
											 (uint8_t *)(DATA_OFFSET + EEPROMFS_SECTORSIZE * fh->cur_sector + fh->cur_soffset),
											 bytes_to_write);

		/* adjust state */
		bdata           += bytes_to_write;
		fh->size        += bytes_to_write;
		fh->cur_offset  += bytes_to_write;
		fh->cur_soffset  = (fh->cur_soffset + bytes_to_write) % EEPROMFS_SECTORSIZE;
		length          -= bytes_to_write;
		*bytes_written  += bytes_to_write;
	}
#endif
	return SFS_ERROR_OK;
}

/**
 * serialfs_read - read from a file
 * @fh        : pointer to file handle
 * @data      : pointer to buffer for data to be read
 * @length    : number of bytes to read
 * @bytes_read: pointer to output var for number of bytes read
 *
 * This function writes @length bytes from @data to the file opened on @fh.
 * The actual number of bytes written to the file is stored in
 * @bytes_written. Returns an serialfs error code depending on the
 * result.
 */
sfs_error_t serialfs_read(sfs_fh_t *fh, void *data, uint16_t length, uint16_t *bytes_read)
{
	uart_puts_P(PSTR("entry: serialfs_read"));
	VAR_UNUSED(fh);
	VAR_UNUSED(data);
	VAR_UNUSED(length);
	VAR_UNUSED(bytes_read);
#if 0
	uint8_t *bdata = data;

	if (fh->filemode != EEFS_MODE_READ)
		return EEFS_ERROR_INVALID;

	*bytes_read = 0;

	if (length > fh->size - fh->cur_offset)
		length = fh->size - fh->cur_offset;

	while (length > 0) {
		uint8_t bytes_to_read = min(length, EEPROMFS_SECTORSIZE - fh->cur_soffset);

		eeprom_read_block(bdata,
											(uint8_t *)(DATA_OFFSET + EEPROMFS_SECTORSIZE * fh->cur_sector + fh->cur_soffset),
											bytes_to_read);

		/* adjust state */
		*bytes_read    += bytes_to_read;
		bdata          += bytes_to_read;
		length         -= bytes_to_read;
		fh->cur_offset += bytes_to_read;
		fh->cur_soffset = (fh->cur_soffset + bytes_to_read) % EEPROMFS_SECTORSIZE;

		if (fh->cur_offset != fh->size && fh->cur_soffset == 0) {
			// FIXME: If rw-mode-support is added, this code generates a state that
			//        differs from the assumptions in the write path
			//        (current sector advances here, write assumes it hasn't)
			/* move to next sector */
			read_entry(fh->cur_entry);
			fh->cur_sindex++;

			if (fh->cur_sindex >= curentry_max_sectors()) {
				/* all sectors within current entry are processed */
				assert(listptr->nextentry != 0xff);

				fh->cur_entry  = listptr->nextentry;
				fh->cur_sindex = 0;
				read_entry(fh->cur_entry);
			}

			assert(listptr->sectors[fh->cur_sindex] != SECTOR_FREE);

			fh->cur_sector = listptr->sectors[fh->cur_sindex];
		}
	}
#endif
	return SFS_ERROR_OK;
}

/**
 * serialfs_close - close a file
 * @fh: handle of the file to close
 *
 * This function closes the file opened on @fh.
 * No return value.
 */
void serialfs_close(sfs_fh_t *fh)
{
	uart_puts_P(PSTR("entry: serialfs_close"));
	VAR_UNUSED(fh);
#if 0
	// FIXME: Read mode check could be removed if write_entry only writes changed bytes (EEPROMFS_MINIMIZE_WRITES)
	if (fh->filemode != EEFS_MODE_READ) {
		/* write new file length */
		read_entry(fh->entry);
		nameptr->size = fh->size;
		write_entry(fh->entry);
	}
	fh->filemode = 0;
#endif
}

/**
 * serialfs_rename - rename a file
 * @oldname: current file name
 * @newname: new file name
 *
 * This function renames the file @oldname to @newname.
 * Returns an serialfs error code depending on the result.
 */
sfs_error_t serialfs_rename(uint8_t* oldname, uint8_t* newname)
{
	uart_puts_P(PSTR("entry: serialfs_rename"));
	VAR_UNUSED(oldname);
	VAR_UNUSED(newname);
#if 0
	uint8_t diridx;

	/* check if the new file name already exists */
	diridx = find_entry(newname);

	if (diridx != 0xff)
		return SFS_ERROR_FILEEXISTS;

	/* read the directory entry */
	diridx = find_entry(oldname);

	if (diridx == 0xff)
		return SFS_ERROR_FILENOTFOUND;

	memcpy(nameptr->name, newname, EEFS_NAME_LENGTH);

	write_entry(diridx);
#endif
	return SFS_ERROR_OK;
}


/**
 * serialfs_delete - delete a file
 * @name: name of the file to delete
 *
 * This function deletes the file @name.
 * Returns an serialfs error code depending on the result.
 */
sfs_error_t serialfs_delete(uint8_t* name)
{
	uart_puts_P(PSTR("entry: serialfs_delete"));
	VAR_UNUSED(name);
#if 0
	uint8_t diridx, old_diridx;

	/* look up the first directory entry */
	diridx = find_entry(name);
	if (diridx == 0xff)
		return SFS_ERROR_FILENOTFOUND;

	// TODO: Implement
#endif
	return SFS_ERROR_OK;
}
