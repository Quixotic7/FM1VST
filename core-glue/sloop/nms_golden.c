/* SPDX-License-Identifier: GPL-3.0-only */
/* No Man Sky FM's own host simulator, natively: firmware/wasm/nms_wasm.c (the site's simulator unit, built for
 * wasm32 upstream) compiled for the Mac with its section attributes dropped, driven as the site drives it
 * (sim/fm1-worklet.js: emu_render per 128-frame block, emu_frame every 6 blocks, emu_start ~900 ms after power-on
 * from sim.js) and a fixed input script, its screen written as PPM: the reference side of the No Man Sky golden
 * check (core_nomansky.cmake). Unmodified upstream; only this driver is ours.
 *
 *   nms_golden OUT_PREFIX     writes OUT_PREFIX_home.ppm (1500 ms) and OUT_PREFIX_edit.ppm (2600 ms: EDIT tapped
 *                             at 2000 ms for 100 ms) */
#include <stdio.h>
#include <stdint.h>
#define __attribute__(x)
#include "nms_wasm.c"
#undef __attribute__
#ifndef FRAME_EVERY_15
#define FRAME_EVERY_15 1
#endif

static void nms_golden_ppm(const char *prefix, const char *name)
{
    char path[1024];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        const uint16_t p = emu_fb[i];               /* (native RGB565: the ST7789 model assembled the bytes) */
        const unsigned char c[3] = {(unsigned char)(((p >> 11) & 31u) * 255u / 31u),
                                    (unsigned char)(((p >> 5) & 63u) * 255u / 63u), (unsigned char)((p & 31u) * 255u / 31u)};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "nms";
    const uint32_t edit = 1u << 6;                  /* EDIT: matrix button 6 (panel.c PANEL_DEFAULT) */
    uint32_t ms, acc = 0, blocks = 0;
    emu_init();
    for (ms = 0; ms <= 2600u; ms++) {
        if (ms == 930u)
            emu_start();                            /* (sim.js: W.boot ~900 ms after the worklet starts) */
        if (ms == 2000u)
            emu_input(0, edit);
        if (ms == 2100u)
            emu_input(0, 0);
        if (FRAME_EVERY_15 ? ms % 15u == 0u : 0)
            emu_frame();
        acc += 44100u;
        while (acc >= 1000u * 128u) {               /* the worklet's process(): 128 frames, a frame every 6 */
            acc -= 1000u * 128u;
            emu_render(128);
            if (!FRAME_EVERY_15 && ++blocks % 6u == 0u)
                emu_frame();
        }
        if (ms == 1500u)
            nms_golden_ppm(prefix, "home");
    }
    nms_golden_ppm(prefix, "edit");
    return 0;
}
