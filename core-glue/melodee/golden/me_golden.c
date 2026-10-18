/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core's golden check: the core's screen against Melodee's own host renderer (tests/ui_render.c, built
 * from the submodule as tests/run_tests.sh builds it: the firmware's drawing code on tests/ui_test.c's stubs).
 *
 *   me_golden MODULE.fm1core UI_RENDER OUT_DIR
 *
 * ui_render draws its scenes straight from a staged state (no main loop, no clock); the core gets there from the
 * panel, through its re-implemented main.c (melodee_fw.c), with simulated time. Two scenes are reachable both ways
 * and are compared, in NIGHT (the default palette), pixel for pixel as ui_render writes them (write_ppm):
 *   calibration  OCT- + OCT+ held at power-on: Calibration (ui_input.c panel_setup, on the main loop's coroutine);
 *                the 14 buttons pressed in turn, SELECT turned right: "TURN RIGHT / ALGORITHM". ui_render draws
 *                setup_title + setup_show("TURN RIGHT", "ALGORITHM"): the whole screen must match.
 *   uboot        OCT- + OCT+ held 2.5 s on HOME: the UPDATE MODE countdown at 3 (main.c's loop, ui_draw.c
 *                draw_uboot). Below the header (y >= 30, H_HEAD) the screen must match; the header is the song's
 *                (ui_render's state() is a playing song at 124 BPM with REC armed; the core is a fresh power-on),
 *                so it is not compared.
 * Exit 0: both match; the core's screens are written next to ui_render's (OUT_DIR/core_<scene>.ppm). */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "fm1core.h"
#include "ppm.h"

static const fm1core_t *core;
static fm1panel_t *pn;
static uint32_t dev_ms, last_frame;
static uint64_t frames_done;

static void run_ms(void)
{
    const uint32_t ms = dev_ms++;
    core->tick(ms);
    if (ms == 0 || ms - last_frame >= 15u) {
        last_frame = ms;
        core->frame();
    } else {
        core->idle();
    }
    while (frames_done + FM1P_BLOCK <= (uint64_t)ms * FM1P_FS / 1000u) {
        int16_t blk[FM1P_BLOCK * 2];
        core->audio(blk, FM1P_BLOCK);
        frames_done += FM1P_BLOCK;
    }
}
static void run_to(uint32_t ms)
{
    while (dev_ms < ms && !core->halted())
        run_ms();
}

static int load(const char *module, const char *copy)
{
    const fm1core_t *(*get)(uint32_t);
    void *h;
    FILE *in = fopen(module, "rb"), *out = fopen(copy, "wb");   /* (a copy per scene: fresh globals) */
    char buf[65536];
    size_t n;
    if (!in || !out) {
        fprintf(stderr, "me_golden: cannot copy %s to %s\n", module, copy);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    h = dlopen(copy, RTLD_NOW | RTLD_LOCAL);
    if (!h || !(get = (const fm1core_t *(*)(uint32_t))dlsym(h, FM1CORE_SYMBOL)) || !(core = get(FM1CORE_ABI))) {
        fprintf(stderr, "me_golden: cannot load %s: %s\n", copy, dlerror());
        return 0;
    }
    pn = core->panel;
    dev_ms = last_frame = 0;
    frames_done = 0;
    core->options(NULL, 1, 0, 1);                 /* RAM-only flash, headless */
    pn->ready = 2;                                /* simulated time */
    return 1;
}

/* the LCD as ui_render's write_ppm writes host_screen: common/ppm.h (big-endian RGB565 -> c * 255 / 31 ..) */
/* differing pixels from row y0 down */
static uint32_t diff(const uint8_t *a, const uint8_t *b, uint32_t y0)
{
    uint32_t i, n = 0;
    for (i = y0 * 240u; i < 240u * 240u; i++)
        n += memcmp(a + 3u * i, b + 3u * i, 3) != 0;
    return n;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

int main(int argc, char **argv)
{
    static uint8_t ref[240 * 240 * 3], got[240 * 240 * 3];
    char path[1024], cmd[3072], info[256];
    uint32_t i, d;
    if (argc < 4) {
        fprintf(stderr, "usage: me_golden MODULE.fm1core UI_RENDER OUT_DIR\n");
        return 2;
    }
    printf("me_golden: %s against %s\n", argv[1], argv[2]);
    snprintf(path, sizeof path, "%s/ppm", argv[3]);
    mkdir(argv[3], 0755);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/slot", argv[3]);
    mkdir(path, 0755);
    /* Melodee's renderer (its exit status is its layout lint's: not this check's) */
    snprintf(cmd, sizeof cmd, "'%s' '%s' '%s/slot' > '%s/ui_render.log' 2>&1", argv[2], argv[3], argv[3], argv[3]);
    i = (uint32_t)system(cmd);
    printf("  (ui_render exited %u; its log: %s/ui_render.log)\n", i, argv[3]);

    /* ---- calibration */
    snprintf(path, sizeof path, "%s/ppm/NIGHT_calibration.ppm", argv[3]);
    check(fm1g_read_ppm(path, ref), "ui_render wrote NIGHT_calibration.ppm");
    snprintf(path, sizeof path, "%s/a.fm1core", argv[3]);
    if (!load(argv[1], path))
        return 1;
    pn->buttons = 3u;                             /* OCT- + OCT+ (matrix 0, 1: main.c's check) held at power-on */
    core->init(0);
    run_to(100);
    pn->buttons = 0;
    run_to(200);
    for (i = 0; i < FM1P_NB; i++) {               /* "PRESS" each label in B_NAME's order: the same table */
        pn->buttons = 1u << pn->btn_id[i];
        run_to(dev_ms + 30u);
        pn->buttons = 0;
        run_to(dev_ms + 30u);
    }
    pn->enc[pn->enc_id[FM1P_E_SELECT]] += pn->enc_dir[FM1P_E_SELECT];   /* "TURN RIGHT" SELECT */
    run_to(dev_ms + 100u);
    core->ui_info(info, sizeof info);
    fm1g_lcd_rgb(pn->lcd, got);
    snprintf(path, sizeof path, "%s/core_calibration.ppm", argv[3]);
    fm1g_write_ppm(path, got);
    d = diff(ref, got, 0);
    snprintf(cmd, sizeof cmd, "calibration (TURN RIGHT ALGORITHM, %u ms): the whole screen as ui_render's "
             "(%u pixels differ)", dev_ms, d);
    check(d == 0, cmd);
    core->shutdown();

    /* ---- uboot */
    snprintf(path, sizeof path, "%s/ppm/NIGHT_uboot.ppm", argv[3]);
    check(fm1g_read_ppm(path, ref), "ui_render wrote NIGHT_uboot.ppm");
    snprintf(path, sizeof path, "%s/b.fm1core", argv[3]);
    if (!load(argv[1], path))
        return 1;
    core->init(0);
    run_to(1000);
    pn->buttons = (1u << pn->btn_id[FM1P_B_OCTDN]) | (1u << pn->btn_id[FM1P_B_OCTUP]);
    run_to(3500);
    core->ui_info(info, sizeof info);
    fm1g_lcd_rgb(pn->lcd, got);
    snprintf(path, sizeof path, "%s/core_uboot.ppm", argv[3]);
    fm1g_write_ppm(path, got);
    d = diff(ref, got, 30);
    snprintf(cmd, sizeof cmd, "uboot (OCT- + OCT+ held 2.5 s: %s): below the header as ui_render's (%u pixels "
             "differ)", strstr(info, "uboot 3") ? "the countdown at 3" : info, d);
    check(d == 0 && strstr(info, "uboot 3") != NULL, cmd);
    pn->buttons = 0;
    run_to(3600);
    core->shutdown();
    printf("me_golden: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
