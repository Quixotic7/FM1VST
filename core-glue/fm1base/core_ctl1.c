/* SPDX-License-Identifier: GPL-3.0-only */
/* The CTL-1 core: CTL-1 1.0.1 for the FM-1 by Charles Vestal (https://github.com/charlesvestal/fm1-ctl, the pinned,
 * unmodified submodule cores/fm1-ctl, MPL-2.0, with its own submodules base = fm1-base and base/bsp = felucca-bsp,
 * MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from AMB-1's (core_amb.c: its fm1-base,
 * 698e2cb, is AMB-1's 9bca715 with one installer-page fix). The include list follows fm1-base's device unity build
 * (base/device/unity.c) with the hardware left out: plat.h, the app's header, lib/touch.h, gfx.c, fm1-base's defaults
 * (fm1b_config.h), the store (storage.c on the NOR image, the app's APP_STORE_MAP: the settings, the panel
 * calibration, the Bluetooth stack's address), the screen reader (announce.c), the panel calibration (panel.c), the
 * build with no radio (radio_none.c: CTL-1's USB build, build.py --no-ble, ctl-usb.fwsc), the device's plat.h
 * implementation (plat_fm1.c), the key sensing (lib/touch.c: APP_TOUCH, velocity and aftertouch from the keys), the
 * Bluetooth MIDI devices' list (lib/btdev.c: APP_BTDEV, over ble_none.c: never any) and the app (app.c: the settings,
 * the engine, the UI). The GM synth (app/dsp/gm.c and Sonivox EAS, app/dsp/eas/) is fm1.toml's "o2" units, compiled
 * apart (core_ctl1.cmake).
 *
 * CTL-1 is a MIDI controller: the keys, the knobs, the arpeggiator and the Euclidean sequencer send MIDI out
 * (plat_midi_send to the USB-MIDI out ring, fm1base_hal.h), MIDI in is passed along, and the onboard GM synth plays
 * it all (Sound on, the default), so the core has sound of its own.
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 1.0.1's: "1.0.1" and FM-1_6010001
 * (fm1.toml id_prefix FM-1_6 + 01 00 01), from core_ctl1.cmake.
 * Left out, as in every core: the splash (APP_SPLASH: plat_glue.h starts the UI at power-on). APP_QUIET (the radio's
 * first second) never applies with no radio; APP_OVERLOAD (the synth sheds voices when the render runs late) has no
 * audio timing on the host and is not called.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52: the FM-1 Screen Reader, which CTL-1's README names), the log readout
 * (7D 4C 47) and the remote control (7D 4C 43, this fm1-base's: no lease); CTL-1 has no app_sysex (no APP_SYSEX) and
 * no WiFi; then ota_service (the M-UPGRADE handshake: felucca-bsp's chip/ota.c through common/sysex_ota.h with the
 * package identity) every pass. Here the same, in that order. The log readout says what an emulated unit has: no crash
 * recorded, then "end". The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h):
 * the installer (web/ctl_installer.html) gets the identity, no read requests (it times out), and nothing is
 * written. */
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
#error "APP_VERSION: build.py --release's (core_ctl1.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_ctl1.cmake)"
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
#define FM1SX_OTA_C "../../cores/fm1-ctl/base/bsp/chip/ota.c"
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
#include "btdev.c"                        /* (APP_BTDEV: the Setup page's devices; no Bluetooth here, none found) */
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_ctl_log_line(const char *t)
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
static int fm1core_ctl_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_ctl_log_line("no crash recorded");
    fm1core_ctl_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base: no lease) */
static int fm1core_ctl_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: the screen reader, the log readout, the remote control (a frame one of them took is
 * done); then ota_service (the M-UPGRADE handshake; other SysEx is dropped; the update key's ota_req: not taken) */
static void fm1core_ctl_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &p, &n) &&
        (ann_frame(p, n, fm1_ms) || fm1core_ctl_log_frame(p, n) || fm1core_fb_remote_frame(p, n)))
        fm1sx_done(&fm1core_pl_sxi);
    ota_service();
}
#define FM1CORE_PL_SX_ACCEPT fm1core_ctl_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_ctl_sx_service()

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
/* the 1 kHz tick (APP_TICK: the clock, the arpeggiator, the sequencer; app.c app_tick sets in_tick itself), in the
 * timer interrupt on the device */
#define FM1CORE_PL_TICK_HOOK()                                                                                 \
    do {                                                                                                       \
        fm1core_fb_sense_tick();                                                                               \
        app_tick();                                                                                            \
    } while (0)
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
    app_init();                           /* the saved settings (or defaults), then the GM synth */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
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
/* the lazy save now: ui.c's autosave without its waits (a few seconds after a change, nothing held, not playing);
 * while the transport plays, deferred */
static int fm1core_pl_sync(void)
{
    if (!ui.dirty)
        return 1;
    if (ui.playing)
        return 0;
    if (project_save() == 0)
        ui.dirty = 0;
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u chan %u program %u bank %u playing %u arp %u sound %u dirty %u", (unsigned)ui.view,
             (unsigned)proj.chan + 1u, (unsigned)proj.program, (unsigned)proj.bank, (unsigned)ui.playing,
             (unsigned)proj.arp_on, (unsigned)proj.sound, (unsigned)ui.dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
