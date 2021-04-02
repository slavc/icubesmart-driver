#ifndef BOARD_H
#define BOARD_H

#include "cpu.h"

#define M(X) ((X) * 1000000)
#define K(X) (Hz(X) * 1000)
#define Hz(X) (X)

#define CPU_CLK_HZ M(Hz(12))
#define TIMER_CLK_HZ (CPU_CLK_HZ / 12)

/* Configure Frame Buffer */

#define FB_TIMER_MOD TMOD
#define FB_TIMER 0
#define FB_TIMER_IRQ 1
#define FB_TIMER_MOD_SHIFT (FB_TIMER * 4)
#define FB_TIMER_RUN TR0
#define FB_TIMER_IE ET0
#define FB_TIMER_TL TL0
#define FB_TIMER_TH TH0

#define LATCH_DATA P0
#define LATCH_LOAD P2
#define PLANE_ENABLE P1

/* Configure UART */
#define UART_TIMER_MOD TMOD
#define UART_TIMER 1
#define UART_TIMER_MOD_SHIFT (UART_TIMER * 4)
#define UART_TIMER_RUN TR1
#define UART_TIMER_TL TL1
#define UART_TIMER_TH TH1

#define UART_IRQ 4
#define UART_RI RI
#define UART_TI TI
#define UART_REN REN
#define UART_IE ES

#endif
