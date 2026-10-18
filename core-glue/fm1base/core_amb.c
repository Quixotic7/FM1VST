/* SPDX-License-Identifier: GPL-3.0-only */
/* The AMB-1 core: AMB-1 (https://github.com/charlesvestal/fm1-amb, the pinned, unmodified submodule cores/fm1-amb,
 * MPL-2.0, with its submodules base = fm1-base and base/bsp = felucca-bsp) built as one unit with the fm1-base glue
 * (core-glue/fm1base/README.md). The include list follows fm1-base's device unity build (base/device/unity.c) with
 * the hardware left out, as fm1-base's own host simulator (base/host/plat_host.c) does: plat.h, the app's header,
 * gfx.c, fm1-base's defaults (fm1b_config.h), the store (base/device/storage.c on the NOR image, the app's
 * APP_STORE_MAP), the screen reader (announce.c, nobody listening), the panel calibration (panel.c), the build with
 * no radio (radio_none.c), the device's plat.h implementation (base/device/plat_fm1.c), the key sensing (lib/touch.c,
 * for an app with APP_TOUCH: AMB-1 has none) and the app (app.c: the project, the UI). The engine (app/dsp/amb.c) is
 * its own unit (amb_amb.c). The shared core is the plat family's (core-glue/plat/plat_glue.h).
 *
 * AMB-1's files are MPL-2.0 (none carries the "Incompatible With Secondary Licenses" notice), announce.c MIT; this
 * glue is GPL-3.0-only (MPL-2.0 section 3.3: a Larger Work). Its version is a build-time define (APP_VERSION, "DEV" by
 * default): the hub's version (core_amb.cmake). */
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

#define APP_VERSION FM1CORE_VERSION
#include "fm1base_hal.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#ifdef APP_TOUCH
#include "touch.h"
#endif
#include "gfx.c"
#include "fm1b_config.h"
#include "storage.c"
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* (no SysEx path to a screen reader: not sent) */
{
    (void)p;
    (void)n;
    return -1;
}
#include "panel.c"
#include "radio_none.c"
#include "plat_fm1.c"
#ifdef APP_TOUCH
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#endif
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
#ifdef APP_KEY_SENSE
    if (fm1_an.con)
        app_host_tick();
#endif
}
#if defined APP_TICK
#define FM1CORE_PL_TICK_HOOK()                                                                                 \
    do {                                                                                                       \
        fm1core_fb_sense_tick();                                                                               \
        in_tick = 1;                                                                                           \
        app_tick();                       /* 1 kHz, in the timer interrupt on the device */                    \
        in_tick = 0;                                                                                           \
    } while (0)
#elif defined APP_KEY_SENSE
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
#endif
#ifdef APP_KEY_SENSE
/* the keys' velocity and pressure (an app with APP_TOUCH): the panel's key_vel / key_lean become the readings the
 * scan would hand the app, through lib/touch.c's own host model (app_host_key: a key landing at a velocity, pressed
 * a percentage more; app_host_tick: the samples every 2 ms while plat_sense is on), as fm1-base's host simulator and
 * browser emulator feed them (base/web/emu/emu_web.c web_key) */
extern fm1panel_t fm1core_pl_panel;
static uint8_t fm1core_fb_vel[NKEYS], fm1core_fb_lean[NKEYS];
static void fm1core_fb_keys(uint32_t down, uint32_t up, uint32_t held)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        const uint8_t lean = fm1p_lean(&fm1core_pl_panel, k);
        if ((down >> k) & 1u) {
            fm1core_fb_vel[k] = fm1p_vel(&fm1core_pl_panel, k, 100);   /* (none given: 100, the emulator's) */
            fm1core_fb_lean[k] = lean;
            app_host_key(k, fm1core_fb_vel[k], lean);
        } else if ((up >> k) & 1u) {
            app_host_key(k, 0, 0);
        } else if (((held >> k) & 1u) && lean != fm1core_fb_lean[k]) {
            fm1core_fb_lean[k] = lean;
            app_host_key(k, fm1core_fb_vel[k], lean);   /* the lean changed: the same landing, pressed more */
        }
    }
}
#define FM1CORE_PL_KEY_HOOK(down, up, held) fm1core_fb_keys(down, up, held)
#endif

static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the app's engine and its saved state (or defaults) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
#ifdef APP_BOOT
    app_boot();
#endif
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n) { app_render(lr, n, APP_GAIN(master_pos)); }
static void fm1core_pl_master(int32_t knob)   /* main_fm1.c: MASTER's position 0..4092 */
{
    fm1core_fb_master_adc = knob / 16;
    if (!fm1_an.con)
        fm1_an.master = (uint16_t)fm1core_fb_master_adc;
    master_pos = remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4);
}
static void fm1core_pl_loop(void)
{
    ble_poll();
#ifdef APP_LOOP
    app_loop();
#endif
}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        app_say("UPDATE MODE IN", d);
    } else {
        app_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save now (AUTOSAVE: a few seconds after a change, resting and quiet); never while the garden grows (the
 * device saves then only by SAVE): deferred */
static int fm1core_pl_sync(void)
{
    if (!ui.dirty)
        return 1;
    if (en_playing)
        return 0;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = plat_ms();
    }
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u piece %u '%s' playing %u dirty %u", (unsigned)ui.view, (unsigned)proj.piece,
             piece_name(proj.piece), (unsigned)en_playing, (unsigned)ui.dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
