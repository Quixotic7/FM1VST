/* SPDX-License-Identifier: GPL-3.0-only */
/* Translating a firmware's HAL to and from the ABI's panel (core-api/fm1core.h fm1panel_t; core-glue/common/README.md
 * "panel"). Header-only: static inline functions and macros, no state.
 *
 * The rule (fm1core.h THREADING CONTRACT): the core reads the host's input at the top of each tick / frame / idle
 * (copy it into the firmware's HAL there) and has written its output when init / frame / idle return (copy the LEDs
 * and the LCD out there, only when they changed).
 *
 *   input     FM1P_EMU_IN(panel, hal)       an emu-style HAL (fields keys, buttons, keys_tap, buttons_tap, enc[],
 *                                           master: ChoralRoot's emu_hal_t, fm1p_emu_hal_t, ..): held state copied,
 *                                           taps and detents taken (moved over, cleared in the panel)
 *             fm1p_take_taps / fm1p_take_enc / fm1p_take_role: the same, field by field
 *             fm1p_edges: key / button masks -> press and release edges (a tap: a press now, its release next tick)
 *             fm1p_vel / fm1p_lean: a key's velocity (or the firmware's default) and aftertouch
 *             fm1p_label_of / fm1p_labels: matrix button id -> label (FM1P_B_*)
 *   boot      FM1P_EMU_BOOT(panel, hal)     before the firmware's power-on: ready, master (and the held input)
 *             FM1P_EMU_BOOTED(panel, hal)   after it: btn_id / enc_id / enc_dir, master, ready back to the panel
 *             fm1p_panel_default(panel)     the FM-1's factory wiring (Felucca panel.c PANEL_DEFAULT), for a HAL
 *                                           that has no panel table of its own; FM1P_KEYMAP is its keymap
 *   output    fm1p_leds_planes(panel, planes, levels, n)   packed row-bit planes (fm1_led[11], fm1_led_dim[11], ..),
 *                                           the first plane with an LED's bit gives its level; PLAY green from its
 *                                           position (fm1p_play_green)
 *             fm1p_led_id(panel, keymap, id, level)        a per-LED-id firmware (fm1_led_key(id, on), led_lv[41])
 *             fm1p_lcd_out(panel, lcd, writes)            a big-endian RGB565 frame buffer with its own write count
 *             fm1p_lcd_out_native(panel, fb)               a native-endian one (swapped), compared, bumped if changed */
#pragma once
#include <stdint.h>
#include <string.h>
#include "fm1core.h"

/* ---- the emu-style HAL (ChoralRoot's ABI 1..4 emu_hal_t, minus its diagnostics): what FM1VST's own HALs for the
 * Felucca 1.0 / Melodee 0.11 cores keep internally (`typedef fm1p_emu_hal_t emu_hal_t;`) */
typedef struct {
    volatile uint32_t keys, buttons, keys_tap, buttons_tap;
    volatile int32_t enc[FM1P_NENC];
    volatile int32_t master;
    uint8_t led[FM1P_NCOL], led_dim[FM1P_NCOL];  /* fm1_led / fm1_led_dim: packed row bits per column */
    uint16_t lcd[FM1P_LCD_W * FM1P_LCD_H];       /* RGB565 big-endian as sent */
    volatile uint32_t lcd_writes;
    uint8_t btn_id[FM1P_NB];
    uint8_t enc_id[FM1P_NE - 1];
    int8_t enc_dir[FM1P_NE - 1];
    uint8_t led_play_green;                      /* (col << 3) | row bit of PLAY's green LED */
    uint8_t ready;
} fm1p_emu_hal_t;

/* ---------------------------------------------------------------- input --- */
static inline uint32_t fm1p_take_u32(volatile uint32_t *v) { return __atomic_exchange_n(v, 0u, __ATOMIC_SEQ_CST); }
static inline int32_t fm1p_take_i32(volatile int32_t *v) { return __atomic_exchange_n(v, 0, __ATOMIC_SEQ_CST); }
/* the taps since the last take (and clear them in the panel) */
static inline uint32_t fm1p_take_key_taps(fm1panel_t *p) { return fm1p_take_u32(&p->keys_tap); }
static inline uint32_t fm1p_take_button_taps(fm1panel_t *p) { return fm1p_take_u32(&p->buttons_tap); }
/* a matrix encoder's detents (+ = clockwise), taken */
static inline int32_t fm1p_take_enc(fm1panel_t *p, uint32_t matrix_enc)
{
    return matrix_enc < FM1P_NENC ? fm1p_take_i32(&p->enc[matrix_enc]) : 0;
}
/* a knob role's detents (+ = clockwise for the role), taken: for a HAL that reads its knobs by role */
static inline int32_t fm1p_take_role(fm1panel_t *p, int role)
{
    if (role < 0 || role >= FM1P_NE - 1)
        return 0;
    return fm1p_take_enc(p, p->enc_id[role]) * (p->enc_dir[role] < 0 ? -1 : 1);
}

