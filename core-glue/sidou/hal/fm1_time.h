/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_time.h on the host. TIMER4 is the simulated clock (sidou_glue.h): it moves only
 * between the host's calls, so a wait is a yield of the main loop's coroutine (fm1_delay_ms; outside it, at the
 * power-on, it returns at once) and a microsecond spin returns at once (fm1_delay_us: the key matrix's settle, never
 * run in a core). */
#pragma once
#include <stdint.h>
#include "sidou_glue.h"

#define FM1_TICKS_PER_US 24u
static inline void fm1_time_init(void) {}
static inline uint32_t fm1_ticks(void) { return fm1core_sd_ticks(); }
static inline uint32_t fm1_micros(void) { return fm1core_sd_ticks() / FM1_TICKS_PER_US; }
static inline void fm1_delay_us(uint32_t us) { (void)us; }
static inline void fm1_delay_ms(uint32_t ms)
{
    const uint32_t t0 = fm1core_sd_ms();
    if (!fm1core_sd_can_wait())
        return;
    while ((uint32_t)(fm1core_sd_ms() - t0) < ms)
        fm1core_sd_wait();
}
