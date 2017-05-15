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


	 arch-config.h: The main architecture-specific config header

*/

#ifndef ARCH_CONFIG_H
#define ARCH_CONFIG_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>

/* Include avrcompat.h to get the PA0..PD7 macros on 1284P */
#include "avrcompat.h"
#include "utils.h"

/* ----- Common definitions for all AVR hardware variants ------ */

/* Return value of buttons_read() */
typedef uint8_t rawbutton_t;

/* Interrupt handler for system tick */
#define SYSTEM_TICK_HANDLER ISR(TIMER1_COMPA_vect)

/* EEPROMFS: offset and size must be multiples of 4 */
/* to actually enable it, CONFIG_HAVE_EEPROMFS must be set in config */
#  define EEPROMFS_OFFSET     512
#  define EEPROMFS_SIZE       3584
#  define EEPROMFS_ENTRIES    8
#  define EEPROMFS_SECTORSIZE 64


#if CONFIG_HARDWARE_VARIANT==1
/* ---------- Hardware configuration: Example ---------- */
/* This is a commented example for most of the available options    */
/* in case someone wants to build Yet Another[tm] hardware variant. */
/* Some of the values are chosen randomly, so this variant is not   */
/* expected to compile successfully.                                */

/*** SD card support ***/
/* If your device supports SD cards by default, define this symbol. */
#  define HAVE_SD

/* Declaration of the interrupt handler for SD card change */
#  define SD_CHANGE_HANDLER ISR(INT0_vect)

/* Declaration of the interrupt handler for SD card 2 change */
#  define SD2_CHANGE_HANDLER ISR(INT9_vect)

/* Initialize all pins and interrupts related to SD - except SPI */
static inline void sdcard_interface_init(void) {
	/* card detect (SD1) */
	DDRD  &= ~_BV(PD2);
	PORTD |=  _BV(PD2);
	/* write protect (SD1) */
	DDRD &= ~_BV(PD6);
	PORTD |= _BV(PD6);
	/* card change interrupt (SD1) */
	EICRA |= _BV(ISC00);
	EIMSK |= _BV(INT0);
	// Note: Wrapping SD2 in CONFIG_TWINSD may be a good idea
	/* chip select (SD2) */
	PORTD |= _BV(PD4);
	DDRD |= _BV(PD4);
	/* card detect (SD2) */
	DDRD &= ~_BV(PD3);
	PORTD |= _BV(PD3);
	/* write protect (SD2) */
	DDRD &= ~_BV(PD7);
	PORTD |= _BV(PD7);
	/* card change interrupt (SD2) */
	EICRA |=  _BV(ISC90); // Change interrupt
	EIMSK |=  _BV(INT9);  // Change interrupt
}

/* sdcard_detect() must return non-zero while a card is inserted */
/* This must be a pin capable of generating interrupts.          */
static inline uint8_t sdcard_detect(void) {
	return !(PIND & _BV(PD2));
}

/* Returns non-zero when the currently inserted card is write-protected */
static inline uint8_t sdcard_wp(void) {
	return PIND & _BV(PD6);
}

/* Support for a second SD card - use CONFIG_TWINSD=y in your config file to enable! */
/* Same as the two functions above, but for card 2 */
static inline uint8_t sdcard2_detect(void) {
	return !(PIND & _BV(PD3));
}
static inline uint8_t sdcard2_wp(void) {
	return PIND & _BV(PD7);
}

/* SD card 1 is assumed to use the standard SS pin   */
/* If that's not true, #define SDCARD_SS_SPECIAL and */
/* implement this function:                          */
//static inline __attribute__((always_inline)) void sdcard_set_ss(uint8_t state) {
//  if (state)
//    PORTZ |= _BV(PZ9);
//  else
//    PORTZ &= ~_BV(PZ9);
//}

/* SD card 2 CS pin */
static inline __attribute__((always_inline)) void sdcard2_set_ss(uint8_t state) {
	if (state)
		PORTD |= _BV(PD4);
	else
		PORTD &= ~_BV(PD4);
}

/* SD Card supply voltage - choose the one appropiate to your board */
/* #  define SD_SUPPLY_VOLTAGE (1L<<15)  / * 2.7V - 2.8V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<16)  / * 2.8V - 2.9V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<17)  / * 2.9V - 3.0V */
#  define SD_SUPPLY_VOLTAGE (1L<<18)  /* 3.0V - 3.1V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<19)  / * 3.1V - 3.2V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<20)  / * 3.2V - 3.3V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<21)  / * 3.3V - 3.4V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<22)  / * 3.4V - 3.5V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<23)  / * 3.5V - 3.6V */

/* SPI clock divisors - the slow one must be 400KHz or slower,        */
/* the fast one can be as high as you thing your hardware will handle */
#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4


/*** Device address selection ***/
/* device_hw_address() returns the hardware-selected device address */
static inline uint8_t device_hw_address(void) {
	return 8 + !(PIND & _BV(PD7)) + 2*!(PIND & _BV(PD5));
}

/* Configure hardware device address pins */
static inline void device_hw_address_init(void) {
	DDRD  &= ~(_BV(PD7) | _BV(PD5));
	PORTD |=   _BV(PD7) | _BV(PD5);
}


/*** LEDs ***/
/* Please don't build single-LED hardware anymore... */

/* Initialize ports for all LEDs */
static inline void leds_init(void) {
	/* Note: Depending on the chip and register these lines can compile */
	/*       to one instruction each on AVR. For two bits this is one   */
	/*       instruction shorter than "DDRC |= _BV(PC0) | _BV(PC1);"    */
	DDRC |= _BV(PC0);
	DDRC |= _BV(PC1);
}

/* --- "BUSY" led, recommended color: green (usage similiar to 1541 LED) --- */
static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC0);
	else
		PORTC &= ~_BV(PC0);
}

/* --- "DIRTY" led, recommended color: red (errors, unwritten data in memory) --- */
static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC1);
	else
		PORTC &= ~_BV(PC1);
}

/* Toggle function used for error blinking */
static inline void toggle_dirty_led(void) {
	/* Sufficiently new AVR cores have a toggle function */
	PINC |= _BV(PC1);
}


/*** IEC signals ***/
#  define IEC_INPUT PINA
#  define IEC_DDR   DDRA
#  define IEC_PORT  PORTA

/* Pins assigned for the IEC lines */
#  define IEC_PIN_ATN   PA0
#  define IEC_PIN_DATA  PA1
#  define IEC_PIN_CLOCK PA2
#  define IEC_PIN_SRQ   PA3

/* Use separate input/output lines?                                    */
/* The code assumes that the input is NOT inverted, but the output is. */
//#  define IEC_SEPARATE_OUT
//#  define IEC_OPIN_ATN   PA4
//#  define IEC_OPIN_DATA  PA5
//#  define IEC_OPIN_CLOCK PA6
//#  define IEC_OPIN_SRQ   PA7

/* You can use different ports for input and output bits. The code tries */
/* to not stomp on the unused bits. IEC output is on IEC_PORT.           */
/* Not well-tested yet.                                                  */
//#  define IEC_DDRIN      DDRX
//#  define IEC_DDROUT     DDRY
//#  define IEC_PORTIN     PORTX

