/* SPDX-License-Identifier: GPL-3.0-only */
/* The QUARTZ core: QUARTZ 0.6 by f0nski (https://github.com/F0nski/quartz, the pinned, unmodified submodule
 * cores/quartz, GPL-3.0-or-later, with its own submodules base = fm1-base @ b0976f2 and base/bsp = felucca-bsp @
 * 5b0879b, MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from Mighty Arp's (core_mightyarp.c:
 * the same fm1-base and felucca-bsp commits). The include list follows fm1-base's device unity build
 * (base/device/unity.c, with QUARTZ's patches/fm1-base-layout.patch: APP_LATE after the app) with the hardware left
 * out: plat.h, the app's header, lib/touch.h, gfx.c, fm1-base's defaults (fm1b_config.h), the store (storage.c on the
 * NOR image, the app's APP_STORE_MAP and its pool of 20 sectors from 0x99000 for the project's 11 chunks), the screen
 * reader (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c), the device's plat.h
 * implementation (plat_fm1.c), lib/touch.c, the app (app.c: the sequencer, the voices, the store) and, after it, its
 * screen (APP_LATE, app_ui.c: the UI and the animated scenes). The engine (fm1.toml "o2": QUARTZ's C++ voice and
 * effects over Plaits and stmlib) is its own units (core_quartz.cmake).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.10's: "0.10" and FM-1_98001000
 * (fm1.toml id_prefix FM-1_98 + 00 06 00), from core_quartz.cmake.
 * Left out: the splash (APP_SPLASH, scene_splash.c: plat_glue.h starts the UI at power-on; app_init's boot_step is
 * NULL then, as app.c allows). Bluetooth is not built (fm1.toml ble = false; radio_none.c, as in every fm1base core).
 * The second core (fm1.toml cpu2, cpu2_fallback) is not started: one core renders every voice and the reverb, as on a
 * unit whose second core was given up (plat_cpu2_set says -1; audio.c then never hands a job over); the STATS
 * scene shows no second core.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43, this
 * fm1-base's: no lease); QUARTZ has no app_sysex and no backup (fm1.toml has neither); then ota_service (the
 * M-UPGRADE handshake: felucca-bsp's chip/ota.c through common/sysex_ota.h with the package identity) every pass.
 * Here the same, in that order. The log readout says what an emulated unit has: no crash recorded, then "end". The
 * update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): an installer gets the
 * identity, no read requests (it times out), and nothing is written.
 *
 * Saving: only SAVE writes (the project's chunks), as on the device; flash_sync has nothing pending. */
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

#ifndef APP_VERSION
#error "APP_VERSION: build.py --release's (core_quartz.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_quartz.cmake)"
#endif
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_FB_MIRROR_DIM 1           /* (this fm1-base's mirror_leds: the glow's LEDs too; mirror_mid) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "touch.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"                     /* the faces of the platform's own screens (panel.c's, unity.c's) */
#include "storage.c"
/* felucca-bsp's chip/ota.c (the M-UPGRADE identity) on the managers' transport, here where unity.c includes it:
 * plat_fm1.c's host reads (plat_host_read, plat_host_patience) are its ota_read, ota_try_ms, ota_deadline */
#define FM1SX_OTA_C "../../cores/quartz/base/bsp/chip/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
/* felucca-bsp's usb.c and guard, as plat_fm1.c reads them: the USB link never lost (a host is always there); the
 * guard's code window (plat_code_window) has no meaning here */
static volatile uint8_t midi_usb_lost;
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), not sent */
{
    return fm1core_pl_sx_send(p, n);
}
#include "panel.c"
#include "radio_none.c"
#include "plat_fm1.c"
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#include "app.c"
#include APP_LATE                         /* (app.h: the screen, app_ui.c, after the platform, as unity.c with QUARTZ's patch) */
#undef __attribute__

/* ---------------------------------------------------------------- the second core (cpu2.c, not built) --- */
/* one core, never given up (plat.h; audio.c's load reads plat_cpu2_on): audio_init's plat_cpu2_set(1) gets -1 and
 * the engine renders every voice itself */
int plat_cpu2_set(int on) { return on ? -1 : 0; }
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_qz_log_line(const char *t)
{
    uint8_t f[48];
    uint32_t n = 0;
    f[n++] = 0xF0;
    f[n++] = 0x7D;
    f[n++] = 0x4C;
    f[n++] = 0x47;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    fm1core_pl_sx_send(f, n);
}
static int fm1core_qz_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_qz_log_line("no crash recorded");
    fm1core_qz_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base: no lease) */
static int fm1core_qz_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: the screen reader, the log readout, the remote control (a frame one of them took is
 * done); then ota_service (the M-UPGRADE handshake; other SysEx is dropped; the update key's ota_req: not taken) */
static void fm1core_qz_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &p, &n) &&
        (ann_frame(p, n, fm1_ms) || fm1core_qz_log_frame(p, n) || fm1core_fb_remote_frame(p, n)))
        fm1sx_done(&fm1core_pl_sxi);
    ota_service();
}
#define FM1CORE_PL_SX_ACCEPT fm1core_qz_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_qz_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
/* the live knob labels and readouts (common/rd_cols.h): the footer as app/ui.c draw_foot draws it, from the view the
 * frame just drew (ui.shown.foot: its page, values, hidden knobs, insert type, model): the label PAGE[pid].label[k]
 * (INSERT's first three: INS_LABEL), the value knob_text; a knob with no label or hidden: "". A LANES / TONNETZ /
 * CHORDS cell held (K1 / K2 set the cell, not the footer's knobs): nothing given */
#include "rd_cols.h"
static void fm1core_quartz_rd(void)
{
    const q_view *v = &ui.shown;
    const q_page *pg = &PAGE[v->foot.pid];
    int k;
    rdc_begin();
    if (ui.cell)
        return;
    for (k = 0; k < 4; k++) {
        const char *lb = v->foot.pid == P_INSERT && k < 3 ? INS_LABEL[v->foot.itype][k] : pg->label[k];
        char t[24];
        if (!lb || v->foot.hide >> k & 1u) {
            rdc_col((uint32_t)k, "", "");
            continue;
        }
        t[0] = 0;
        knob_text(v->foot.pid, k, v->foot.v[k], v->foot.model, t);
        rdc_col((uint32_t)k, lb, t);
    }
}
#define FM1CORE_PL_UI_FRAME() (app_frame(), fm1core_quartz_rd())
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
/* the 1 kHz tick: the keys' readings (QUARTZ has no APP_TICK: its clock runs in app_render) */
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
#ifdef APP_KEY_SENSE
/* the keys' velocity and pressure (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand
 * the app, through lib/touch.c's own host model (app_host_key: a key landing at a velocity, pressed a percentage
 * more; app_host_tick: the samples while plat_sense is on), as fm1-base's host simulator and browser emulator feed
 * them (base/web/emu/emu_web.c web_key) */
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
    app_init();                           /* the engine, then the saved settings (or defaults) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (GLOBAL's BLUETOOTH; no radio in this build: nothing) */
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
static void fm1core_pl_loop(void) { ble_poll(); }
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        app_say("UPDATE MODE IN", d);
    } else {
        app_say("UPDATE MODE", "CANCELLED");
    }
}
/* nothing pending: only SAVE writes (the project), as on the device */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "domain %d track %d octave %d dirty %d", ui.domain, ui.track, ui.octave, ui.dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
