/* SPDX-License-Identifier: GPL-3.0-only */
/* Bytemania's fake FM-1 for the Bytemania core (core_bytemania.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under DINGHY_HOST; that file is a program (a script, a WAV, PPM files,
 * main(), exit at its end), so core_bytemania.cmake maps the path to this file with clang's -ivfsoverlay: the
 * firmware tree stays unmodified.
 *
 * The shape is Bytemania's tests/host.c (Dinghy's, zvenson; llozd's), minus its script, files and main(), with:
 *   - the panel the glue drives (core_bytemania.c fm1core_dg_hal_in): fm1_in, press and release edges, the encoders,
 *     the LEDs per id
 *   - the flash as on the device (platform.c's device branch) over common/nor.c's image: settings_load /
 *     settings_save in the DNGY sector format at 0xFC000, lib_load / lib_save (the 64 user slots) in the same record
 *     format with the app's own magic, LIB_SECTORS sectors from 0x97000, so a unit's dump boots here and the other
 *     way round
 *   - USB-MIDI out (midi_send) into the glue's queue
 *   - platform_poll: the coroutine's yield (dinghy_pre.h). The clock (fm1_ms), the input and the audio are the glue's
 *     (tick, hal_in, the ISR), not the poll's
 *   - fm1_ticks: 0, as the firmware's own host mode (the status line's DSP load reads 0) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "BYTEMANIA"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return 0; }
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel;
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = host_rel; host_rel = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (fm1_in.notes is the glue's) */
static int32_t enc_acc[8];
static int32_t fm1_enc_take(uint32_t e) { int32_t v = enc_acc[e & 7u]; enc_acc[e & 7u] = 0; return v; }
static uint8_t host_led[64];
static void fm1_led_key(uint32_t id, int on) { if (id < 64u) host_led[id] = on ? 1 : 0; }

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
#define CV_MAX (240u * 72u)
#include "gfx.c"

/* the records: platform.c's [magic][length][crc32] then the bytes, one 4 KiB sector, the header written last */
#define FM1CORE_BM_SETTINGS_AT 0x000FC000u
#define FM1CORE_BM_LIB_AT 0x00097000u
#define LIB_SECTORS 4u
#define LIB_MAX (0x1000u - 12u)
static int fm1core_bm_rec_load(uint32_t at, uint32_t magic, void *p, uint32_t n)
{
    uint32_t h[3];
    if (n > LIB_MAX || fm1nor_read(at, h, sizeof h) || h[0] != magic || h[1] != n)
        return -1;
    if (fm1core_dg_crc32(fm1nor_image + at + sizeof h, n) != h[2])
        return -1;
    memcpy(p, fm1nor_image + at + sizeof h, n);
    return 0;
}
static int fm1core_bm_rec_save(uint32_t at, uint32_t magic, const void *p, uint32_t n)
{
    uint32_t h[3] = {magic, n, 0};
    if (n > LIB_MAX)
        return -1;
    h[2] = fm1core_dg_crc32((const uint8_t *)p, n);
    if (fm1nor_erase(at) || fm1nor_prog_span(at + sizeof h, p, n))
        return -8;
    return fm1nor_prog_span(at, h, sizeof h);
}
static int settings_load(void *p, uint32_t n)
{
    if (n > 256u) return -1;
    return fm1core_bm_rec_load(FM1CORE_BM_SETTINGS_AT, FM1CORE_DG_MAGIC, p, n);
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > 256u) return -1;
    return fm1core_bm_rec_save(FM1CORE_BM_SETTINGS_AT, FM1CORE_DG_MAGIC, p, n);
}
static int lib_load(uint32_t k, uint32_t magic, void *p, uint32_t n)
{
    if (k >= LIB_SECTORS) return -1;
    return fm1core_bm_rec_load(FM1CORE_BM_LIB_AT + k * 0x1000u, magic, p, n);
}
static int lib_save(uint32_t k, uint32_t magic, const void *p, uint32_t n)
{
    if (k >= LIB_SECTORS) return -1;
    return fm1core_bm_rec_save(FM1CORE_BM_LIB_AT + k * 0x1000u, magic, p, n);
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