/* ATN interrupt (required) */
#  define IEC_ATN_INT_VECT    PCINT0_vect
static inline void iec_interrupts_init(void) {
	PCMSK0 = _BV(PCINT0);
	PCIFR |= _BV(PCIF0);
}

/* CLK interrupt (not required) */
/* Dreamload requires interrupts for both the ATN and CLK lines. If both are served by */
/* the same PCINT vector, define that as ATN interrupt above and define IEC_PCMSK.     */
//#  define IEC_PCMSK             PCMSK0
/* If the CLK line has its own dedicated interrupt, use the following definitions: */
//#  define IEC_CLK_INT           INT5
//#  define IEC_CLK_INT_VECT      INT5_vect
//static inline void iec_clock_int_setup(void) {
//  EICRB |= _BV(ISC50);
//}


/*** IEEE signals ***/
/* not documented yet, look at petSD/XS-1541 for guidance */

/*** User interface ***/
/* Button NEXT changes to the next disk image and enables sleep mode (held) */
#  define BUTTON_NEXT _BV(PC4)

/* Button PREV changes to the previous disk image */
#  define BUTTON_PREV _BV(PC3)

/* Read the raw button state - a depressed button should read as 0 */
static inline rawbutton_t buttons_read(void) {
	return PINC & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
	DDRC  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTC |= BUTTON_NEXT | BUTTON_PREV;
}

/* Software I2C lines for the RTC and display */
#  define SOFTI2C_PORT    PORTC
#  define SOFTI2C_PIN     PINC
#  define SOFTI2C_DDR     DDRC
#  define SOFTI2C_BIT_SCL PC4
#  define SOFTI2C_BIT_SDA PC5
#  define SOFTI2C_DELAY   6


/*** board-specific initialisation ***/
/* Currently used on uIEC/CF and uIEC/SD only */
//#define HAVE_BOARD_INIT
//static inline void board_init(void) {
//  // turn on power LED
//  DDRG  |= _BV(PG1);
//  PORTG |= _BV(PG1);
//}


/* Pre-configurated hardware variants */

#elif CONFIG_HARDWARE_VARIANT==2
/* ---------- Hardware configuration: Shadowolf 1 ---------- */
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<18)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4

static inline void sdcard_interface_init(void) {
	DDRD &= ~_BV(PD2);
	PORTD |= _BV(PD2);
	DDRD &= ~_BV(PD6);
	PORTD |= _BV(PD6);
	EICRA |= _BV(ISC00);
	EIMSK |= _BV(INT0);
}

static inline uint8_t sdcard_detect(void) {
	return !(PIND & _BV(PD2));
}

static inline uint8_t sdcard_wp(void) {
	return PIND & _BV(PD6);
}

static inline uint8_t device_hw_address(void) {
	return 8 + !(PIND & _BV(PD7)) + 2*!(PIND & _BV(PD5));
}

static inline void device_hw_address_init(void) {
	DDRD  &= ~(_BV(PD7)|_BV(PD5));
	PORTD |=   _BV(PD7)|_BV(PD5);
}

static inline void leds_init(void) {
	DDRC |= _BV(PC0);
	DDRC |= _BV(PC1);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC0);
	else
		PORTC &= ~_BV(PC0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC1);
	else
		PORTC &= ~_BV(PC1);
}

static inline void toggle_dirty_led(void) {
	PINC |= _BV(PC1);
}

#  define IEC_INPUT             PINA
#  define IEC_DDR               DDRA
#  define IEC_PORT              PORTA
#  define IEC_PIN_ATN           PA0
#  define IEC_PIN_DATA          PA1
#  define IEC_PIN_CLOCK         PA2
#  define IEC_PIN_SRQ           PA3
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_PCMSK             PCMSK0

static inline void iec_interrupts_init(void) {
	PCICR |= _BV(PCIE0);
	PCIFR |= _BV(PCIF0);
}

#  define BUTTON_NEXT           _BV(PC4)
#  define BUTTON_PREV           _BV(PC3)

static inline rawbutton_t buttons_read(void) {
	return PINC & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
	DDRC  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTC |= BUTTON_NEXT | BUTTON_PREV;
}


#elif CONFIG_HARDWARE_VARIANT == 3
/* ---------- Hardware configuration: LarsP ---------- */
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4

static inline void sdcard_interface_init(void) {
	DDRD  &= ~_BV(PD2);
	PORTD |=  _BV(PD2);
	DDRD  &= ~_BV(PD6);
	PORTD |=  _BV(PD6);
	EICRA |=  _BV(ISC00);
	EIMSK |=  _BV(INT0);
}

static inline uint8_t sdcard_detect(void) {
	return !(PIND & _BV(PD2));
}

static inline uint8_t sdcard_wp(void) {
	return PIND & _BV(PD6);
}

static inline uint8_t device_hw_address(void) {
	return 8 + !(PINA & _BV(PA2)) + 2*!(PINA & _BV(PA3));
}

static inline void device_hw_address_init(void) {
	DDRA  &= ~(_BV(PA2)|_BV(PA3));
	PORTA |=   _BV(PA2)|_BV(PA3);
}

