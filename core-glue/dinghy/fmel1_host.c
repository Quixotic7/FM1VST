/* SPDX-License-Identifier: GPL-3.0-only */
/* FMEL-1's fake FM-1 for the FMEL-1 core (core_fmel1.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under DINGHY_HOST; that file is a program (a script, a WAV, PPM files,
 * main(), exit at its end), so core_fmel1.cmake maps the path to this file with clang's -ivfsoverlay: the firmware
 * tree stays unmodified.
 *
 * The shape is FMEL-1's tests/host.c (Dinghy's, zvenson; AntonioAltea's), minus its script, files and main(), with:
 *   - the panel the glue drives (core_fmel1.c fm1core_dg_hal_in): fm1_in, press and release edges, the encoders,
 *     the LEDs per id (lit or off)
 *   - the settings as on the device (platform.c's device branch) over common/nor.c's image: settings_load /
 *     settings_save in its DNGY sector format at 0xFC000, so a unit's dump boots here and the other way round
 *   - USB-MIDI out (midi_send) into the glue's queue
 *   - platform_poll: the coroutine's yield (dinghy_pre.h). The clock (fm1_ms), the input and the audio are the glue's
 *     (tick, hal_in, the ISR), not the poll's
 *   - the battery as host.c has it (a reading of 600, no USB host): the header's icon as its own host sim draws it */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
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
static int32_t host_enc[7];
static int32_t fm1_enc_take(uint32_t e) { int32_t d = host_enc[e % 7u]; host_enc[e % 7u] = 0; return d; }
static uint32_t host_led[2];                    /* bit id: buttons 0..13, keys 14..40 */
static void fm1_led_key(uint32_t id, int on)
{
    if (id < 64u) {
        if (on) host_led[id >> 5] |= 1u << (id & 31u);
        else host_led[id >> 5] &= ~(1u << (id & 31u));
    }
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
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* the settings: platform.c's sector at 0xFC000, [DNGY][length][crc32] then the bytes (dinghy_pre.h); a record of
 * another length does not load (mel.c tries its older layouts in turn, as on the device) */
#define FM1CORE_MEL_SETTINGS_AT 0x000FC000u
static uint32_t fm1core_mel_saves;
static int settings_load(void *p, uint32_t n)
{
    if (n > 256u) return -1;
    return fm1core_dg_sector_load(FM1CORE_MEL_SETTINGS_AT, p, n);
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > 256u) return -1;
    fm1core_mel_saves++;
    return fm1core_dg_sector_save(FM1CORE_MEL_SETTINGS_AT, p, n);
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
static int32_t batt_raw(void) { return 600; }
static int usb_powered(void) { return 0; }
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void platform_poll(void) { fm1core_dg_poll(); }
