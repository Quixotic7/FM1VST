/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_irq.h on the host. One thread runs the whole device (the ABI's threading contract):
 * the interrupts are the core unit's calls (the audio block, the 1 ms tick), never in the middle of the main loop,
 * so masking them is nothing. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_IRQ_TARGET 1
enum { FM1_IRQ_EXCEPTION = 1, FM1_IRQ_ALNK0 = 11, FM1_IRQ_SPI1 = 16, FM1_IRQ_UART1 = 20,
       FM1_IRQ_SARADC = 24, FM1_IRQ_SPI2 = 37, FM1_IRQ_TIMER5 = 63, FM1_IRQ_SOFT0 = 120 };
static inline void fm1_irq_off(void) {}
static inline void fm1_irq_on(void) {}
#define FM1_CRASH_MAGIC 0x43525348u
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;
} fm1_crash_t;
static void fm1_irq_init(void) {}
static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)
{
    (void)n;
    (void)h;
    (void)prio;
}
static void fm1_irq_mask(uint32_t n) { (void)n; }
static void fm1_irq_enable_all(void) {}