static inline void leds_init(void) {
	DDRA |= _BV(PA0);
	DDRA |= _BV(PA1);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
	if (state)
		PORTA &= ~_BV(PA0);
	else
		PORTA |= _BV(PA0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
	if (state)
		PORTA &= ~_BV(PA1);
	else
		PORTA |= _BV(PA1);
}

static inline void toggle_dirty_led(void) {
	PINA |= _BV(PA1);
}

#  define IEC_INPUT             PINC
#  define IEC_DDR               DDRC
#  define IEC_PORT              PORTC
#  define IEC_PIN_ATN           PC0
#  define IEC_PIN_DATA          PC1
#  define IEC_PIN_CLOCK         PC2
#  define IEC_PIN_SRQ           PC3
#  define IEC_ATN_INT_VECT      PCINT2_vect
#  define IEC_PCMSK             PCMSK2

static inline void iec_interrupts_init(void) {
	PCICR |= _BV(PCIE2);
	PCIFR |= _BV(PCIF2);
}

#  define BUTTON_NEXT           _BV(PA4)
#  define BUTTON_PREV           _BV(PA5)

static inline rawbutton_t buttons_read(void) {
	return PINA & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
	DDRA  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTA |= BUTTON_NEXT | BUTTON_PREV;
}

#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC6
#  define SOFTI2C_BIT_SDA       PC5
#  define SOFTI2C_BIT_INTRQ     PC7
#  define SOFTI2C_DELAY         6


#elif CONFIG_HARDWARE_VARIANT == 4
/* ---------- Hardware configuration: uIEC ---------- */
/* Note: This CONFIG_HARDWARE_VARIANT number is tested in system.c */
#  define HAVE_ATA
#  ifndef CONFIG_NO_SD
#    define HAVE_SD
#  endif
#  define SPI_LATE_INIT
#  define CF_CHANGE_HANDLER     ISR(INT7_vect)
#  define SD_CHANGE_HANDLER     ISR(PCINT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4

#  define SINGLE_LED

static inline void cfcard_interface_init(void) {
	DDRE  &= ~_BV(PE7);
	PORTE |=  _BV(PE7);
	EICRB |=  _BV(ISC70);
	EIMSK |=  _BV(INT7);
}

static inline uint8_t cfcard_detect(void) {
	return !(PINE & _BV(PE7));
}

static inline void sdcard_interface_init(void) {
	DDRB   &= ~_BV(PB7);
	PORTB  |=  _BV(PB7);
	DDRB   &= ~_BV(PB6);
	PORTB  |=  _BV(PB6);
	PCMSK0 |=  _BV(PCINT7);
	PCICR  |=  _BV(PCIE0);
	PCIFR  |=  _BV(PCIF0);
}

static inline uint8_t sdcard_detect(void) {
	return !(PINB & _BV(PB7));
}

static inline uint8_t sdcard_wp(void) {
	return PINB & _BV(PB6);
}

static inline uint8_t device_hw_address(void) {
	/* No device jumpers on uIEC */
	return 10;
}

static inline void device_hw_address_init(void) {
	return;
}

static inline void leds_init(void) {
	DDRE |= _BV(PE3);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
	if (state)
		PORTE |= _BV(PE3);
	else
		PORTE &= ~_BV(PE3);
}

static inline void toggle_led(void) {
	PINE |= _BV(PE3);
}

#  define IEC_INPUT             PINE
#  define IEC_DDR               DDRE
#  define IEC_PORT              PORTE
#  define IEC_PIN_ATN           PE6
#  define IEC_PIN_DATA          PE4
#  define IEC_PIN_CLOCK         PE5
#  define IEC_PIN_SRQ           PE2
#  define IEC_ATN_INT           INT6
#  define IEC_ATN_INT_VECT      INT6_vect
#  define IEC_CLK_INT           INT5
#  define IEC_CLK_INT_VECT      INT5_vect

static inline void iec_interrupts_init(void) {
	EICRB |= _BV(ISC60);
	EICRB |= _BV(ISC50);
}

#  define BUTTON_NEXT           _BV(PG4)
#  define BUTTON_PREV           _BV(PG3)

static inline rawbutton_t buttons_read(void) {
	return PING & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
	DDRG  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTG |= BUTTON_NEXT | BUTTON_PREV;
}

#  define SOFTI2C_PORT          PORTD
#  define SOFTI2C_PIN           PIND
#  define SOFTI2C_DDR           DDRD
#  define SOFTI2C_BIT_SCL       PD0
#  define SOFTI2C_BIT_SDA       PD1
#  define SOFTI2C_BIT_INTRQ     PD2
#  define SOFTI2C_DELAY         6

/* parallel cable - conflicts with the SOFTI2C pins above! */
#  ifdef CONFIG_NO_SD
#    define HAVE_PARALLEL
#    define PARALLEL_HANDLER      ISR(PCINT0_vect)
#    define PARALLEL_PDDR         DDRD      // CONN2 pins 1,3,...,15
#    define PARALLEL_PPORT        PORTD
#    define PARALLEL_PPIN         PIND
#    define PARALLEL_HDDR         DDRB
#    define PARALLEL_HPORT        PORTB
#    define PARALLEL_HPIN         PINB
#    define PARALLEL_HSK_OUT_BIT  5         // CONN2 pin 14, to C64 FLAG2
#    define PARALLEL_HSK_IN_BIT   4         // CONN2 pin 16, to C64 PC2
#    define PARALLEL_PCMSK        PCMSK0
#    define PARALLEL_PCINT_GROUP  0
#  elif defined(CONFIG_PARALLEL_DOLPHIN)
#    error CONFIG_PARALLEL_DOLPHIN on uIEC requires CONFIG_NO_SD=y !
#  endif

/* Use diskmux code to optionally turn off second IDE drive */
#  define NEED_DISKMUX

#  define HAVE_BOARD_INIT

static inline void board_init(void) {
	/* Force control lines of the external SRAM high */
	DDRG  = _BV(PG0) | _BV(PG1) | _BV(PG2);
	PORTG = _BV(PG0) | _BV(PG1) | _BV(PG2);
}


#elif CONFIG_HARDWARE_VARIANT==5
/* ---------- Hardware configuration: Shadowolf 2 aka sd2iec 1.x ---------- */
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT0_vect)
#  define SD2_CHANGE_HANDLER    ISR(INT2_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<18)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4

static inline void sdcard_interface_init(void) {
	DDRD  &= ~_BV(PD2);
	PORTD |=  _BV(PD2);
	DDRD  &= ~_BV(PD6);
	PORTD |=  _BV(PD6);
	EICRA |=  _BV(ISC00);
	EIMSK |=  _BV(INT0);
#ifdef CONFIG_TWINSD
	PORTD |=  _BV(PD3); // CS
	DDRD  |=  _BV(PD3); // CS
	DDRC  &= ~_BV(PC7); // WP
	PORTC |=  _BV(PC7); // WP
	DDRB  &= ~_BV(PB2); // Detect
	PORTB |=  _BV(PB2); // Detect
	EICRA |=  _BV(ISC20); // Change interrupt
	EIMSK |=  _BV(INT2);  // Change interrupt
#endif
}

static inline uint8_t sdcard_detect(void) {
	return !(PIND & _BV(PD2));
}

static inline uint8_t sdcard_wp(void) {
	return PIND & _BV(PD6);
}

static inline uint8_t sdcard2_detect(void) {
	return !(PINB & _BV(PB2));
}

static inline uint8_t sdcard2_wp(void) {
	return PINC & _BV(PC7);
}

static inline __attribute__((always_inline)) void sdcard2_set_ss(uint8_t state) {
	if (state)
		PORTD |= _BV(PD3);
	else
		PORTD &= ~_BV(PD3);
}

static inline uint8_t device_hw_address(void) {
	return 8 + !(PIND & _BV(PD7)) + 2*!(PIND & _BV(PD5));
}

static inline void device_hw_address_init(void) {
	DDRD  &= ~(_BV(PD7)|_BV(PD5));
	PORTD |=   _BV(PD7)|_BV(PD5);
}

static inline void leds_init(void) {
	DDRC |= _BV(PC0);
	DDRC |= _BV(PC1);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC0);
	else
		PORTC &= ~_BV(PC0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC1);
	else
		PORTC &= ~_BV(PC1);
}

static inline void toggle_dirty_led(void) {
	PINC |= _BV(PC1);
}

#  define IEC_INPUT             PINA
#  define IEC_DDR               DDRA
#  define IEC_PORT              PORTA
#  define IEC_PIN_ATN           PA0
#  define IEC_PIN_DATA          PA1
#  define IEC_PIN_CLOCK         PA2
#  define IEC_PIN_SRQ           PA3
#  define IEC_SEPARATE_OUT
#  define IEC_OPIN_ATN          PA4
#  define IEC_OPIN_DATA         PA5
#  define IEC_OPIN_CLOCK        PA6
#  define IEC_OPIN_SRQ          PA7
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_PCMSK             PCMSK0

static inline void iec_interrupts_init(void) {
	PCICR |= _BV(PCIE0);
	PCIFR |= _BV(PCIF0);
}

#  define BUTTON_NEXT           _BV(PC3)
#  define BUTTON_PREV           _BV(PC2)

static inline rawbutton_t buttons_read(void) {
	return PINC & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
	DDRC  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTC |= BUTTON_NEXT | BUTTON_PREV;
}

#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC4
#  define SOFTI2C_BIT_SDA       PC5
#  define SOFTI2C_BIT_INTRQ     PC6
#  define SOFTI2C_DELAY         6


