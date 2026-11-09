/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_sys.h on the host. No second core, no P33 power domain: the watchdog's feed is where
 * a wait gives the clock back (the update protocol's reply waits feed it: ota_idle); a reset, UBOOT and the update
 * loader end the core's "process" (exit(): it halts, the host reloads it to power on again). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "sidou_glue.h"

#ifndef FM1_CPU1_USED
#define FM1_CPU1_USED 0
#endif
#define FM1_RESET_PREP() ((void)0)
FM1_INLINE void fm1_cpu1_halt(void) {}
FM1_INLINE void fm1_cpu1_stop(void) {}    /* (felucca-bsp's fm1_cpu1_stop.h: no second core to hold) */
static uint32_t fm1_p33_timeouts;
static uint8_t fm1_p33_read(uint32_t a)
{
    (void)a;
    return 0;
}
static void fm1_p33_write(uint32_t a, uint8_t d)
{
    (void)a;
    (void)d;
}
static void fm1_p33_or(uint32_t a, uint8_t d) { fm1_p33_write(a, d); }
static void fm1_p33_and(uint32_t a, uint8_t d) { fm1_p33_write(a, d); }
static void fm1_wdt_arm(uint8_t t) { (void)t; }
static void fm1_wdt_feed(void) { fm1core_sd_wait(); }
static void fm1_wdt_stop(void) {}
static struct {
    uint8_t p3_rst;
    uint32_t rst_src;
    uint8_t wdt_con;
} fm1_boot = {1u, 0u, 0u};
static void fm1_reset_reason(void) {}
static void fm1_reboot(void) { fm1core_sd_exit(2); }
static void fm1_enter_uboot(void) { fm1core_sd_exit(3); }
FM1_INLINE void fm1_core_reset(void) { fm1core_sd_exit(2); }
FM1_INLINE void fm1_enter_update(const uint8_t *parm)
{
    (void)parm;
    fm1core_sd_exit(3);
}
FM1_INLINE void fm1_updata_parm_clear(void) {}
FM1_INLINE void fm1_mailbox_clear(void) {}
static int fm1_mem_readable(uint32_t a, uint32_t n)
{
    (void)a;
    (void)n;
    return 0;
}
FM1_INLINE uint8_t fm1_peek8(uint32_t a)
{
    (void)a;
    return 0;
}
