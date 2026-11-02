/* SPDX-License-Identifier: GPL-3.0-only */
/* Smileyface's fake FM-1 for the Smileyface core (core_smileyface.c), in place of its tests/host.c. platform.c
 * includes "../../tests/host.c" by a relative path under SMILEY_HOST; that file is a program (a script, a WAV, PPM
 * files, main(), exit at its end) or, under SMILEY_WEB, the browser's (emscripten_sleep, main), so
 * core_smileyface.cmake maps the path to this file with clang's -ivfsoverlay: the firmware tree stays unmodified.
 *
 * The shape is Smileyface's tests/host.c (Dinghy's, zvenson; pbarilla's), minus its script, files and main(), with:
 *   - the panel the glue drives (core_smileyface.c fm1core_dg_hal_in): fm1_in, press and release edges, the encoders,
 *     the LEDs per id (on, glow)
 *   - the flash as on the device (platform.c's device branch) over common/nor.c's image: pf_read / pf_erase /
 *     pf_write over the projects' area at PROJ_AT, settings_load / settings_save in its DNGY sector format at
 *     0xFC000, so a unit's dump boots here and the other way round
 *   - USB-MIDI out (midi_send) into the glue's queue
 *   - platform_poll: the coroutine's yield (dinghy_pre.h). The clock (fm1_ms), the input and the audio are the glue's
 *     (tick, hal_in, the ISR), not the poll's
 *   - ota.c's prototypes the projects' SysEx (projio.c, overlaid with its device half on: core_smileyface.cmake)
 *     calls before ota.c is included (dinghy_glue.h, FM1CORE_DG_OTA_C) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "SMILEYFACE"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel;
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = host_rel; host_rel = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (fm1_in.notes is the glue's) */
static int32_t enc_acc[8];
static int32_t fm1_enc_take(uint32_t e) { int32_t v = enc_acc[e & 7u]; enc_acc[e & 7u] = 0; return v; }
static uint8_t host_led[64];                    /* 0 off, 1 glow, 2 on */
static void fm1_led_key(uint32_t id, int on) { if (id < 64u) host_led[id] = on ? 2 : (host_led[id] == 2 ? 0 : host_led[id]); }
static void fm1_led_glow(uint32_t id, int on) { if (id < 64u && host_led[id] != 2) host_led[id] = on ? 1 : 0; }

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
#define CV_MAX (240u * 96u)
#include "gfx.c"

/* the projects' flash (platform.c pf_*: read anywhere, a 4 KiB erase, a write that only clears bits), at the device's
 * offsets in the NOR image */
static int pf_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int pf_erase(uint32_t off)
{
    if (off < PROJ_AT || off + 0x1000u > PROJ_AT + PROJ_LEN || (off & 0xFFFu)) return -1;
    return fm1nor_erase(off);
}
static int pf_write(uint32_t off, const void *p, uint32_t n)
{
    if (off < PROJ_AT || off + n > PROJ_AT + PROJ_LEN) return -1;
    return fm1nor_prog_span(off, p, n);
}
/* the settings: platform.c's sector at 0xFC000, [DNGY][length][crc32] then the bytes (dinghy_pre.h) */
#define FM1CORE_SF_SETTINGS_AT 0x000FC000u
static uint32_t fm1core_sf_saves;
static int settings_load(void *p, uint32_t n)
{
    if (n > 256u) return -1;
    return fm1core_dg_sector_load(FM1CORE_SF_SETTINGS_AT, p, n);
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > 256u) return -1;
    fm1core_sf_saves++;
    return fm1core_dg_sector_save(FM1CORE_SF_SETTINGS_AT, p, n);
}

/* MIDI in (the glue's fm1core_dg_midi_put fills it) and out */
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
static void platform_poll(void) { fm1core_dg_poll(); }

/* ota.c's (included after the firmware by dinghy_glue.h), called by projio.c's px_reply before then */
static int ota_wire_send(const uint8_t *p, uint32_t n);
static uint32_t ota_pack7(const uint8_t *in, uint32_t n, uint8_t *out);