/* Hardware configuration 6 was old NKC MMC2IEC */


#elif CONFIG_HARDWARE_VARIANT == 7
/* ---------- Hardware configuration: uIEC v3 ---------- */
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT6_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4

#  define SINGLE_LED

static inline void sdcard_interface_init(void) {
	DDRE  &= ~_BV(PE6);
	PORTE |=  _BV(PE6);
	DDRE  &= ~_BV(PE2);
	PORTE |=  _BV(PE2);
	EICRB |=  _BV(ISC60);
	EIMSK |=  _BV(INT6);
}

static inline uint8_t sdcard_detect(void) {
	return !(PINE & _BV(PE6));
}

static inline uint8_t sdcard_wp(void) {
	return PINE & _BV(PE2);
}

static inline uint8_t device_hw_address(void) {
	/* No device jumpers on uIEC */
	return 10;
}

static inline void device_hw_address_init(void) {
	return;
}

static inline void leds_init(void) {
	DDRG |= _BV(PG0);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
	if (state)
		PORTG |= _BV(PG0);
	else
		PORTG &= ~_BV(PG0);
}

static inline void toggle_led(void) {
	PING |= _BV(PG0);
}

#  define IEC_INPUT             PINB
#  define IEC_DDRIN             DDRB
#  define IEC_PORTIN            PORTB
#  define IEC_PIN_ATN           PB4
#  define IEC_PIN_DATA          PB5
#  define IEC_PIN_CLOCK         PB6
#  define IEC_PIN_SRQ           PB7
#  define IEC_SEPARATE_OUT
#  define IEC_PORT              PORTD
#  define IEC_DDROUT            DDRD
#  define IEC_OPIN_ATN          PD4
#  define IEC_OPIN_DATA         PD5
#  define IEC_OPIN_CLOCK        PD6
#  define IEC_OPIN_SRQ          PD7
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_PCMSK             PCMSK0

static inline void iec_interrupts_init(void) {
	PCICR |= _BV(PCIE0);
	PCIFR |= _BV(PCIF0);
}

#  define BUTTON_NEXT           _BV(PG4)
#  define BUTTON_PREV           _BV(PG3)

static inline rawbutton_t buttons_read(void) {
	return PING & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
	DDRG  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTG |= BUTTON_NEXT | BUTTON_PREV;
}

#  define HAVE_BOARD_INIT

static inline void board_init(void) {
	// turn on power LED
	DDRG  |= _BV(PG1);
	PORTG |= _BV(PG1);
}

#elif CONFIG_HARDWARE_VARIANT == 8
/* ---------- Hardware configuration: petSD ---------- */
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(PCINT3_vect)
#  define SD_SUPPLY_VOLTAGE (1L<<21)

/* 288 kHz slow, 2.304 MHz fast */
#  define SPI_DIVISOR_SLOW 64
#  define SPI_DIVISOR_FAST 8

static inline void sdcard_interface_init(void) {
	DDRD   &= ~_BV(PD4);            /* card detect */
	PORTD  |=  _BV(PD4);
	DDRC   &= ~_BV(PC3);            /* write protect  */
	PORTC  |=  _BV(PC3);
	PCMSK3 |=  _BV(PCINT28);        /* card change interrupt */
	PCICR  |=  _BV(PCIE3);
	PCIFR  |=  _BV(PCIF3);
}

static inline uint8_t sdcard_detect(void) {
	return (!(PIND & _BV(PD4)));
}

static inline uint8_t sdcard_wp(void) {
	return (PINC & _BV(PC3));
}

static inline uint8_t device_hw_address(void) {
	/* No device jumpers on petSD */
	return 8;
}
static inline void device_hw_address_init(void) {
	return;
}

static inline void leds_init(void) {
	DDRD |= _BV(PD5);
	DDRD |= _BV(PD6);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
	if (state)
		PORTD |= _BV(PD5);
	else
		PORTD &= (uint8_t) ~_BV(PD5);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
	if (state)
		PORTD |= _BV(PD6);
	else
		PORTD &= (uint8_t) ~_BV(PD6);
}

static inline void toggle_dirty_led(void) {
	PIND |= _BV(PD6);
}

#  define HAVE_IEEE
#  define IEEE_ATN_INT          INT0    /* ATN interrupt (required!) */
#  define IEEE_ATN_INT_VECT     INT0_vect

static inline void ieee_interrupts_init(void) {
	DDRD &= ~_BV(PD2);
	PORTD |= _BV(PD2);
	EICRA |= _BV(ISC00);
	EIMSK |= _BV(INT0);
}

#  define HAVE_7516X            /* Device uses 75160/75161 bus drivers */
#  define IEEE_PORT_TE          PORTB   /* TE */
#  define IEEE_DDR_TE           DDRB
#  define IEEE_PIN_TE           PB0
#  define IEEE_PORT_DC          PORTC   /* DC */
#  define IEEE_DDR_DC           DDRC
#  define IEEE_PIN_DC           PC5
#  define IEEE_INPUT_ATN        PIND    /* ATN */
#  define IEEE_PORT_ATN         PORTD
#  define IEEE_DDR_ATN          DDRD
#  define IEEE_PIN_ATN          PD2
#  define IEEE_INPUT_NDAC       PINC    /* NDAC */
#  define IEEE_PORT_NDAC        PORTC
#  define IEEE_DDR_NDAC         DDRC
#  define IEEE_PIN_NDAC         PC6
#  define IEEE_INPUT_NRFD       PINC    /* NRFD */
#  define IEEE_PORT_NRFD        PORTC
#  define IEEE_DDR_NRFD         DDRC
#  define IEEE_PIN_NRFD         PC7
#  define IEEE_INPUT_DAV        PINB    /* DAV */
#  define IEEE_PORT_DAV         PORTB
#  define IEEE_DDR_DAV          DDRB
#  define IEEE_PIN_DAV          PB2
#  define IEEE_INPUT_EOI        PIND    /* EOI */
#  define IEEE_PORT_EOI         PORTD
#  define IEEE_DDR_EOI          DDRD
#  define IEEE_PIN_EOI          PD7
#  define IEEE_INPUT_SRQ                /* SRQ */
#  define IEEE_PORT_SRQ         PORTA
#  define IEEE_DDR_SRQ          DDRA
#  define IEEE_PIN_SRQ
#  define IEEE_INPUT_IFC                /* IFC */
#  define IEEE_PORT_IFC         PORTA
#  define IEEE_DDR_IFC          DDRA
#  define IEEE_PIN_IFC
#  define IEEE_INPUT_REN                /* REN */
#  define IEEE_PORT_REN         PORTA
#  define IEEE_DDR_REN          DDRA
#  define IEEE_PIN_REN
#  define IEEE_D_PIN            PINA    /* Data */
#  define IEEE_D_PORT           PORTA
#  define IEEE_D_DDR            DDRA
#  define IEEE_BIT_DC           _BV(IEEE_PIN_DC)
#  define IEEE_BIT_TE           _BV(IEEE_PIN_TE)
#  define IEEE_BIT_SRQ          0   /* Define as 0 if SRQ not connected */
#  define IEEE_BIT_REN          0   /* Define as 0 if REN not connected */
#  define IEEE_BIT_IFC          0   /* Define as 0 if IFC not connected */

