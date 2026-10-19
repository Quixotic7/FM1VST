/* SPDX-License-Identifier: GPL-3.0-only */
/* The Fart Machine One core: Fart Machine One (https://github.com/adamcharleswilsonmusic-code/Fart-Machine-One, the
 * pinned, unmodified submodule cores/Fart-Machine-One, a FoMni fork) built as one unit with the plat glue
 * (core-glue/plat/README.md). The include list follows the device's unity build (firmware/src/main.c) with the
 * hardware left out, as the firmware's own host simulator (host/omni_host.c) does: gfx.c, plat.h, Felucca's
 * storage.c (on the NOR image: only the panel calibration; the firmware keeps no project, "predictable defaults on
 * every power-on"), the app's header, the panel calibration (panel.c), the device's plat.h implementation
 * (plat_fm1.c), the project and the UI. No screen reader. The sample player (dsp/omni.c, with the eleven recordings
 * of dsp/sample_bank.h: GATED, core_fartmachine.cmake) is its own unit (fartmachine_omni.c).
 * Its version is a build-time define (OM_VERSION, "DEV" by default): the hub's version (core_fartmachine.cmake). */
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
#define FELUCCA_ID "FM-1_701"              /* the package identity: the hub package's (tools/build.py PRODUCT) */
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
#define FM1SX_OTA_C "../../cores/Fart-Machine-One/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
static int fm1core_fart_sx_accept(const uint8_t *f, uint32_t n)
{
    return fm1sx_is_mupg(f, n);
}
static void fm1core_fart_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_fart_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_fart_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "Fart Machine One"
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
static int fm1core_pl_sync(void) { return 1; }   /* (no project: nothing is ever saved lazily) */
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "bpm %u playing %u recording %u step %u", (unsigned)om_seq_bpm, (unsigned)om_seq_playing,
             (unsigned)om_seq_recording, (unsigned)om_seq_step);
}

#include "plat_glue.h"
#include "exit_end.h"
