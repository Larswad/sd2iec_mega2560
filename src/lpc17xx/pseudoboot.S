/* pre-startup code for LPC17xx
 *
 * Written 2012 by Ingo Korb, no copyright claimed
 */
#include "asmconfig.h"

/* address definitions */
/* CMSIS only provides C structs, nothing assembler-friendly */
#define VTOR_Offset 8
        
        .syntax unified

        .section .bootvectors

        .macro  except label
        .word   __unhandled_exception
        .endm

        /* Cortex M3 standard except vectors */
        .word   __stack
        .word   _trampoline_start
        except  NMI_Handler
        except  HardFault_Handler  /* 2 */
        except  MemManage_Handler
        except  BusFault_Handler   /* 4 */
        except  UsageFault_Handler
        .word   0
        .word   0
        .word   0
        .word   0
        except  SVC_Handler     /* 10 */
        except  DebugMon_Handler
        .word   0
        except  PendSV_Handler
        except  SysTick_Handler

        /* External interrupt vectors */
        except  WDT_IRQHandler
        except  TIMER0_IRQHandler
        except  TIMER1_IRQHandler
        except  TIMER2_IRQHandler
        except  TIMER3_IRQHandler
        except  UART0_IRQHandler  /* 20 */
        except  UART1_IRQHandler
        except  UART2_IRQHandler
        except  UART3_IRQHandler
        except  PWM1_IRQHandler
        except  I2C0_IRQHandler
        except  I2C1_IRQHandler
        except  I2C2_IRQHandler  /* 27 */
        except  SPI_IRQHandler
        except  SSP0_IRQHandler
        except  SSP1_IRQHandler
        except  PLL0_IRQHandler
        except  RTC_IRQHandler
        except  EINT0_IRQHandler
        except  EINT1_IRQHandler
        except  EINT2_IRQHandler
        except  EINT3_IRQHandler
        except  ADC_IRQHandler
        except  BOD_IRQHandler
        except  USB_IRQHandler
        except  CAN_IRQHandler
        except  DMA_IRQHandler
        except  I2S_IRQHandler
        except  ENET_IRQHandler
        except  RIT_IRQHandler
        except  MCPWM_IRQHandler
        except  QEI_IRQHandler
        except  PLL1_IRQHandler


        /* include the bootloader information block */
        .section .bootinfo

#include "lpc17xx/bootinfo.S"

        /* ASCII string with additional information, zero-terminated */
        .ascii "sd2iec " VERSION LONGVERSION
        .byte   0
        

        .section .boottrampoline, "x"

        .global _trampoline_start
        .thumb_func

_trampoline_start:
        /* change vector table offset */
        ldr     r0, =0x4000
        ldr     r1, =SCB_BASE
        str     r0, [r1, #VTOR_Offset]

        /* load start address */
        ldr     r1, [r0, #4]

        /* jump to main code */
        bx      r1

        .end