static inline void ieee_interface_init(void) {
	IEEE_PORT_TE  &= (uint8_t) ~ IEEE_BIT_TE;         // Set TE low
	IEEE_PORT_DC  |= IEEE_BIT_DC;                     // Set DC high
	IEEE_PORT_SRQ |= IEEE_BIT_SRQ;                    // Set SRQ high
	IEEE_DDR_TE   |= IEEE_BIT_TE;                     // Define TE  as output
	IEEE_DDR_DC   |= IEEE_BIT_DC;                     // Define DC  as output
	IEEE_DDR_SRQ  |= IEEE_BIT_SRQ;                    // Define SRQ as output
	IEEE_PORT_ATN |= _BV(IEEE_PIN_ATN);               // Enable pull-up for ATN
	IEEE_PORT_REN |= IEEE_BIT_REN;                    // Enable pull-up for REN
	IEEE_PORT_IFC |= IEEE_BIT_IFC;                    // Enable pull-up for IFC
	IEEE_DDR_ATN  &= (uint8_t) ~ _BV(IEEE_PIN_ATN);   // Define ATN as input
	IEEE_DDR_REN  &= (uint8_t) ~ IEEE_BIT_REN;        // Define REN as input
	IEEE_DDR_IFC  &= (uint8_t) ~ IEEE_BIT_IFC;        // Define IFC as input
}

#  define BUTTON_NEXT           _BV(PB1)
#  define BUTTON_PREV           _BV(PB3)

static inline rawbutton_t buttons_read(void) {
	return (PINB & (BUTTON_NEXT | BUTTON_PREV));
}

static inline void buttons_init(void) {
	DDRB  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
	PORTB |= BUTTON_NEXT | BUTTON_PREV;
}

#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC0
#  define SOFTI2C_BIT_SDA       PC1
#  define SOFTI2C_BIT_INTRQ     PC2
#  define SOFTI2C_DELAY         6

#  define HAVE_BOARD_INIT

static inline void board_init(void) {
	DDRC  |= _BV(PC4);
	PORTC |= _BV(PC4);       /* Disable  ENC28J60 */
}


#elif CONFIG_HARDWARE_VARIANT == 9
/* ---------- Hardware configuration: XS-1541 ---------- */
#  define HAVE_SD
#  define SD_SUPPLY_VOLTAGE     (1L<<18)

/* 230 kHz slow, 1.8432 2MHz fast */
#  define SPI_DIVISOR_SLOW 64
#  define SPI_DIVISOR_FAST 8

static inline void sdcard_interface_init(void) {
	/* No card detect switch, no write protect switch... nothing */
	return;
}

static inline uint8_t sdcard_detect(void) {
	return 1; /* assume it's always there */
}

static inline uint8_t sdcard_wp(void) {
	return 0;
}

static inline uint8_t device_hw_address(void) {
	/* No device jumpers on XS-1541 */
	return 8;
}

static inline void device_hw_address_init(void) {
	return;
}

