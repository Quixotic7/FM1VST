/* SPDX-License-Identifier: GPL-3.0-only */
/* CONDUCTOR's fake FM-1 for the conductor core (core_conductor.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under DINGHY_HOST; that file is a program (a script, a WAV, PPM files,
 * main(), exit at its end), so core_conductor.cmake maps the path to this file with clang's -ivfsoverlay: the
 * firmware tree stays unmodified.
 *
 * The shape is CONDUCTOR's tests/host.c (Dinghy's, zvenson), the FM-1 board, minus its script, files and main(), with:
 *   - the panel the glue drives (core_conductor.c fm1core_dg_hal_in): fm1_in, press and release edges, the encoders,
 *     the LEDs per id and their dim layer
 *   - the flash as on the device (platform.c's device branch) over common/nor.c's image: settings / blobs / slots in
 *     platform.c's record format ([magic][length][crc32] then the bytes, the header written last) at store.h's FM-1
 *     offsets, with the app's own STORE_MAGIC ("CNDR"; slots SLOT ^ CNDR), so a unit's dump boots here and the other
 *     way round. CONDUCTOR keeps its scores in the slots (SAVE 1..7 and the work in progress)
 *   - USB-MIDI in (midi_get, source 1) and out (midi_send) through the glue's queues
 *   - platform_poll: the coroutine's yield (dinghy_pre.h). The clock (fm1_ms), the input and the audio are the glue's
 *     (tick, hal_in, the ISR), not the poll's */
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
#define HOST_NENC 7
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel;
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_rel;
    host_rel = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (fm1_in.notes is the glue's) */
static int32_t host_enc[HOST_NENC];
static int32_t fm1_enc_take(uint32_t e) { int32_t v = host_enc[e % HOST_NENC]; host_enc[e % HOST_NENC] = 0; return v; }
static uint64_t host_leds, host_glow;           /* bit id: LED id lit / glowing (0..13 buttons, 14..40 keys) */
static void fm1_irq_off(void) {}                 /* (the audio runs between the main loop's passes) */
static void fm1_irq_on(void) {}
static void fm1_led_key(uint32_t id, int on) { if (on) host_leds |= (uint64_t)1 << id; else host_leds &= ~((uint64_t)1 << id); }
static void fm1_led_key_glow(uint32_t id, int on) { if (on) host_glow |= (uint64_t)1 << id; else host_glow &= ~((uint64_t)1 << id); }

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

/* storage: platform.c's area_load / area_save over the NOR image, at store.h's FM-1 offsets */
#include "store.h"
#define FM1CORE_CND_SLOT_MAGIC 0x544F4C53u    /* platform.c SLOT_MAGIC "SLOT" */
static int fm1core_cnd_load(uint32_t at, uint32_t max, uint32_t magic, void *p, uint32_t n)
{
    uint32_t h[3];
    if (n > max || fm1nor_read(at, h, sizeof h) || h[0] != magic || h[1] != n)
        return -1;
    if (fm1core_dg_crc32(fm1nor_image + at + sizeof h, n) != h[2])
        return -1;
    memcpy(p, fm1nor_image + at + sizeof h, n);
    return 0;
}
static int fm1core_cnd_save(uint32_t at, uint32_t sectors, uint32_t magic, const void *p, uint32_t n)
{
    uint32_t h[3] = {magic, n, 0}, k;
    if (n > sectors * 0x1000u - sizeof h)
        return -1;
    h[2] = fm1core_dg_crc32((const uint8_t *)p, n);
    for (k = 0; k < sectors; k++)
        if (fm1nor_erase(at + k * 0x1000u))
            return -8;
    if (fm1nor_prog_span(at + sizeof h, p, n))
        return -8;
    return fm1nor_prog_span(at, h, sizeof h);
}
static int settings_load(void *p, uint32_t n) { return fm1core_cnd_load(SETTINGS_AT, SECTOR_MAX, STORE_MAGIC, p, n); }
static int settings_save(const void *p, uint32_t n) { return fm1core_cnd_save(SETTINGS_AT, 1u, STORE_MAGIC, p, n); }
static int blob_load(uint32_t i, void *p, uint32_t n) { return i < NBLOB ? fm1core_cnd_load(BLOBS_AT + i * 0x1000u, SECTOR_MAX, STORE_MAGIC, p, n) : -1; }
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? fm1core_cnd_save(BLOBS_AT + i * 0x1000u, 1u, STORE_MAGIC, p, n) : -1; }
static int slot_load(uint32_t i, void *p, uint32_t n)
{
    return i < NSLOT ? fm1core_cnd_load(SLOTS_AT + i * SLOT_SECT * 0x1000u, SLOT_MAX, FM1CORE_CND_SLOT_MAGIC ^ STORE_MAGIC, p, n) : -1;
}
static int slot_save(uint32_t i, const void *p, uint32_t n)
{
    return i < NSLOT ? fm1core_cnd_save(SLOTS_AT + i * SLOT_SECT * 0x1000u, SLOT_SECT, FM1CORE_CND_SLOT_MAGIC ^ STORE_MAGIC, p, n) : -1;
}

/* MIDI in (the glue's fm1core_dg_midi_put fills it: USB, source 1) and out */
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
