/* SPDX-License-Identifier: GPL-3.0-only */
/* The ZEN-1 core: ZEN-1 (https://github.com/aceyprime/zen-1, the pinned, unmodified submodule cores/zen-1; on FuMi-1,
 * on FoMni) built as one unit with the plat glue (core-glue/plat/README.md), from FoMni's glue. The include list
 * follows the device's unity build (firmware/src/main.c) with the hardware left out, as the firmware's own host
 * simulator (host/zen_host.c) does: gfx.c, the kanji (zen_kanji.h), plat.h, Felucca's storage.c (on the NOR image),
 * the app's header, the panel calibration (panel.c), the device's plat.h implementation (plat_fm1.c), the project
 * and the UI (its scenes). No screen reader (ZEN-1 has none). The instrument (dsp/fumi.c, with the samples and
 * patches it includes) is its own unit (zen1_fumi.c). Its version is a build-time define (FM_VERSION, "DEV" by
 * default): the glue's VERSION (core_zen1.cmake). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>                       /* before exit.h: its prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "nor.h"
#include "fifo.h"
#include "ticks.h"
#include "panel.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#define OM_HOST 1                         /* the device's .pool section off (plat.h OM_POOL) */
#define FM_HOST 1                         /* (dsp/fumi.c FM_POOL, as host/zen_host.c) */
#define FM_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_6010200"          /* the package identity: build.py --release 1.2 (FM-1_6 01 02 00) */
#define FM1CORE_PL_SX 1                   /* the installer's M-UPGRADE identity (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"
/* what ZEN-1's sources take from the hardware layer and the host that plat_hal.h has no use for: the LED matrix's lines
 * (plat_fm1.c's led_hush around a flash write: the panel here has no scan to darken) and, under OM_HOST, the host's
 * view of the screen for render8.c's stale-band count (the panel's RAM as sent) */
static void fm1__led_lines(uint32_t rowmask) { (void)rowmask; }
static uint16_t lcd_view_host(uint32_t i) { return fm1core_pl_lcd[i % (FM1P_LCD_W * FM1P_LCD_H)]; }

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator) */
#include "gfx.c"
#include "app/zen_kanji.h"                /* (main.c: after gfx.c, before plat.h) */
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
#include "storage.c"
#include "app/app.h"
#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/project.c"
#include "app/ui.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* The installer (the hub's, fm1ota-style) finds the unit by the M-UPGRADE identity: main_fm1.c's loop runs
 * ota_service (the firmware's ota.c, through common/sysex_ota.h). The update key
 * F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): no read requests follow (the installer
 * times out), nothing is written, the core runs on. */
#define FM1SX_OTA_C "../../cores/zen-1/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
static int fm1core_zen1_sx_accept(const uint8_t *f, uint32_t n)
{
    return fm1sx_is_mupg(f, n);
}
static void fm1core_zen1_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_zen1_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_zen1_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "ZEN-1"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the buttons' glow: one scan frame in GLOW_DIV */
static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main */
{
    fm_init();
    project_load();                       /* defaults when nothing (or another format) is saved */
    project_apply();
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}
/* main_fm1.c's render count for plat_fm1.c's journal timing (a render inside a program's time: none here, the
 * ISR never interrupts the main loop) */
static uint32_t fm1core_zen_halves;
static uint32_t audio_halves(void) { return fm1core_zen_halves; }
static uint32_t audio_last_us(void) { return 0u; }
static void fm1core_pl_render(int32_t *lr, uint32_t n)
{
    fm_render(lr, n, master_q12);
    fm1core_zen_halves++;
}
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;
}
static void fm1core_pl_loop(void) {}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        ui_say("UPDATE MODE IN", d);
    } else {
        ui_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save now (AUTOSAVE: a few seconds after a change, nothing sounding, nothing touched); while the rhythm
 * plays the device never erases: deferred */
static int fm1core_pl_sync(void)
{
    if (!ui.dirty)
        return 1;
    if (fm_nvoices)
        return 0;                         /* (ui.c's autosave waits for silence too) */
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = plat_ms();
    }
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u voices %u dirty %u", (unsigned)ui.view, (unsigned)fm_nvoices, (unsigned)ui.dirty);
}

#include "plat_glue.h"
#include "exit_end.h"