static inline void leds_init(void) {
	DDRC |= _BV(PC0);  /* busy LED onboard */
	DDRB |= _BV(PB0);  /* dirty LED extern */
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
	if (state)
		PORTC |= _BV(PC0);
	else
		PORTC &= ~_BV(PC0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
	if (state)
		PORTB |= _BV(PB0);
	else
		PORTB &= ~_BV(PB0);
}

static inline void toggle_dirty_led(void) {
	PINB |= _BV(PB0);
}

// dual-interface device, currently only as a compile-time option
#ifdef CONFIG_HAVE_IEC
#  define IEC_INPUT             PORTD
#  define IEC_DDR               DDRD
#  define IEC_PORT              PORTD
#  define IEC_PIN_ATN           PD2
#  define IEC_PIN_DATA          PD4
#  define IEC_PIN_CLOCK         PD7
#  define IEC_PIN_SRQ           PD5
#  define IEC_ATN_INT_VECT      PCINT3_vect
#  define IEC_PCMSK             PCMSK3

static inline void iec_interrupts_init(void) {
	PCICR |= _BV(PCIE3);
	PCIFR |= _BV(PCIF3);
}
#endif // CONFIG_HAVE_IEC

#ifdef CONFIG_HAVE_IEEE
#  define IEEE_ATN_INT          PCINT3
#  define IEEE_PCMSK            PCMSK3
#  define IEEE_PCINT            PCINT27
#  define IEEE_ATN_INT_VECT     PCINT3_vect

static inline void ieee_interrupts_init(void)  {
	/* clear interrupt flag */
	PCIFR |= _BV(PCIF3);

	/* enable ATN in pin change enable mask */
	IEEE_PCMSK |= _BV(IEEE_PCINT);

	/* Enable pin change interrupt 3 (PCINT31..24) */
	PCICR |= _BV(PCIE3);
}

#  define IEEE_C_PIN            PINC    /* Control signals */
#  define IEEE_C_DDR            DDRC
#  define IEEE_C_PORT           PORTC
#  define IEEE_C_ATN_PIN        PIND
#  define IEEE_C_ATN_PORT       PORTD
#  define IEEE_C_ATN_DDR        DDRD
#  define IEEE_PIN_TE           0       /* 7516x only */
#  define IEEE_PIN_DC           0       /* 7516x only */
#  define IEEE_PIN_NDAC         PC4
#  define IEEE_PIN_NRFD         PC5
#  define IEEE_PIN_DAV          PC6
#  define IEEE_PIN_EOI          PC7
#  define IEEE_PIN_ATN          PD3
#  define IEEE_PIN_SRQ          PC3
#  define IEEE_PIN_REN          PC1
#  define IEEE_PIN_IFC          PC2
#  define IEEE_D_PIN            PINA    /* Data */
#  define IEEE_D_DDR            DDRA
#  define IEEE_D_PORT           PORTA
#  define IEEE_BIT_DC           _BV(IEEE_PIN_DC)
#  define IEEE_BIT_TE           _BV(IEEE_PIN_TE)
#  define IEEE_BIT_SRQ          _BV(IEEE_PIN_SRQ)
#  define IEEE_BIT_REN          _BV(IEEE_PIN_REN)
#  define IEEE_BIT_IFC          _BV(IEEE_PIN_IFC)

static inline void ieee_interface_init(void) {
	/* Define TE, DC, SRQ as outputs */
	IEEE_C_DDR |= IEEE_BIT_TE | IEEE_BIT_DC | IEEE_BIT_SRQ;

	/* Define REN, IFC as inputs */
	IEEE_C_DDR &= (uint8_t) ~ (IEEE_BIT_REN | IEEE_BIT_IFC);

	/* DC and SRQ high, pull-up for REN and IFC */
	IEEE_C_PORT     |= IEEE_BIT_DC | IEEE_BIT_SRQ | IEEE_BIT_REN | IEEE_BIT_IFC;
	IEEE_C_ATN_DDR  &= (uint8_t)~_BV(IEEE_PIN_ATN); // ATN as input
	IEEE_C_ATN_PORT |= _BV(IEEE_PIN_ATN);           // enable ATN pullup
}

#endif // CONFIG_HAVE_IEEE

#  define BUTTON_NEXT           _BV(PB1)
#  define BUTTON_PREV           _BV(PB2)

static inline rawbutton_t buttons_read(void) {
	return (PINB & (BUTTON_NEXT | BUTTON_PREV));
}

static inline void buttons_init(void) {
	DDRB &= (uint8_t) ~ (BUTTON_NEXT | BUTTON_PREV);
	PORTB |= BUTTON_NEXT | BUTTON_PREV;
}


#elif CONFIG_HARDWARE_VARIANT == 10

// ------------------------------------------------------------------------
// ---- Hardware configuration: Arduino Mega 2560 or clone ----
// ------------------------------------------------------------------------
// NOTE: A lot of features are optional and can be added with additional components
// or complete shields. Such features are however assumed to be not present and is
// therefore commented out and replaced with dummy returns that fits a simpler default configuration.

/*** SD card support ***/
// If your device supports SD cards by default, define this symbol.
#define HAVE_SD

// Declaration of the interrupt handler for SD card change
#define SD_CHANGE_HANDLER ISR(INT4_vect)

// Initialize all pins and interrupts related to SD - except SPI
static inline void sdcard_interface_init(void)
{
#if 0
	// card detect (SD1)
	DDRE  and_eq compl _BV(PE4);
	PORTE or_eq  _BV(PE4);
	// write protect (SD1) (NOT WIRED YET, PE2 unused on the Arduino)
	DDRE and_eq compl _BV(PE2);
	PORTE or_eq _BV(PE2);

	// card change interrupt (SD1)
	EICRB or_eq _BV(ISC40);   // Any logical change on INTn
	EICRB and_eq ~_BV(ISC41);  // generates an interrupt request
	EIMSK or_eq _BV(INT4);

	// chip select (SD1)
	PORTE or_eq _BV(PE3);
	DDRE or_eq _BV(PE3);
#endif
	// Note: Wrapping SD2 in CONFIG_TWINSD may be a good idea
#if 0
	/* Declaration of the interrupt handler for SD card 2 change */
	#  define SD2_CHANGE_HANDLER ISR(INT9_vect)
	/* chip select (SD2) */
	PORTD |= _BV(PD4);
	DDRD |= _BV(PD4);
	/* card detect (SD2) */
	DDRD &= ~_BV(PD3);
	PORTD |= _BV(PD3);
	/* write protect (SD2) */
	DDRD &= ~_BV(PD7);
	PORTD |= _BV(PD7);
	/* card change interrupt (SD2) */
	EICRA |=  _BV(ISC90); // Change interrupt
	EIMSK |=  _BV(INT9);  // Change interrupt
#endif
}

// sdcard_detect() must return non-zero while a card is inserted
// This must be a pin capable of generating interrupts.
static inline uint8_t sdcard_detect(void)
{
	// OPTIONAL: Add detect pin for sd card if the shield has such.
	// Simply return true otherwise, fake always available.
	return true;
//	return not (PINE bitand _BV(PE4));
}

// SD card 1 is assumed to use the standard SS pin
// If that's not true, #define SDCARD_SS_SPECIAL and
// implement this function:
#if 0
#define SDCARD_SS_SPECIAL
static inline __attribute__((always_inline)) void sdcard_set_ss(uint8_t state)
{
	if(state)
		PORTE or_eq _BV(PE3);
	else
		PORTE and_eq ~_BV(PE3);
}
#endif

// Returns non-zero when the currently inserted card is write-protected
static inline uint8_t sdcard_wp(void)
{
	// OPTIONAL: Add write protect pin for sd card, if the shield has such.
	// Otherwise simply return false (fake always write enabled).
	return false;
}

// Support for a second SD card - use CONFIG_TWINSD=y in your config file to enable!
// Same as the two functions above, but for card 2
static inline uint8_t sdcard2_detect(void)
{
	return 0;
}
static inline uint8_t sdcard2_wp(void)
{
	return 0;
}

// SD card 2 CS pin
static inline __attribute__((always_inline)) void sdcard2_set_ss(uint8_t state)
{
	(void)(state);
}

/* SD Card supply voltage - choose the one appropiate to your board */
// #  define SD_SUPPLY_VOLTAGE (1L<<15)  // 2.7V - 2.8V
// #  define SD_SUPPLY_VOLTAGE (1L<<16)  // 2.8V - 2.9V
// #  define SD_SUPPLY_VOLTAGE (1L<<17)  // 2.9V - 3.0V
// #  define SD_SUPPLY_VOLTAGE (1L<<18)  // 3.0V - 3.1V
// #  define SD_SUPPLY_VOLTAGE (1L<<19)  // 3.1V - 3.2V
// #  define SD_SUPPLY_VOLTAGE (1L<<20)  // 3.2V - 3.3V
#  define SD_SUPPLY_VOLTAGE (1L << 21)  // 3.3V - 3.4V
// #  define SD_SUPPLY_VOLTAGE (1L<<22)  // 3.4V - 3.5V
// #  define SD_SUPPLY_VOLTAGE (1L<<23)  // 3.5V - 3.6V

// SPI clock divisors - the slow one must be 400KHz or slower,
// the fast one can be as high as you thing your hardware will handle
#  define SPI_DIVISOR_SLOW 32
#  define SPI_DIVISOR_FAST 4


/*** Device address selection ***/
// device_hw_address() returns the hardware-selected device address
static inline uint8_t device_hw_address(void)
{
	// OPTIONAL: Attach and implement a 2-button DIP-switch.
	// If not we return always a hard coded number.
	return 8;
	// return 8 + not(PIND bitand _BV(PD7)) + 2 * not(PIND bitand _BV(PD5));
}

/* Configure hardware device address pins */
static inline void device_hw_address_init(void)
{
	// DDRD  and_eq compl (_BV(PD7) bitor _BV(PD5));
	// PORTD or_eq   _BV(PD7) bitor _BV(PD5);
}


/*** LEDs ***/
// Please don't build single-LED hardware anymore...

// Initialize ports for all LEDs
static inline void leds_init(void)
{
	// Note: Depending on the chip and register these lines can compile
	//       to one instruction each on AVR. For two bits this is one
	//       instruction shorter than "DDRB |= _BV(PB6) | _BV(PB7);"
	DDRB or_eq _BV(PB7);
	// OPTIONAL: Implement a second led (RED one!).
	//DDRH |= _BV(PH3);
}

// --- "BUSY" led, recommended color: green (usage similiar to 1541 LED) ---
static inline __attribute__((always_inline)) void set_busy_led(uint8_t state)
{
	// OPTIONAL: Attach and implement a green led for BUSY.
	(void)(state);
//	if (state)
//		PORTH and_eq ~_BV(PH3);
//	else
//		PORTH or_eq _BV(PH3);
}

// --- "DIRTY" led, recommended color: red (errors, unwritten data in memory) ---
static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state)
{
	if(state)
		PORTB and_eq compl _BV(PB7);
	else
		PORTB or_eq _BV(PB7);
}

// Toggle function used for error blinking
static inline void toggle_dirty_led(void)
{
	// Sufficiently new AVR cores have a toggle function
	PINB or_eq _BV(PB7);
}


/*** IEC signals ***/
#define IEC_INPUT PIND
#define IEC_DDR   DDRD
#define IEC_PORT  PORTD

// Pins assigned for the IEC lines
#define IEC_PIN_ATN   PD3
#define IEC_PIN_DATA  PD2
#define IEC_PIN_CLOCK PD1
#define IEC_PIN_SRQ   PD0

