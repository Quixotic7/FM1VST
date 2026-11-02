/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 SID's fake FM-1 for the sid core (core_sid.c). FM-1 SID builds its app (firmware/fm1sid.c) on its own copy of
 * Dinghy 0.3 (dinghy/src/platform.c), whose host branch (DINGHY_HOST) includes "../../tests/host.c" by a relative
 * path: FM-1 SID ships no such file (its computer builds are fm1-emu running the device binary), so core_sid.cmake
 * maps that path to this file with clang's -ivfsoverlay; the firmware tree stays unmodified.
 *
 * What platform.c's device branch gives the app, on the glue (dinghy_pre.h / dinghy_glue.h):
 *   the clock        fm1_ms (the glue's tick sets it), fm1_ticks the 24 MHz counter (common/ticks.h)
 *   the panel        fm1_in, fm1_input_edges, fm1_input_note_edges, fm1_enc_take (core_sid.c fm1core_dg_hal_in fills
 *                    them once a pass); the LEDs as Dinghy keeps them: fm1_led / fm1_led_dim, a word per keymap column,
 *                    a bit per row (hal/fm1_input.h), read back per LED id by core_sid.c
 *   the LCD          lcd_blit's strips (RGB565, byte-swapped as the ST7789 takes them) into a native frame buffer
 *                    (fm1core_sid_lcd: the app's own fb is its 4-bit picture)
 *   the waits        fm1_delay_ms: the coroutine's sleep (the app waits for the audio ISR to finish its block)
 *   the flash        st_read / fl_write / fl_erase4k over common/nor.c's image, writes refused outside the app's
 *                    FL_RANGE_OK (fm1sid.c: the update area, the settings sector, its song store)
 *   USB              the counters the SAVE page shows (a computer has it: config 1); usb_detach does nothing
 *   the rescue       recovery_main / fm1_enter_uboot (HOME held 5 s, OCT- and OCT+ held 3 s): core_sid.c's
 *   the tune pack    _pack: the pack core_sid.cmake made with the firmware's own tools/sidpack.py (sid_pack.h)
 *   the C++ statics  __init_array_start / _end: empty here (the module's loader runs reSID's constructors)
 * The firmware's shape (Dinghy's, zvenson; Felucca's by Leo Kuroshita; GPL-3.0-only). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "FM-1 SID"
#endif
#define FS 44100u                                  /* platform.c: the sample rate */
#define HALF_FRAMES 256u                           /* the I2S half: app_audio's block */
#define FM1_TICKS_PER_US 24u

static volatile uint32_t fm1_ms;
static uint32_t fm1_ticks(void) { return fm1t_ticks24(); }
static inline void fm1_irq_off(void) {}
static inline void fm1_irq_on(void) {}
static inline uint32_t irq_save(void) { return 0; }
static inline void irq_restore(uint32_t f) { (void)f; }
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(uint32_t n) { fm1core_dg_sleep(n); }

/* the panel: the keymap (hal/fm1_input.h FM1_KEYMAP, the factory one), the input as the glue hands it over */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_released, host_note_taps;
static int32_t host_enc[8];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL];   /* lit / glow: a bit per row */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    if (released)
        *released = host_released;
    host_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void)        /* keys pressed since the last look (a tap too) */
{
    uint32_t t = host_note_taps;
    host_note_taps = 0;
    return t;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t d;
    if (e >= 8u)
        return 0;
    d = host_enc[e];
    host_enc[e] = 0;
    return d;
}

/* the screen: 240 x 240 RGB565 (native), as the panel shows it */
static uint16_t fm1core_sid_lcd[240 * 240];
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                const uint16_t c = px[j * w + i];
                fm1core_sid_lcd[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}

/* the sound: fm1_audio_stop (the rescue, UBOOT) silences the ISR until core_sid.c's rescue screen is left */
static uint8_t fm1core_sid_audio_off;
static void fm1_audio_stop(void) { fm1core_sid_audio_off = 1; }

/* USB: what the SAVE page shows (usb.c's counters); the plugin's host has it as a MIDI device */
static struct {
    uint32_t up, config, suspended, resets, rx_pkts, tx_pkts, setups, sof_seen, timeouts;
} usb = {1, 1, 0, 0, 0, 0, 0, 0, 0};
static uint32_t so_w, so_r;                        /* (usb.c's SysEx out ring: none here) */
static void usb_detach(void) {}

/* the boot guard (bootguard.h): nothing to guard */
static struct { uint32_t pending; } bootguard;
static void bootguard_clear(void *g) { (void)g; }

/* the rescue and UBOOT: core_sid.c (after the app: they reset its hold timers) */
static void recovery_main(void);
static void fm1_enter_uboot(void);

/* the flash: platform.c's device functions over common/nor.c (st_read reads anywhere; erase and program only where
 * FL_RANGE_OK, fm1sid.c's, allows) */
#define SETTINGS_AT 0x000FC000u
#define FL_OTA_LO 0x000E0000u                      /* hal/fm1_flash.h */
#define FL_OTA_HI 0x000E5000u
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n) ? -1 : 0; }
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    if (!FL_RANGE_OK(off, n))
        return -8;
    return fm1nor_prog_span(off, p, n) ? -1 : 0;
}
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    if (took)
        *took = FM1NOR_ERASE_MS * 1000u * FM1_TICKS_PER_US;
    if ((off & 0xFFFu) || !FL_RANGE_OK(off, 0x1000u))
        return -8;
    return fm1nor_erase(off) ? -1 : 0;
}

/* USB-MIDI: the glue's queue in (fm1core_dg_midi_put), out as USB-MIDI packets */
static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w)
        return 0;
    *st = mq[mq_r % 64u][0];
    *d1 = mq[mq_r % 64u][1];
    *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }

/* the tune pack (core_sid.cmake: the songs FM-1 SID's release starts with, by tools/sidpack.py), at the image's end */
#include "sid_pack.h"
#define _pack fm1core_sid_pack

/* the C++ constructors app_main runs (Dinghy's startup has none): the module's loader ran them already */
typedef void (*fm1core_sid_ctor_t)(void);
static fm1core_sid_ctor_t fm1core_sid_ctors[1];
#define __init_array_start fm1core_sid_ctors
#define __init_array_end fm1core_sid_ctors

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void platform_poll(void);                   /* core_sid.c: the coroutine's yield, and the store's place */
