/* SPDX-License-Identifier: GPL-3.0-only */
/* The MORSE core: MORSE (https://github.com/daviddwlee84/fm1-morse, the pinned, unmodified submodule cores/fm1-morse,
 * GPL-3.0-or-later, common/ MIT, with its submodules fm1-base = fm1-base and fm1-base/bsp = felucca-bsp, MPL-2.0)
 * built as one unit with the fm1-base glue (core-glue/fm1base/README.md). The app is the repository's root (app.h,
 * app.c, fm1.toml; fm1-base in fm1-base/). Its fm1-base is c8e0599, SY1's: this unit follows core_sy1.c without the
 * second core (fm1.toml has no cpu2) and without lib/touch.c (no APP_TOUCH), with app_tick (APP_TICK: the key, the
 * keyer, the players, MIDI in and out; 1 kHz, as main_fm1.c's timer runs it) and the app's own SysEx (app_sysex, the
 * host link 7D 4D 4F: host/morse, the progress page). The include list follows fm1-base's device unity build
 * (base/device/unity.c) with the hardware left out: plat.h, the app's header, gfx.c, fm1-base's defaults
 * (fm1b_config.h), the platform's screens (sys_text.c), the store (storage.c on the NOR image, the app's
 * APP_STORE_MAP), the updater's framing (felucca-bsp's chip/ota.c: the M-UPGRADE identity), the screen reader
 * (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c: fm1.toml ble = true with the
 * own stack is not built; SETTINGS > BLUETOOTH says NOT IN THIS BUILD, as app.c has it for a build without it), the
 * device's plat.h implementation (plat_fm1.c) and the app (app.c: hostlink.c, ledfx.c, morse.c, learn.c, content.c,
 * tone.c, the sidetone and the screens).
 *
 * MORSE's version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.6's: "0.6" and
 * FM-1_41000600 (fm1.toml id_prefix FM-1_41 + 00 06 00); core_morse.cmake derives both from its VERSION.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives each SysEx
 * frame to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47: "no crash recorded", then "end"), the
 * remote control (7D 4C 43) and the app's own (app_sysex: the host link, F0 7D 4D 4F <ver> ..), then runs ota_service
 * (the M-UPGRADE handshake; usb_cmd has nothing in this build). The update key F0 22 24 35 7F F7 and UBOOT's
 * F0 22 24 35 7D F7 are not taken (plat_sx.h). */
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
#error "APP_VERSION: build.py --release's (core_morse.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_morse.cmake)"
#endif
#define FM1B_BACKUP 0                     /* (fm1.toml has no backup) */
#define FM1B_CPU2 0                       /* (fm1.toml has no cpu2) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_FB_MIRROR_DIM 1           /* (this fm1-base's mirror_leds: the glow's LEDs too; mirror_mid) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#ifdef APP_TOUCH
#include "touch.h"
#endif
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed), the update's staging area (OTA_AREA) on the NOR image. The update's own steps
 * (ota_session: its screen, its commit) are never reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/fm1-morse/fm1-base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_morse_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_morse_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_morse_ota_in(off, n))
        return -8;
    return fm1nor_prog_span(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    (void)step;
    (void)code;
}
static void ota_commit(const uint8_t *parm) { (void)parm; }

/* felucca-bsp's usb.c and guard, as plat_fm1.c reads them: the USB link never lost (a host is always there); no
 * guarded PC range to extend */
static volatile uint8_t midi_usb_lost;
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading): not sent */
{
    return fm1core_pl_sx_send(p, n);
}
#include "panel.c"
#include "radio_none.c"
/* plat_fm1.c's SysEx stream (plat_sysex_begin / put / end, plat_sysex_room; app.c's replies through plat_sysex_send)
 * writes USB-MIDI packets into usb.c's SysEx ring: here the managers' out ring (plat_sx.h fm1core_pl_sxo, the same
 * CIN 4..7 packets), read by midi_out_take; the stream holds it (sx_busy) from begin to end */
static uint8_t sx_busy;
#define SXQ FM1SX_OUT_PKTS
#define sx_out_q fm1core_pl_sxo.q
#define so_w fm1core_pl_sxo.w
#define so_r fm1core_pl_sxo.r
#include "plat_fm1.c"
#undef SXQ
#undef sx_out_q
#undef so_w
#undef so_r

#ifdef APP_TOUCH
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#endif
#include "app.c"                          /* (the device's paths: PLAT_HOST off; only app_expect is host-only) */
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7: "no crash
 * recorded", the app's own lines (APP_LOG, none in MORSE), "end" last */
static void fm1core_morse_log_line(const char *t)
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
static int fm1core_morse_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_morse_log_line("no crash recorded");
#ifdef APP_LOG
    app_log(fm1core_morse_log_line);
#endif
    fm1core_morse_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_morse_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u) || fm1core_pl_sx_is(f, n, 0x4Du, 0x4Fu);   /* (MORSE's host link: 7D 4D 4F) */
}
/* main_fm1.c fm1_main, each pass: a frame one service took is done; a frame none took goes to ota_service (usb_cmd:
 * nothing in this build) */
static void fm1core_morse_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n)) {
        if (ann_frame(p, n, fm1_ms) || fm1core_morse_log_frame(p, n) || fm1core_fb_remote_frame(p, n) || app_sysex(p, n))
            ota_frame_done();
        else
            ota_service();
    }
}
#define FM1CORE_PL_SX_ACCEPT fm1core_morse_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_morse_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid's steady mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

/* the 1 kHz tick (APP_TICK: the key, the keyer, the players, MIDI in and out), in the timer interrupt on the device
 * (main_fm1.c: in_tick around it, plat_lock's view) */
#define FM1CORE_PL_TICK_HOOK()                                                                                 \
    do {                                                                                                       \
        in_tick = 1;                                                                                           \
        app_tick();                                                                                            \
        in_tick = 0;                                                                                           \
    } while (0)
#ifdef APP_KEY_SENSE
/* the keys' velocity (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand the app,
 * through lib/touch.c's own host model (as core_echofield.c) */
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

static void fm1core_pl_flash_boot(void)  /* fm1_main: nothing (no backup.c: no restore record) */
{
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the settings, the bank, the two parts' engines, the second core */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (no USB audio) */
{
    app_render(lr, n, APP_GAIN(master_pos));
}
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
/* the lazy save now: app_frame's settings and progress save (3 s after a change: rec_save, the one record) without
 * its wait. Nothing else waits */
static int fm1core_pl_sync(void)
{
    if (dirty_at)
        rec_save();
    return 1;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u wpm %u keyer %u saves %u", (unsigned)ui.page, (unsigned)rec.set.wpm,
             (unsigned)rec.set.keyer, (unsigned)saves);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