/* the panel's input into an emu-style HAL: held state copied, taps OR'ed in and detents added (and taken from the
 * panel), MASTER copied. At the top of every tick, frame and idle. */
#define FM1P_EMU_IN(p, h)                                                                                      \
    do {                                                                                                       \
        uint32_t fm1p_i_;                                                                                      \
        (h)->keys = (p)->keys;                                                                                 \
        (h)->buttons = (p)->buttons;                                                                           \
        (h)->keys_tap |= fm1p_take_u32(&(p)->keys_tap);                                                        \
        (h)->buttons_tap |= fm1p_take_u32(&(p)->buttons_tap);                                                  \
        for (fm1p_i_ = 0; fm1p_i_ < FM1P_NENC; fm1p_i_++)                                                      \
            (h)->enc[fm1p_i_] += fm1p_take_i32(&(p)->enc[fm1p_i_]);                                           \
        (h)->master = (p)->master;                                                                             \
    } while (0)

/* before the power-on: what the host set (ready = 2, MASTER or 0 for the firmware's default, held input) */
#define FM1P_EMU_BOOT(p, h)                                                                                    \
    do {                                                                                                       \
        (h)->ready = (p)->ready;                                                                               \
        FM1P_EMU_IN(p, h);                                                                                     \
    } while (0)
/* after it: the firmware's panel table, its MASTER default, ready */
#define FM1P_EMU_BOOTED(p, h)                                                                                  \
    do {                                                                                                       \
        memcpy((p)->btn_id, (const void *)(h)->btn_id, sizeof (p)->btn_id);                                    \
        memcpy((p)->enc_id, (const void *)(h)->enc_id, sizeof (p)->enc_id);                                    \
        memcpy((p)->enc_dir, (const void *)(h)->enc_dir, sizeof (p)->enc_dir);                                 \
        (p)->master = (h)->master;                                                                             \
        (p)->ready = (h)->ready;                                                                               \
    } while (0)

/* press / release edges of a held mask plus its taps: a tap is a press this call and a release the next */
typedef struct {
    uint32_t prev;
} fm1p_edges_t;
static inline uint32_t fm1p_edges(fm1p_edges_t *e, uint32_t held, uint32_t taps, uint32_t *down, uint32_t *up)
{
    const uint32_t now = held | taps;
    if (down)
        *down = now & ~e->prev;
    if (up)
        *up = e->prev & ~now;
    e->prev = now;
    return now;                                  /* the state to report as held this call */
}

/* a key's velocity 1..127 (def when the host gave none), its aftertouch 0..100 */
static inline uint8_t fm1p_vel(const fm1panel_t *p, uint32_t key, uint8_t def)
{
    const uint8_t v = key < FM1P_NKEY ? p->key_vel[key] : 0;
    return v ? (v > 127 ? 127 : v) : def;
}
static inline uint8_t fm1p_lean(const fm1panel_t *p, uint32_t key)
{
    const uint8_t v = key < FM1P_NKEY ? p->key_lean[key] : 0;
    return v > 100 ? 100 : v;
}

/* the label (FM1P_B_*) of a matrix button, FM1P_NB if none; a matrix mask as a label mask */
static inline int fm1p_label_of(const fm1panel_t *p, uint32_t matrix_id)
{
    int b;
    for (b = 0; b < FM1P_NB; b++)
        if (p->btn_id[b] == matrix_id)
            return b;
    return FM1P_NB;
}
static inline uint32_t fm1p_labels(const fm1panel_t *p, uint32_t matrix_mask)
{
    uint32_t out = 0;
    int b;
    for (b = 0; b < FM1P_NB; b++)
        if (matrix_mask & (1u << p->btn_id[b]))
            out |= 1u << b;
    return out;
}

