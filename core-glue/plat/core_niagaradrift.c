/* SPDX-License-Identifier: GPL-3.0-only */
/* The Niagara Drift core: Niagara Drift (https://github.com/evanyerburgh/niagara-drift, the pinned, unmodified
 * submodule cores/niagara-drift, a FoMni fork with a two-drone engine) built as one unit with the plat glue
 * (core-glue/plat/README.md). The include list follows the device's unity build (firmware/src/main.c) with the
 * hardware left out, as the firmware's own host simulator (host/omni_host.c) does: gfx.c, plat.h, Felucca's storage.c
 * (on the NOR image: the sound and bank A, the panel calibration, banks B and C), the app's header, the key pressure
 * (sense.c), the panel calibration (panel.c), the device's plat.h implementation (plat_fm1.c), the project and the UI.
 * No screen reader. The drone engine (dsp/drone.c, with drone_fx.c) is its own unit (niagaradrift_drone.c).
 * Its version is a build-time define (OM_VERSION, "DEV" by default): the hub's version (core_niagaradrift.cmake). */
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
#define FELUCCA_ID "FM-1_8000201"          /* the package identity: build.py --release 0.2.1 (FM-1_8 00 02 00) */
#define FM1CORE_PL_SX 1                   /* the installer's M-UPGRADE identity (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ------------------------------------------- what plat_fm1.c needs beyond the HAL half --- */
/* MONK-1's HAL additions (firmware/hal/fm1_input.h, fm1_adc.h, fm1_jack.h): the LED PWM frames, the analog row
 * sampling's settings, the headphone jack. No analog samples reach sense.c here (the ABI's keys are digital): every
 * key stays open to it, so plat_pressure is 0 while held and the pressure crossfade does not move. */
#define FM1_LED_PHASES 32u
static uint8_t fm1_led_pwm[FM1_LED_PHASES][FM1P_NCOL];   /* the LEDs lit in each of 32 frames (a level's share) */
static struct {
    volatile int32_t master;
    uint8_t immediate, drive, probe;
    uint16_t con;
} fm1_an;
#define FM1_ADC_CON(div, delay) ((uint32_t)((div) & 7u) | 8u | ((uint32_t)((delay) & 15u) << 12))
static int fm1_jack_phones(void) { return 1; }   /* (a computer's output, no Speaker low cut: as web/emu does) */

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator) */
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
#include "storage.c"
#include "app/app.h"
#include "app/sense.c"
#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/project.c"
#include "app/ui.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* The installer (the hub's, fm1ota-style) finds the unit by the M-UPGRADE identity: main_fm1.c's loop runs
 * dbg_service (ota.c's frames: the identity; its BACKUP / RESTORE and load commands are main_fm1.c's, not built, so
 * not answered). The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): no read
 * requests follow (the installer times out), nothing is written, the core runs on. */
#define FM1SX_OTA_C "../../cores/niagara-drift/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
static int fm1core_niagaradrift_sx_accept(const uint8_t *f, uint32_t n)
{
    return fm1sx_is_mupg(f, n);
}
static void fm1core_niagaradrift_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_niagaradrift_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_niagaradrift_sx_service()

/* ---------------------------------------------------------------- the LEDs --- */
/* plat_fm1.c draws the levels below 255 as PWM frames (fm1_led_pwm: lit in d of 32): to the ABI's planes, lit in
 * half the frames or more the mid level, fewer the dim one */
static uint8_t fm1core_nd_mid[FM1P_NCOL];
static void fm1core_nd_leds(void)
{
    uint32_t c, r, f;
    for (c = 0; c < FM1P_NCOL; c++) {
        uint8_t mid = 0, dim = 0;
        for (r = 0; r < 8u; r++) {
            const uint8_t m = (uint8_t)(1u << r);
            uint32_t on = 0;
            if (fm1_led[c] & m)
                continue;
            for (f = 0; f < FM1_LED_PHASES; f++)
                on += (fm1_led_pwm[f][c] & m) != 0;
            if (on >= FM1_LED_PHASES / 2u)
                mid |= m;
            else if (on)
                dim |= m;
        }
        fm1core_nd_mid[c] = mid;
        fm1_led_dim[c] = dim;
    }
}

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "Niagara Drift"
#define FM1CORE_PL_UI_FRAME() (ui_frame(), fm1core_nd_leds())
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the dim PWM levels */
#define FM1CORE_PL_DIM_ON 1               /* (no glow divider: the PWM frames instead) */
#define FM1CORE_PL_MID_PLANE fm1core_nd_mid
static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main */
{
    plat_pressure_init();
    dr_init();
    project_load();                       /* defaults when nothing (or another format) is saved */
    project_apply();
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n) { dr_render(lr, n, master_q12); }
static void fm1core_pl_master(int32_t knob)   /* main_fm1.c: a square law to the clean top (3277), then the drive */
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = k10 < 819u ? 3277u * k10 * k10 / (819u * 819u) : 3277u + (k10 - 819u) * 819u / 204u;
}
static void fm1core_pl_loop(void) { plat_pressure_poll(); }
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        ui_say("UPDATE MODE IN", d);
    } else {
        ui_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save now (ui.c autosave: a few seconds after the last change, the drone stopped, nothing touched); while
 * the drone plays or fades the device never erases: deferred */
static int fm1core_pl_sync(void)
{
    if (!ui.dirty)
        return 1;
    if (ui.playing || dr_fade > 0.0f)
        return 0;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = plat_ms();
    }
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u bank %u scene %d playing %u dirty %u", (unsigned)ui.view, (unsigned)proj.bank,
             (int)ui.scene_at, (unsigned)ui.playing, (unsigned)ui.dirty);
}

#include "plat_glue.h"
#include "exit_end.h"
