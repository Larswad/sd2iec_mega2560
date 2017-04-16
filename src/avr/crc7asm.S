/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN, see tff.c|h.

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


   crc7asm.S: space-efficient CRC7 implementation for AVR

*/

        .section .text

        .global crc7update
        ;; uint8_t crc7update(uint8_t crc, uint8_t data)
        ;;
        ;; input : r24: crc, r22: data
        ;; output: r24: new crc
crc7update:
        ldi     r18, 8       ; number of bits to process
        ldi     r19, 0x09    ; CRC7 polynomial
        ldi     r20, 0x80    ; constant for inverting the top bit of the CRC

loop:   lsl     r24          ; shift CRC
        lsl     r22          ; shift data byte
        brcc    0f           ; jump if top data bit was 0
        eor     r24, r20     ; invert top bit of CRC if not
0:      bst     r24, 7       ; read top bit of CRC
        brtc    1f           ; skip if top bit of CRC is now clear
        eor     r24, r19     ; apply polynomial
1:      dec     r18          ; decrement bit counter
        brne    loop         ; loop for next bit
        andi    r24, 0x7f    ; clear top bit of CRC
        ret                  ; return

        .end
