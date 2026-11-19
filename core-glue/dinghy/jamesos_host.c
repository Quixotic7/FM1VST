/* SPDX-License-Identifier: GPL-3.0-only */
/* JamesOS's fake FM-1 for the JamesOS core (core_jamesos.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under DINGHY_HOST; that file is a program (a script, a WAV, PPM files,
 * main(), exit at its end), so core_jamesos.cmake maps the path to this file with clang's -ivfsoverlay: the firmware
 * tree stays unmodified.
 *
 * The shape is JamesOS's tests/host.c (Dinghy's, zvenson; modified for JamesOS by Aaron Grando), FM-1 product only,
 * minus its script, WAV and PPM files and main(), with:
 *   - the panel the glue drives (core_jamesos.c fm1core_dg_hal_in): fm1_in, press and release edges, the note-key
 *     edges with their 24 MHz stamps, the encoders, the LEDs per id at the four levels of fm1_led_levels.h
 *   - the settings (the session record SAVE writes) as on the device: platform.c's settings_load / settings_save,
 *     [magic "DNGY"][length][crc32] then the bytes, one 4 KiB sector at 0xFC000, over common/nor.c's image
 *     (dinghy_pre.h's sector format is the same); host_store[0] says whether that sector's header is written (the
 *     session code's "nonempty" check under DINGHY_HOST)
 *   - MIDI in with its arrival stamp (the glue's fm1core_dg_midi_put fills mq), MIDI out (midi_send) into the glue's
 *     queue
 *   - the clock: fm1_ms is the glue's (tick); host_ticks, the fake 24 MHz timer, follows it at every poll
 *   - platform_poll: the coroutine's yield (dinghy_pre.h). The audio is the glue's ISR, not the poll's. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/hal/fm1_cpu1_worker.h"

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS FM1_AUDIO_RATE_HZ
#define HALF_FRAMES FM1_AUDIO_HALF_FRAMES

static volatile uint32_t fm1_ms;
static int32_t host_battery_raw = 600;          /* a full battery (USB power) */
static int32_t fm1_battery_level(void) { return fm1_battery_bars_from_adc(host_battery_raw); }
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_pressed, host_released, host_note_pressed;
static int32_t host_encoder[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_pressed;
    host_pressed = 0;
    *released = host_released;
    host_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t b = host_note_pressed; host_note_pressed = 0; return b; }
static uint32_t fm1_input_note_snapshot(uint32_t *held)
{
    uint32_t b = host_note_pressed;
    host_note_pressed = 0;
    if (held) *held = fm1_in.notes;
    return b;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t n = e < 7u ? host_encoder[e] : 0;
    if (e < 7u) host_encoder[e] = 0;
    return n;
}
static uint8_t host_led_state[FM1_LED_COUNT];
static uint8_t host_led_level_state[FM1_LED_COUNT];
static void fm1_led_level(uint32_t id, fm1_led_level_t level)
{
    if (id >= sizeof host_led_state || level < FM1_LED_OFF || level > FM1_LED_BRIGHT)
        return;
    host_led_level_state[id] = (uint8_t)level;
    host_led_state[id] = level == FM1_LED_BRIGHT;
}
static void fm1_led_key(uint32_t id, int on)
{
    fm1_led_level(id, on ? FM1_LED_BRIGHT : FM1_LED_OFF);
}

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    BR_UIT_LCD_BYTES((x < 240u && y < 240u) ? (x + w > 240u ? 240u - x : w) * (y + h > 240u ? 240u - y : h) * 2u : 0u);
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u)
        return;                                 /* (tests/host.c aborts: an app bug, never seen) */
    BR_UIT_LCD_BYTES(w * h * 2u);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint16_t c = px[j * w + i];
            fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
        }
}
#define CV_MAX (240u * 40u)
#include "gfx.c"

/* the settings: platform.c's device record at its device offset, over the NOR image */
#define FM1CORE_JO_SETTINGS_AT 0x000FC000u
#define HOST_SETTINGS_PAYLOAD_CAPACITY (4096u - 3u * sizeof(uint32_t))
static uint8_t host_store[4];                   /* [0]: the settings sector's header is written */
static void fm1core_jo_store_seen(void)
{
    uint32_t h[3] = {0, 0, 0};
    fm1nor_read(FM1CORE_JO_SETTINGS_AT, h, sizeof h);
    host_store[0] = h[0] != 0xFFFFFFFFu || h[1] != 0xFFFFFFFFu || h[2] != 0xFFFFFFFFu;
}
static int settings_load(void *p, uint32_t n)
{
    if (n > HOST_SETTINGS_PAYLOAD_CAPACITY) return -1;
    return fm1core_dg_sector_load(FM1CORE_JO_SETTINGS_AT, p, n);
}
static int settings_save(const void *p, uint32_t n)
{
    int rc;
    if (n > HOST_SETTINGS_PAYLOAD_CAPACITY) return -1;
    rc = fm1core_dg_sector_save(FM1CORE_JO_SETTINGS_AT, p, n);
    fm1core_jo_store_seen();
    return rc;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
/* Played-note timing: the fake 24 MHz timer (host_ticks: platform_poll keeps it at fm1_ms), each MIDI message's
 * arrival and each note key's last change, as the device records them. */
static uint32_t host_ticks, mq_time[64], midi_last_timestamp, host_note_tick[27], host_note_stamped;
static uint8_t mq_stamped[64];
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    midi_last_timestamp = mq_stamped[mq_r % 64u] ? mq_time[mq_r % 64u] : host_ticks;
    mq_stamped[mq_r % 64u] = 0u;
    mq_r++;
    return 1;
}
static uint32_t host_note_key_tick(uint32_t k)
{
    return k < 27u && !(((fm1_in.notes ^ host_note_stamped) >> k) & 1u) ? host_note_tick[k] : host_ticks;
}
#define BR_NOTE_NOW() host_ticks
#define BR_NOTE_MIDI_TICK() midi_last_timestamp
#define BR_NOTE_KEY_TICK(k) host_note_key_tick(k)
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }

static uint32_t app_audio(int32_t *out, uint32_t n);
static void app_main(void);

static void platform_poll(void)                 /* one call = one pass; the clock is the glue's */
{
    fm1core_dg_poll();
    host_ticks = fm1_ms * 24000u;
}

/* The device boot order: the shared immutable resources (the packed Plaits waves) before the voices; once. */
static int host_init_resources(void)
{
#if FM1_PLAITS_PACK_WAVES && (!defined(BR_ENABLE_PLAITS) || BR_ENABLE_PLAITS)
    static int ready;
    if (!ready) {
        if (!fm1_plaits_waves_init(NULL)) return 0;
        ready = 1;
    }
#endif
    return 1;
}
