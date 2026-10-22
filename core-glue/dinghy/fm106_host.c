/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106's fake FM-1 for the FM-106 core (core_fm106.c). FM-106's own tests/host.c has fallen behind the app at
 * Beta4 and Beta5 (19 errors with Apple clang: str_cpy, fm106_now_ms, fm1_adc_read / FM1_ADC_BATT, uac's newer
 * fields, audio_lates), and it is a stripped shim (no knobs, no releases, no LEDs, no preset flash). platform.c includes
 * "../../tests/host.c" by a relative path with no guard, so core_fm106.cmake maps that path to this file with
 * clang's -ivfsoverlay: the firmware tree stays unmodified.
 *
 * The shape is FM-106's host.c (Dinghy's, zvenson; GPL-3.0-only), minus its script, files and main(), plus:
 *   - the symbols the app needs since (the survey's 6 lines: fm1_adc_read, str_cpy, fm106_now_ms, uac, audio_lates)
 *   - the panel the glue drives: press and release edges, the 7 encoders, the LEDs per id, the backlight plane
 *   - the flash as on the device: settings_load / settings_save in platform.c's sector format at 0xFC000 and
 *     presets_flash_read / presets_flash_save over 0x98000 (8 KiB, two sectors erased then written as one blob),
 *     on common/nor.c's image (the functions of platform.c's device branch over st_read / fl_erase4k / fl_write)
 *   - USB-MIDI out (midi_send, the SysEx replies of fm106_sysex_send) into the glue's queue
 *   - platform_poll: the coroutine's yield (dinghy_pre.h)
 * The dual-core worker stays off (DINGHY_DUAL_CORE is the device branch's): the engine renders inline. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "FM-106"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static uint32_t fm1_ticks(void) { return fm1_ms * 24000u; }
static inline void fm1_irq_off(void) {}
static inline void fm1_irq_on(void) {}
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led_bg[FM1_NCOL];           /* the buttons' backlight (packed row bits per column) */
static volatile uint16_t fm1_led_bg_ns;
static struct { uint32_t overruns, underruns, alt, starts, stalls, rate_sets, missed, flowing; } uac;
static volatile uint32_t audio_lates;
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(int c) { (void)c; return -1; }   /* (MASTER is the glue's; no battery here) */
static uint32_t fm106_now_ms(void) { return fm1_ms; }
static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    if (!max) return;
    while (s[i] && i + 1 < max) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* the panel (the glue writes these at the top of each pass: core_fm106.c fm1core_dg_hal_in) */
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_released;
static int32_t host_enc[8];
static uint8_t host_led[64];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_released;
    host_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t fm1_enc_take(uint32_t e) { int32_t d = e < 8u ? host_enc[e] : 0; if (e < 8u) host_enc[e] = 0; return d; }
static void fm1_led_key(uint32_t id, int on) { if (id < 64u) host_led[id] = (uint8_t)(on != 0); }

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

/* the flash: platform.c's device functions over common/nor.c (dinghy_pre.h's sector format) */
#define SETTINGS_AT 0x000FC000u
#define FM106_PRESETS_FLASH_ADDR 0x00098000u
#define FM106_PRESETS_FLASH_SIZE 0x2000u
static int settings_load(void *p, uint32_t n)
{
    return n > 256u ? -1 : fm1core_dg_sector_load(SETTINGS_AT, p, n);
}
static int settings_save(const void *p, uint32_t n)
{
    return n > 256u ? -1 : fm1core_dg_sector_save(SETTINGS_AT, p, n);
}
static int presets_flash_read(void *dst, uint32_t n) { return fm1nor_read(FM106_PRESETS_FLASH_ADDR, dst, n); }
static int presets_flash_save(const void *p, uint32_t n)
{
    if (n > FM106_PRESETS_FLASH_SIZE)
        return -1;
    if (fm1nor_erase(FM106_PRESETS_FLASH_ADDR) || fm1nor_erase(FM106_PRESETS_FLASH_ADDR + 0x1000u))
        return -8;
    return fm1nor_prog_span(FM106_PRESETS_FLASH_ADDR, p, n);
}
static int fm106_sysex_send(const uint8_t *p, uint32_t n)
{
    fm1core_dg_sysex_out(p, n);
    return 0;
}

/* USB-MIDI */
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

/* one call = one device millisecond: the glue resumes the main loop once per tick */
static void platform_poll(void) { fm1core_dg_poll(); }
