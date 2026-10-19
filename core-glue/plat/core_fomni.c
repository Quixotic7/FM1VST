/* SPDX-License-Identifier: GPL-3.0-only */
/* The FoMni core: FoMni-1 (https://github.com/charlesvestal/fm1-fomni, the pinned, unmodified submodule
 * cores/fm1-fomni) built as one unit with the plat glue (core-glue/plat/README.md). The include list follows the
 * device's unity build (firmware/src/main.c) with the hardware left out, as the firmware's own host simulator
 * (host/omni_host.c) does: gfx.c, plat.h, Felucca's storage.c (on the NOR image), the app's header, the screen
 * reader (announce.c, nobody listening), the panel calibration (panel.c), the device's plat.h implementation
 * (plat_fm1.c), the project and the UI. The instrument (dsp/omni.c) is its own unit (fomni_omni.c).
 * Its version is a build-time define (OM_VERSION, "DEV" by default): the hub's version (core_fomni.cmake). */
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
#define OM_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_8000400"          /* the package identity: build.py --release 0.4 (FM-1_8 00 04 00) */
#define FM1CORE_PL_SX 1                   /* the installer's M-UPGRADE identity (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator) */
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
#include "storage.c"
#include "app/app.h"
#define ANN_NAME "FoMni-1 " OM_VERSION
#define ANN_DATA OM_POOL
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), not sent */
{
    return fm1core_pl_sx_send(p, n);
}
#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/project.c"
#include "app/ui.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* The installer (the hub's, fm1ota-style) finds the unit by the M-UPGRADE identity: main_fm1.c's loop runs
 * the screen reader's frame, then ota_service (the firmware's ota.c, through common/sysex_ota.h). The update key
 * F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): no read requests follow (the installer
 * times out), nothing is written, the core runs on. */
#define FM1SX_OTA_C "../../cores/fm1-fomni/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
static int fm1core_fomni_sx_accept(const uint8_t *f, uint32_t n)
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n);
}
static void fm1core_fomni_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    const uint8_t *p;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &p, &n) && ann_frame(p, n, fm1_ms))   /* a screen reader's, before the updater */
        fm1sx_done(&fm1core_pl_sxi);
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_fomni_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_fomni_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "FoMni"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the buttons' glow: one scan frame in GLOW_DIV */
static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main */
{
    om_init();
    project_load();                       /* defaults when nothing (or another format) is saved */
    project_apply();
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n) { om_render(lr, n, master_q12); }
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
    if (om_playing)
        return 0;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = plat_ms();
    }
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u chord %d playing %u dirty %u", (unsigned)ui.view, (int)ui.pad, (unsigned)om_playing,
             (unsigned)ui.dirty);
}

#include "plat_glue.h"
#include "exit_end.h"
