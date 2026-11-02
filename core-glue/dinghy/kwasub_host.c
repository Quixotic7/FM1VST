/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WASUB's fake FM-1 for the K-WASUB core (core_kwasub.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under DINGHY_HOST; K-WASUB's tree no longer carries that file (its computer
 * builds are the browser's, web/wasm_host.c under KAWA_WASM, and tests/stress.c over it), so core_kwasub.cmake maps
 * the path to this file with clang's -ivfsoverlay: the firmware tree stays unmodified.
 *
 * The shape is web/wasm_host.c (K-WASUB's, after KAWA's and Dinghy's, zvenson) minus its WebAssembly exports, with
 * the device's halves where the browser has none:
 *   - the panel the glue drives (core_kwasub.c fm1core_dg_hal_in): fm1_in, press and release edges, the note edges,
 *     the encoders; the LEDs per id (host_leds: 0 off, 1 dim, 2 on)
 *   - the flash as on the device (platform.c's device branch) over common/nor.c's image: st_read anywhere,
 *     fl_erase4k / fl_write inside the store (STORE_LO..STORE_HI, ksub.c's 0x97000..0x9D000) and nowhere else, the
 *     settings in platform.c's DNGY sector at 0xFC000, so a unit's dump boots here and the other way round
 *   - USB-MIDI out (midi_send) into the glue's queue; the SysEx replies (ota.c's ota_wire_send, which the backup link
 *     ksub_link.c answers through as on the device) as USB-MIDI SysEx packets
 *   - ota.c itself (the M-UPGRADE identity), with this transport's hooks; its update steps are never reached
 *   - the reset's cause (fm1_boot.p3_rst: a power-on), the crash mark: ksub_ui.c rst_read reads them outside the
 *     browser build
 *   - platform_poll: the coroutine's yield (dinghy_pre.h), then the app's own poll (MIDI in, the clock out) as the
 *     device's platform_poll ends. The clock (fm1_ms), the input and the audio are the glue's (tick, hal_in, the ISR) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "K-WASUB"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define FM1_TICKS_PER_US 24u
#define RING_PUBLISH() ((void)0)
#define STORE_LO 0x00097000u                    /* (ksub.c's, set for the device build only) */
#define STORE_HI 0x0009D000u

static volatile uint32_t fm1_ms;
static uint64_t fm1core_kw_frames;              /* the audio frames rendered: the 24 MHz tick, as the browser's */
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t w_pressed, w_released, w_note_edges;
static int32_t w_enc[7];
static uint8_t w_leds[41];
static uint32_t fm1_ticks(void) { return (uint32_t)(fm1core_kw_frames * 544u); }
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = w_pressed;
    *released = w_released;
    w_pressed = w_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t e = w_note_edges; w_note_edges = 0; return e; }
static int32_t fm1_enc_take(uint32_t e) { int32_t d = w_enc[e % 7u]; w_enc[e % 7u] = 0; return d; }
static void host_leds(const uint8_t *w) { memcpy(w_leds, w, 41); }

/* the reset's cause and the crash mark (ksub_ui.c rst_read): a power-on, no crash */
static struct { uint8_t p3_rst; } fm1_boot = {1};
static uint32_t fm1_crash_mark[2];

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
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
/* libc.c: its string and number helpers; its memset / memcpy / memcmp (the device has no C library) under other
 * names, the C library's kept */
#define memset fm1core_kw_libc_memset
#define memcpy fm1core_kw_libc_memcpy
#define memcmp fm1core_kw_libc_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#define CV_MAX (240u * 64u)
#include "gfx.c"

/* the flash (platform.c's device branch): read anywhere; erase / program only the store (and, below, the settings) */
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int fm1core_kw_in_store(uint32_t off, uint32_t n) { return off >= STORE_LO && n <= STORE_HI - off; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    if (took) *took = 0;
    if (!fm1core_kw_in_store(off, 0x1000u) || (off & 0xFFFu)) return -8;
    return fm1nor_erase(off);
}
static int fl_write(uint32_t off, const uint8_t *p, uint32_t n)
{
    if (!fm1core_kw_in_store(off, n)) return -8;
    return fm1nor_prog_span(off, p, n);
}
/* the settings: platform.c's sector at 0xFC000, [DNGY][length][crc32] then the bytes (dinghy_pre.h) */
#define FM1CORE_KW_SETTINGS_AT 0x000FC000u
static int settings_load(void *p, uint32_t n)
{
    if (n > 256u) return -1;
    return fm1core_dg_sector_load(FM1CORE_KW_SETTINGS_AT, p, n);
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > 256u) return -1;
    return fm1core_dg_sector_save(FM1CORE_KW_SETTINGS_AT, p, n);
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

/* ota.c on this transport (platform.c's device branch includes it here too): the frame being handled
 * (core_kwasub.c fm1core_kw_sysex), the replies into the MIDI out queue; the update's flash steps refused (never
 * reached: nothing starts an update here). ota_show is ksub_draw.c's on the device and in the browser, left out under
 * DINGHY_HOST: a stub */
static const uint8_t *fm1core_kw_ota_p;
static uint32_t fm1core_kw_ota_n;
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    fm1core_dg_sysex_out(p, n);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!fm1core_kw_ota_p) return 0;
    *p = fm1core_kw_ota_p;
    *n = fm1core_kw_ota_n;
    return 1;
}
static void ota_frame_done(void) { fm1core_kw_ota_p = NULL; }
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "ota.c"

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void app_poll(void);
static void platform_poll(void)
{
    fm1core_dg_poll();                          /* (the next device millisecond: input and SysEx delivered) */
    app_poll();
}
