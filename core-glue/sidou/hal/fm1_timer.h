/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_timer.h on the host. TIMER5's 10 kHz tick is the core unit's tick (core_sidou.c). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_irq.h"

FM1_INLINE void fm1_timer5_start(void (*isr)(void), uint32_t prio)
{
    (void)isr;
    (void)prio;
}
FM1_INLINE void fm1_timer5_ack(void) {}
