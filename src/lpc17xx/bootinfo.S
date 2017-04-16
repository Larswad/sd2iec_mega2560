/* LPC176x SD boot loader info block
 *
 * Written 2012 by Ingo Korb, no copyright claimed
 */
#include "asmconfig.h"

        .syntax unified

        /* this is an empty info block, it will be filled by an external script */
        .word   0xffffffff         /* CONFIG_BOOT_DEVID */
        .short  0xffff             /* BOOT_VERSION      */
        .short  0xffff             /* CRC16 CCITT       */