// Use separate input/output lines?
// The code assumes that the input is NOT inverted, but the output is.
//#define IEC_SEPARATE_OUT
//#define IEC_OPIN_ATN   PA4
//#define IEC_OPIN_DATA  PA5
//#define IEC_OPIN_CLOCK PA6
//#define IEC_OPIN_SRQ   PA7

/* You can use different ports for input and output bits. The code tries */
/* to not stomp on the unused bits. IEC output is on IEC_PORT.           */
/* Not well-tested yet.                                                  */
//#  define IEC_DDRIN      DDRX
//#  define IEC_DDROUT     DDRY
//#  define IEC_PORTIN     PORTX

/* ATN interrupt (required) */
#define IEC_ATN_INT         INT3
#define IEC_ATN_INT_VECT    INT3_vect
static inline void iec_interrupts_init(void)
{
	EIMSK or_eq _BV(INT3);
}

// CLK interrupt (not required)
// Dreamload requires interrupts for both the ATN and CLK lines. If both are served by
// the same PCINT vector, define that as ATN interrupt above and define IEC_PCMSK.
//#define IEC_PCMSK             PCMSK0
// If the CLK line has its own dedicated interrupt, use the following definitions:
#define IEC_CLK_INT           INT1
#define IEC_CLK_INT_VECT      INT1_vect
static inline void iec_clock_int_setup(void)
{
	EICRA or_eq _BV(ISC10);
}


/*** IEEE signals ***/
// not documented yet, look at petSD/XS-1541 for guidance

/*** User interface ***/
// Button NEXT changes to the next disk image and enables sleep mode (held)
// OPTIONAL: Add a push button for next disk image
#define BUTTON_NEXT _BV(PG0)

// Button PREV changes to the previous disk image
// OPTIONAL: Add a push button for previous disk image.
#define BUTTON_PREV _BV(PG2)

// Read the raw button state - a depressed button should read as 0
static inline rawbutton_t buttons_read(void)
{
	// OPTIONAL: Attach/Implement buttons on some GPIO.
	return 0;
//	return PING bitand (BUTTON_NEXT bitor BUTTON_PREV);
}

static inline void buttons_init(void)
{
	// OPTIONAL: Attach/Implement buttons on some GPIO.
//	DDRG  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
//	PORTG |= BUTTON_NEXT | BUTTON_PREV;
}

// Software I2C lines for the RTC and display
#define SOFTI2C_PORT    PORTC
#define SOFTI2C_PIN     PINC
#define SOFTI2C_DDR     DDRC
#define SOFTI2C_BIT_SCL PC4
#define SOFTI2C_BIT_SDA PC5
#define SOFTI2C_DELAY   6


/*** board-specific initialisation ***/
/* Currently used on uIEC/CF and uIEC/SD only */
#define HAVE_BOARD_INIT
static inline void board_init(void)
{
	// Arduino MEGA important: Clock down to 8MHZ for ease of adaptation to strict cycle timings.
	CLKPR = _BV(CLKPCE);
	CLKPR = _BV(CLKPS0);

	// OPTIONAL: Attach/Implement a power led.
	// turn on power LED
//  DDRG  or_eq _BV(PG1);
//  PORTG or_eq _BV(PG1);
}

#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif


/* ---------------- End of user-configurable options ---------------- */

#if !defined(CONFIG_HAVE_IEC) && !defined(CONFIG_HAVE_IEEE)
#  error Need CONFIG_HAVE_IEC and/or CONFIG_HAVE_IEEE
// Please edit your config-<devicename> if this error occurs.
#endif

#if defined(CONFIG_HAVE_IEC) && defined(CONFIG_HAVE_IEEE)
#  error Sorry, dual-interface devices must select only one interface at compile time!
#endif


/* --- IEC --- */
#ifdef CONFIG_HAVE_IEC

#define IEC_BIT_ATN      _BV(IEC_PIN_ATN)
#define IEC_BIT_DATA     _BV(IEC_PIN_DATA)
#define IEC_BIT_CLOCK    _BV(IEC_PIN_CLOCK)
#define IEC_BIT_SRQ      _BV(IEC_PIN_SRQ)

/* Return type of iec_bus_read() */
typedef uint8_t iec_bus_t;

/* OPIN definitions are only used in the assembler module */
#ifdef IEC_SEPARATE_OUT
#  define IEC_OBIT_ATN   _BV(IEC_OPIN_ATN)
#  define IEC_OBIT_DATA  _BV(IEC_OPIN_DATA)
#  define IEC_OBIT_CLOCK _BV(IEC_OPIN_CLOCK)
#  define IEC_OBIT_SRQ   _BV(IEC_OPIN_SRQ)
#  define IEC_OUTPUT     IEC_PORT
#else
#  define IEC_OPIN_ATN   IEC_PIN_ATN
#  define IEC_OPIN_DATA  IEC_PIN_DATA
#  define IEC_OPIN_CLOCK IEC_PIN_CLOCK
#  define IEC_OPIN_SRQ   IEC_PIN_SRQ
#  define IEC_OBIT_ATN   IEC_BIT_ATN
#  define IEC_OBIT_DATA  IEC_BIT_DATA
#  define IEC_OBIT_CLOCK IEC_BIT_CLOCK
#  define IEC_OBIT_SRQ   IEC_BIT_SRQ
#  define IEC_OUTPUT     IEC_DDR
#endif

#ifndef IEC_PORTIN
#  define IEC_PORTIN IEC_PORT
#endif

#ifndef IEC_DDRIN
#  define IEC_DDRIN  IEC_DDR
#  define IEC_DDROUT IEC_DDR
#endif

/* The AVR asm modules don't support noninverted output lines, */
/* so this can be statically defined for all configurations.   */
#define IEC_OUTPUTS_INVERTED

#ifdef IEC_PCMSK
	 /* For hardware configurations using PCINT for IEC IRQs */
#  define set_atn_irq(x) \
		 if (x) { IEC_PCMSK |= _BV(IEC_PIN_ATN); } \
		 else { IEC_PCMSK &= (uint8_t)~_BV(IEC_PIN_ATN); }
#  define set_clock_irq(x) \
		 if (x) { IEC_PCMSK |= _BV(IEC_PIN_CLOCK); } \
		 else { IEC_PCMSK &= (uint8_t)~_BV(IEC_PIN_CLOCK); }
#  define HAVE_CLOCK_IRQ
#else
		 /* Hardware ATN interrupt */
#  define set_atn_irq(x) \
		 if (x) { EIMSK |= _BV(IEC_ATN_INT); } \
		 else { EIMSK &= (uint8_t)~_BV(IEC_ATN_INT); }

#  ifdef IEC_CLK_INT
		 /* Hardware has a CLK interrupt */
#    define set_clock_irq(x) \
			 if (x) { EIMSK |= _BV(IEC_CLK_INT); } \
			 else { EIMSK &= (uint8_t)~_BV(IEC_CLK_INT); }
#    define HAVE_CLOCK_IRQ
#  endif
#endif

/* IEC output functions */
#ifdef IEC_OUTPUTS_INVERTED
#  define COND_INV(x) (!(x))
#else
#  define COND_INV(x) (x)
#endif

static inline __attribute__((always_inline)) void set_atn(uint8_t state)
{
	if (COND_INV(state))
		IEC_OUTPUT or_eq IEC_OBIT_ATN;
	else
		IEC_OUTPUT and_eq ~IEC_OBIT_ATN;
}

