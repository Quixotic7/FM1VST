/* SPDX-License-Identifier: GPL-3.0-only */
/* The tick-unit adapter (core-glue/common/README.md "ticks"): the firmware's clocks from the ABI's simulated device
 * clock. FM1VST always runs simulated time (panel->ready = 2): the device millisecond is the one tick(ms) gave, and
 * nothing reads the host's wall clock, so a run is deterministic.
 *
 *     tick(ms):     fm1t_set_ms(ms);                    (first, before the firmware's tick work)
 *     fm1_ticks():  return fm1t_us();                   (FM1_TICKS_PER_US 1: Felucca 1.0, ChoralRoot's emulator)
 *                   return fm1t_ticks24();              (FM1_TICKS_PER_US 24: TIMER4 at 24 MHz, Felucca 1.6, SLOOP)
 *     fm1_ms:       fm1t_ms()
 *
 * The finer clocks are the millisecond's start (ms * 1000 us, ms * 24000 ticks), wrapping as the device's 32-bit
 * counters do (24 MHz: every 178.9 s; us: every 71.6 min). A firmware that BUSY-WAITS on its tick counter inside one
 * entry point (`t0 = fm1_ticks(); while (fm1_ticks() - t0 < 50 * 24);`) would spin forever, since simulated time does
 * not move inside a call: read the clock through fm1t_us_spin() / fm1t_ticks24_spin() at such sites, which advance
 * the clock by `step` us per read (monotonic and deterministic: the next tick(ms) never moves it back, a long spin runs
 * ahead until the device's ms catches up). Waits longer than a frame belong in a coroutine yield (coro.h) or a glue
 * that returns and resumes. Header-only, static state: once per firmware unit. */
#pragma once
#include <stdint.h>

static uint32_t fm1t_ms_now;              /* the device ms tick() gave */
static uint64_t fm1t_us_now;              /* the simulated us: max(ms * 1000, what spins advanced it to) */

static inline void fm1t_set_ms(uint32_t ms)
{
    const uint64_t us = (uint64_t)ms * 1000u;
    fm1t_ms_now = ms;
    if (us > fm1t_us_now)
        fm1t_us_now = us;
}
static inline uint32_t fm1t_ms(void) { return fm1t_ms_now; }
static inline uint32_t fm1t_us(void) { return (uint32_t)fm1t_us_now; }
static inline uint32_t fm1t_ticks24(void) { return (uint32_t)(fm1t_us_now * 24u); }
static inline uint64_t fm1t_us64(void) { return fm1t_us_now; }
/* the clock for a busy-wait site: each read moves it on by step us */
static inline uint32_t fm1t_us_spin(uint32_t step)
{
    fm1t_us_now += step;
    return (uint32_t)fm1t_us_now;
}
static inline uint32_t fm1t_ticks24_spin(uint32_t step_us)
{
    fm1t_us_now += step_us;
    return (uint32_t)(fm1t_us_now * 24u);
}
/* convert */
static inline uint32_t fm1t_us_to_ticks24(uint32_t us) { return us * 24u; }
static inline uint32_t fm1t_ms_to_ticks24(uint32_t ms) { return ms * 24000u; }
