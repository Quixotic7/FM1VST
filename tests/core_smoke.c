/* SPDX-License-Identifier: GPL-3.0-only */
/* Smoke test of one core module: core_smoke MODULE.fm1core [SCREEN.ppm]
 * The module is loaded as the plugin loads it (dlopen, its one symbol fm1core_get), boots headless (RAM-only flash,
 * simulated time), must draw on its LCD and make sound when note key 9 (D4) is held from 600 ms to 900 ms (after
 * every firmware's splash), and must not halt. The clock is web/emu_web.c's run_ms(): the whole device advanced
 * one millisecond at a time on one thread. With SCREEN.ppm the LCD at the end is written there (binary PPM). */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fm1core.h"

static const fm1core_t *core;
static uint32_t dev_ms, last_frame;
static uint64_t frames_done;
static uint64_t nonzero, samples;
static int peak, peak_before, peak_note;     /* all, before the key (ms < 600), with it (600..1200) */

static void run_ms(void)
{
    uint32_t ms = dev_ms++;
    core->tick(ms);
    if (ms == 0 || ms - last_frame >= 15u) {
        last_frame = ms;
        core->frame();
    } else {
        core->idle();
    }
    while (frames_done + EMU_BLOCK <= (uint64_t)ms * EMU_FS / 1000u) {
        int16_t blk[EMU_BLOCK * 2];
        uint32_t k;
        core->audio(blk, EMU_BLOCK);
        for (k = 0; k < EMU_BLOCK * 2; k++) {
            int v = abs((int)blk[k]);
            samples++;
            if (v) nonzero++;
            if (v > peak) peak = v;
            if (ms < 600u && v > peak_before) peak_before = v;
            if (ms >= 600u && ms < 1200u && v > peak_note) peak_note = v;
        }
        frames_done += EMU_BLOCK;
    }
}

static void write_ppm(const char *path, const emu_hal_t *hal)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", EMU_LCD_W, EMU_LCD_H);
    for (i = 0; i < EMU_LCD_W * EMU_LCD_H; i++) {
        uint16_t c = (uint16_t)((hal->lcd[i] >> 8) | (hal->lcd[i] << 8));   /* big-endian as sent */
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const uint32_t key = 9;                       /* D4 */
    emu_hal_t *hal;
    void *h;
    fm1core_get_fn get;
    if (argc < 2) {
        printf("usage: core_smoke MODULE.fm1core [SCREEN.ppm]\n");
        return 2;
    }
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    get = h ? (fm1core_get_fn)dlsym(h, FM1CORE_SYMBOL) : NULL;
    if (!get) {
        printf("core_smoke: %s: %s: FAIL\n", argv[1], dlerror());
        return 1;
    }
    core = get(FM1CORE_ABI);
    if (!core || get(FM1CORE_ABI + 1u)) {
        printf("core_smoke: fm1core_get: no descriptor, or one for the wrong ABI: FAIL\n");
        return 1;
    }
    hal = core->hal;
    printf("core_smoke: core %s \"%s\" %s (%s), flash %u bytes, %u parameters, buttons %s %s .. %s\n", core->id,
           core->name, core->version, core->source_url, (unsigned)core->flash_size, (unsigned)core->nparams,
           core->button_names[0], core->button_names[1], core->button_names[EMU_NB - 1]);
    core->options(NULL, 1, 0, 1);                 /* RAM-only flash, headless */
    hal->ready = 2;                               /* simulated time, before the power-on (as web_boot) */
    core->init(0);
    while (dev_ms < 1500u) {
        if (dev_ms == 600u) {
            hal->keys_tap |= 1u << key;
            hal->keys |= 1u << key;
        } else if (dev_ms == 900u) {
            hal->keys = 0;
        }
        run_ms();
    }
    if (argc > 2)
        write_ppm(argv[2], hal);
    {
        char info[512];
        info[0] = 0;
        if (core->ui_info)
            core->ui_info(info, sizeof info);
        printf("core_smoke: screen: %s\n", info);
    }
    int ok = nonzero > 0 && peak_note > peak_before && hal->lcd_writes > 0 && !core->halted();
    printf("core_smoke: %s: %u ms, %llu frames, %llu/%llu non-zero samples, peak %d (before the key %d, with it %d), lcd_writes %u: %s\n",
           core->id, (unsigned)dev_ms, (unsigned long long)frames_done, (unsigned long long)nonzero,
           (unsigned long long)samples, peak, peak_before, peak_note, (unsigned)hal->lcd_writes, ok ? "PASS" : "FAIL");
    core->shutdown();
    return ok ? 0 : 1;
}