/* ----------------------------------------------------------------- boot --- */
/* the FM-1's wiring (hal/fm1_input.h FM1_KEYMAP): key id (0..13 matrix button, 14 + n note key n) at (row, column) */
#define FM1P_KEYMAP_INIT                                                                                       \
    {                                                                                                          \
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, /* PA0: encoders */                                      \
        {5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1},       /* PA5 */                                                \
        {34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12},   /* PA6 */                                                \
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33}, /* PA7 */                                                \
        {0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22},   /* PA8 */                                                \
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, /* PB7: encoder 6 */                                     \
    }
#define FM1P_PLAY_GREEN_POS ((8u << 3) | 1u)     /* (column << 3) | row bit: column 8, row bit 1 (ui_input.c) */
/* Felucca's PANEL_DEFAULT (panel.c): label -> matrix button, role -> matrix encoder, all clockwise + */
static inline void fm1p_panel_default(fm1panel_t *p)
{
    static const uint8_t BTN[FM1P_NB] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
    static const uint8_t ENC[FM1P_NE - 1] = {0, 1, 6, 2, 3, 4, 5};
    int i;
    memcpy(p->btn_id, BTN, sizeof BTN);
    memcpy(p->enc_id, ENC, sizeof ENC);
    for (i = 0; i < FM1P_NE - 1; i++)
        p->enc_dir[i] = 1;
}

/* --------------------------------------------------------------- output --- */
/* packed row-bit planes (plane[c] bit r = the LED at column c, row bit r; r 1..4 = line 0..3) -> the panel's levels.
 * planes[0] has priority (an LED in two planes takes the first one's level), so list lit first. play_green_pos:
 * (col << 3) | row bit of PLAY's green LED, or 0 for none. Returns 1 if any level changed. */
static inline int fm1p_leds_planes(fm1panel_t *p, const uint8_t *const *planes, const uint8_t *levels, int n,
                                   uint32_t play_green_pos)
{
    int changed = 0, c, l, k;
    for (c = 0; c < FM1P_NCOL; c++)
        for (l = 0; l < FM1P_NLINE; l++) {
            uint8_t v = FM1P_LED_OFF;
            for (k = 0; k < n; k++)
                if ((planes[k][c] >> (l + 1)) & 1u) {
                    v = levels[k];
                    break;
                }
            if (p->led[c][l] != v) {
                p->led[c][l] = v;
                changed = 1;
            }
        }
    if (play_green_pos) {
        const uint32_t col = play_green_pos >> 3, line = (play_green_pos & 7u) - 1u;
        const uint8_t g = col < FM1P_NCOL && line < FM1P_NLINE ? p->led[col][line] : 0;
        if (p->led_play_green != g) {
            p->led_play_green = g;
            changed = 1;
        }
    }
    return changed;
}
/* where key id `id` (0..13 matrix button, 14..40 note key) sits: 1 and its column / line, 0 none */
static inline int fm1p_led_pos(const int8_t *keymap, int id, int *col, int *line)
{
    int r, c;
    for (r = 1; r <= FM1P_NLINE; r++)
        for (c = 0; c < FM1P_NCOL; c++)
            if (keymap[r * FM1P_NCOL + c] == id) {
                *col = c;
                *line = r - 1;
                return 1;
            }
    return 0;
}
/* one LED by key id (a firmware that sets LEDs per key: fm1_led_key(id, on), a level per id); 1 if it changed */
static inline int fm1p_led_id(fm1panel_t *p, const int8_t *keymap, int id, uint8_t level)
{
    int c, l;
    if (!fm1p_led_pos(keymap, id, &c, &l) || p->led[c][l] == level)
        return 0;
    p->led[c][l] = level;
    return 1;
}

/* the LCD: a big-endian frame buffer that counts its own writes (emu_hal_t.lcd / lcd_writes): copied when the count
 * moved; the panel's count becomes the firmware's. 1 if copied */
static inline int fm1p_lcd_out(fm1panel_t *p, const uint16_t *lcd_be, uint32_t writes)
{
    if (writes == p->lcd_writes)
        return 0;
    memcpy(p->lcd, lcd_be, sizeof p->lcd);
    p->lcd_writes = writes;
    return 1;
}
/* a native-endian RGB565 frame buffer with no count (OberVave's fb[]): swapped in, the count bumped when anything
 * differs. 1 if it changed */
static inline int fm1p_lcd_out_native(fm1panel_t *p, const uint16_t *fb)
{
    uint32_t i, diff = 0;
    for (i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++) {
        const uint16_t s = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        diff |= (uint32_t)(p->lcd[i] ^ s);
        p->lcd[i] = s;
    }
    if (diff)
        p->lcd_writes++;
    return diff != 0;
}
