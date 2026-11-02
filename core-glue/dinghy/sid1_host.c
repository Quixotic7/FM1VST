/* SPDX-License-Identifier: GPL-3.0-only */
/* SID-1's computer-side driver for the sid1 core (core_sid1.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under SID1_HOST: that file is a program (a script, a WAV, PPM files,
 * main(), exit at its end), so core_sid1.cmake maps the path to this file with clang's -ivfsoverlay; the firmware
 * tree stays unmodified. What host.c supplies, here, under the same names:
 *   the panel      fm1_in (notes, buttons), host_edges (fm1_input_edges: presses), host_enc[] (fm1_enc_take), the
 *                  LEDs fm1_led_key per id (0..13 buttons, 14 + k keys) into fm1core_sid1_led
 *   the screen     fb[] (RGB565 native, as host.c: lcd_blit swaps gfx.c's byte-swapped pixels back), gfx.c
 *   settings       host.c's RAM record (SID-1 keeps none: its library, parts and kit are sid1.c's own store)
 *   MIDI           mq[] (midi_get; the glue fills it), midi_send into the glue's out queue (CIN = status >> 4)
 *   platform_poll  the coroutine's yield (dinghy_pre.h): one pass of the main loop per device millisecond. The clock
 *                  (fm1_ms), the panel and the audio are the glue's (tick, hal_in, the ISR), not the poll's
 * After Dinghy's tests/host.c (zvenson) and SID-1's, GPL-3.0-only. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "SID-1"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges;
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t host_enc[8];
static int32_t fm1_enc_take(uint32_t e) { int32_t t = host_enc[e & 7u]; host_enc[e & 7u] = 0; return t; }
static uint64_t fm1core_sid1_led;              /* bit id: lit */
static void fm1_led_key(uint32_t id, int on)
{
    if (id < 64u)
        fm1core_sid1_led = on ? fm1core_sid1_led | 1ull << id : fm1core_sid1_led & ~(1ull << id);
}

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > 256u) return -1;
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

static const int host_sid_log = 0;             /* (host.c's SID_LOG: off) */

static void platform_poll(void) { fm1core_dg_poll(); }
