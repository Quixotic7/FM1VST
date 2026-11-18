/* SPDX-License-Identifier: GPL-3.0-only */
/* The LOAF BREAKER core: LOAF BREAKER 0.9.68 by FLEXOR, from CHOPPAH by Laurens Profittlich
 * (https://github.com/FLEXOR-R/loaf-breaker, the pinned, unmodified submodule cores/loaf-breaker, MPL-2.0, with its own
 * submodules base = fm1-base @ 317cb86 and base/bsp = felucca-bsp @ 5b0879b, MPL-2.0) built with the fm1-base glue
 * (core-glue/fm1base/README.md), from Mighty Arp's (core_mightyarp.c: fm1-base b0976f2, whose device/ is 317cb86's
 * file for file). GATED, its .rfm1 hosted only (core_loafbreaker.cmake: the breaks built in). The include list
 * follows fm1-base's device unity build (base/device/unity.c) with the hardware left out: plat.h, the app's header,
 * gfx.c, fm1-base's defaults (fm1b_config.h), the platform's screens in the app's faces (sys_text.c, APP_SYS_TEXT),
 * the store (storage.c on the NOR image, the app's APP_STORE_MAP: settings, patterns, kicks, the KICK LAB), the screen
 * reader (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false),
 * the device's plat.h implementation (plat_fm1.c, with APP_DIM_LEVELS' plat_dim_ns) and the app (app.c: the break
 * engine's glue, the bank, the sequencer, then ui/*.c). The engine (fm1.toml "o2": app/dsp/*.c) is its own units.
 *
 * Its numbers (core_loafbreaker.cmake): LOAF_VERSION "0.9.68" (the splash's, app/loaf_version.h upstream), APP_VERSION
 * "2.9.68" and FELUCCA_ID FM-1_2623020968 (build.sh --release 2.9.68, scripts/loaf_version.sh's PKG).
 * Left out: the splash (APP_SPLASH: plat_glue.h starts the UI at power-on); APP_OVERLOAD (the FX chain sheds slots when
 * the render runs late) has no audio timing on the host and is not called; the screen mirror (fm1.toml mirror:
 * FM1B_MIRROR 0, the host shows the screen itself), USB audio in (usb_audio_in: the FM-1 as an audio interface),
 * midi_stamp (MIDI in stamped where it lands: here it lands in the 1 kHz tick's order) and the update loader's own
 * screen (app/loader, felucca-bsp's loader: device only). Bluetooth is not built (radio_none.c, as in every fm1base
 * core). A first start asks for the MODE (getenv LOAF_ASK: no environment here, common/env.h).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47), the remote control (7D 4C 43, this
 * fm1-base's: no lease) and the app's own (app_sysex, 7D 43 48: CHOPPAH's editor, the Breaks and Import pages' uploads
 * into the data area); then ota_service (the M-UPGRADE handshake: felucca-bsp's chip/ota.c through
 * common/sysex_ota.h with the package identity) every pass. Here the same, in that order. The log readout says what an
 * emulated unit has: no crash recorded, then "end". The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are
 * not taken (plat_sx.h).
 *
 * Saving: SAVE, and AUTO SAVE (a minute after a change, once stopped) inside app_frame, as on the device; flash_sync
 * has nothing more to force. */
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
#error "APP_VERSION: build.py --release's (core_loafbreaker.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_loafbreaker.cmake)"
#endif
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_FB_MIRROR_DIM 1           /* (this fm1-base's mirror_leds: the glow's LEDs too; mirror_mid) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"
/* felucca-bsp's fm1_input.h fm1_led_dim_ns (APP_DIM_LEVELS: plat_dim_ns, SYSTEM > LIGHTS' GLOW at any strength): the
 * panel has the two glows only, so a pulse nearer DIM LO's 1600 ns than DIM HI's 3200 ns shows as DIM LO (as
 * core_mutabledreams.c) */
static void fm1_led_dim_ns(uint32_t ns) { fm1core_fb_dim_lo = (uint8_t)(ns < 2400u); }

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
#define FM1SX_OTA_C "../../cores/loaf-breaker/base/bsp/chip/ota.c"
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
/* plat_fm1.c's SysEx stream (plat_sysex_begin / put / end, plat_sysex_room; app_sysex's replies) writes USB-MIDI packets
 * into usb.c's SysEx ring: here the managers' out ring (plat_sx.h fm1core_pl_sxo, the same CIN 4..7 packets), read by
 * midi_out_take; the stream holds it (sx_busy) from begin to end (as core_sy1.c) */
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
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the second core (cpu2.c, not built) --- */
/* one core (fm1.toml has no cpu2): plat.h's second-core calls answered as a unit without one */
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
static void fm1core_lb_log_line(const char *t)
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
static int fm1core_lb_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_lb_log_line("no crash recorded");
    fm1core_lb_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base: no lease) */
static int fm1core_lb_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u) || fm1core_pl_sx_is(f, n, 0x43u, 0x48u);   /* (the app's: 7D 43 48) */
}
/* main_fm1.c fm1_main, each pass: the screen reader, the log readout, the remote control, the app (a frame one of them
 * took is done); then ota_service (the M-UPGRADE handshake; other SysEx is dropped; the update key's ota_req: not taken) */
static void fm1core_lb_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &p, &n) &&
        (ann_frame(p, n, fm1_ms) || fm1core_lb_log_frame(p, n) || fm1core_fb_remote_frame(p, n) || app_sysex(p, n)))
        fm1sx_done(&fm1core_pl_sxi);
    ota_service();
}
#define FM1CORE_PL_SX_ACCEPT fm1core_lb_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_lb_sx_service()

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
/* the 1 kHz tick: the keys' readings, then app_tick (APP_TICK: the MIDI clock out on time), in the timer interrupt on
 * the device (in_tick: plat_fm1.c's MIDI out from it) */
#define FM1CORE_PL_TICK_HOOK()                                                                                 \
    do {                                                                                                       \
        fm1core_fb_sense_tick();                                                                               \
        in_tick = 1;                                                                                           \
        app_tick();                                                                                            \
        in_tick = 0;                                                                                           \
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
static void fm1core_pl_loop(void)
{
    ble_poll();
    app_loop();                           /* APP_LOOP: MIDI in (the clock) */
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
/* nothing to force: SAVE and AUTO SAVE write from app_frame, as on the device */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u fx %u kpage %u dirty %u", (unsigned)ui.page, (unsigned)ui.fx_focus, (unsigned)ui.kpage,
             (unsigned)set_dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