static inline __attribute__((always_inline)) void set_data(uint8_t state)
{
	if (COND_INV(state))
		IEC_OUTPUT or_eq IEC_OBIT_DATA;
	else
		IEC_OUTPUT and_eq ~IEC_OBIT_DATA;
}

static inline __attribute__((always_inline)) void set_clock(uint8_t state)
{
	if (COND_INV(state))
		IEC_OUTPUT or_eq IEC_OBIT_CLOCK;
	else
		IEC_OUTPUT and_eq ~IEC_OBIT_CLOCK;
}

#ifdef IEC_SEPARATE_OUT
static inline __attribute__((always_inline)) void set_srq(uint8_t state)
{
	if (COND_INV(state))
		IEC_OUTPUT or_eq IEC_OBIT_SRQ;
	else
		IEC_OUTPUT and_eq ~IEC_OBIT_SRQ;
}
#else
/* this version of the function turns on the pullups when state is 1 */
/* note: same pin for in/out implies inverted output via DDR */
static inline __attribute__((always_inline)) void set_srq(uint8_t state)
{
	if(state) {
		IEC_DDR  and_eq compl IEC_OBIT_SRQ;
		IEC_PORT or_eq  IEC_OBIT_SRQ;
	}
	else {
		IEC_PORT and_eq compl IEC_OBIT_SRQ;
		IEC_DDR  or_eq  IEC_OBIT_SRQ;
	}
}
#endif

#undef COND_INV

// for testing purposes only, probably does not do what you want!
#define toggle_srq() IEC_INPUT |= IEC_OBIT_SRQ

/* IEC lines initialisation */
static inline void iec_interface_init(void)
{
#ifdef IEC_SEPARATE_OUT
	/* Set up the input port - pullups on all lines */
	IEC_DDRIN  &= (uint8_t)~(IEC_BIT_ATN  | IEC_BIT_CLOCK  | IEC_BIT_DATA  | IEC_BIT_SRQ);
	IEC_PORTIN |= IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA | IEC_BIT_SRQ;
	/* Set up the output port - all lines high */
	IEC_DDROUT |=            IEC_OBIT_ATN | IEC_OBIT_CLOCK | IEC_OBIT_DATA | IEC_OBIT_SRQ;
	IEC_PORT   &= (uint8_t)~(IEC_OBIT_ATN | IEC_OBIT_CLOCK | IEC_OBIT_DATA | IEC_OBIT_SRQ);
#else
	// Pullups would be nice, but AVR can't switch from
	// low output to hi-z input directly
	IEC_DDR  and_eq (uint8_t)compl(IEC_BIT_ATN bitor IEC_BIT_CLOCK bitor IEC_BIT_DATA bitor IEC_BIT_SRQ);
	IEC_PORT and_eq (uint8_t)compl(IEC_BIT_ATN bitor IEC_BIT_CLOCK bitor IEC_BIT_DATA);
	// SRQ is special-cased because it may be unconnected
	IEC_PORT or_eq IEC_BIT_SRQ;
#endif

#ifdef HAVE_PARALLEL
	/* set data lines to input with pullup */
	PARALLEL_PDDR  = 0;
	PARALLEL_PPORT = 0xff;

	/* set HSK_OUT and _IN to input with pullup */
	PARALLEL_HDDR  &= ~(_BV(PARALLEL_HSK_OUT_BIT) |
											_BV(PARALLEL_HSK_IN_BIT));
	PARALLEL_HPORT |= _BV(PARALLEL_HSK_OUT_BIT) |
										_BV(PARALLEL_HSK_IN_BIT);

	/* enable interrupt for parallel handshake */
#  ifdef PARALLEL_PCINT_GROUP
	/* excluse PCINT group */
	PARALLEL_PCMSK |= _BV(PARALLEL_HSK_IN_BIT);
	PCICR |= _BV(PARALLEL_PCINT_GROUP);
	PCIFR |= _BV(PARALLEL_PCINT_GROUP);
#  else
	/* exclusive INTx line */
#    error Implement me!
#  endif
#endif
}

/* weak-aliasing is resolved at link time, so it doesn't work */
/* for static inline functions - use a conditionally compiled */
/* wrapper instead                                            */
#  ifndef CONFIG_HAVE_IEEE
static inline void bus_interface_init(void)
{
	iec_interface_init();
}
#  endif
#endif /* CONFIG_HAVE_IEC */


/* --- IEEE --- */
#ifdef CONFIG_HAVE_IEEE
#  ifdef IEEE_PCMSK
/* IEEE-488 ATN interrupt using PCINT */
static inline void set_atn_irq(uint8_t x) {
	if (x)
		IEEE_PCMSK |= _BV(IEEE_PCINT);
	else
		IEEE_PCMSK &= (uint8_t) ~_BV(IEEE_PCINT);
}
#  else
/* Hardware ATN interrupt */
static inline void set_atn_irq(uint8_t x) {
	if (x)
		EIMSK |= _BV(IEEE_ATN_INT);
	else
		EIMSK &= (uint8_t) ~_BV(IEEE_ATN_INT);
}
#  endif

/* same weak alias problem as in IEC version */
#  ifndef CONFIG_HAVE_IEC
static inline void bus_interface_init(void) {
	ieee_interface_init();
}
#  endif
#endif /* CONFIG_HAVE_IEEE */



/* The assembler module needs the vector names, */
/* so the _HANDLER macros are created here.     */
#define IEC_ATN_HANDLER   ISR(IEC_ATN_INT_VECT)
#define IEC_CLOCK_HANDLER ISR(IEC_CLK_INT_VECT)
#define IEEE_ATN_HANDLER  ISR(IEEE_ATN_INT_VECT)

/* SD SS pin default implementation */
#ifndef SDCARD_SS_SPECIAL
static inline __attribute__((always_inline)) void sdcard_set_ss(uint8_t state) {
	if (state)
		SPI_PORT |= SPI_SS;
	else
		SPI_PORT &= ~SPI_SS;
}
#endif

/* Display interrupt pin */
#ifdef CONFIG_REMOTE_DISPLAY
static inline void display_intrq_init(void) {
	/* Enable pullup on the interrupt line */
	SOFTI2C_PORT |= _BV(SOFTI2C_BIT_INTRQ);
}

static inline uint8_t display_intrq_active(void) {
	return !(SOFTI2C_PIN & _BV(SOFTI2C_BIT_INTRQ));
}
#endif

/* P00 name cache is in bss by default */
#ifndef P00CACHE_ATTRIB
#  define P00CACHE_ATTRIB
#endif

/* -- ensure that the timing for Dolphin is achievable        -- */
/* the C64 will switch to an alternate, not-implemented protocol */
/* if the answer to the XQ/XZ commands is too late and the       */
/* file name/command dump takes too long if the buffer is        */
/* smaller than the output from uart_trace                       */
#if defined(CONFIG_PARALLEL_DOLPHIN) && \
		defined(CONFIG_UART_DEBUG) && \
	CONFIG_UART_BUF_SHIFT < 8  // 7 may work with short file names
#  error Enabling both DolphinDOS and UART debugging requires CONFIG_UART_BUF_SHIFT >= 8 !
#endif

#endif
