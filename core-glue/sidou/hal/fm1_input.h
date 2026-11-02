/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_input.h on the host, for its one includer, platform/fm1/input.c. The scan is the
 * core unit's tick (core_sidou.c): it writes the debounced state (fm1_in: a press at once, a release after the
 * device's ~9 ms) and counts a scan frame each device ms; the LEDs are two planes the core unit turns into the
 * panel's levels (fm1_led lit, fm1_led_dim the glow). The pins (input_raw_keys, the power-on gesture) are plain
 * words: never read in a core. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_gpio.h"
#include "fm1_time.h"
#include "sidou_glue.h"

#define FM1_DEB_PRESS 2u
#define FM1_DEB_RELEASE 8u
#define FM1_SETTLE_US 10u
#define FM1_LED_TICK_US 100u
#define FM1_NCOL 11u
#define FM1_NKEY 41u
#define FM1_NENC 7u
_Static_assert(FM1_NCOL == FM1CORE_SD_NCOL && FM1_NENC == FM1CORE_SD_NENC, "sidou_glue.h's shapes");
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#define fm1_in fm1core_sd_in
#define fm1_led fm1core_sd_led
#define fm1_led_dim fm1core_sd_led_dim
static void fm1__led_lines(uint32_t rowmask) { (void)rowmask; }
static void fm1__sr_word(uint32_t w) { (void)w; }
static uint32_t fm1__rows(void) { return 0u; }
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_input_scan(void) {}
static inline uint32_t fm1__lock(void) { return 0u; }
static inline void fm1__unlock(uint32_t k) { (void)k; }
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s;
    if (e >= FM1_NENC)
        return 0;
    s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    return p;
}
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
