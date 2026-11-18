/* SPDX-License-Identifier: GPL-3.0-only */
/* The Tekstuur core: Tekstuur 2.0 by BARNIR (https://github.com/barnierg76/tekstuur-fm1, the pinned, unmodified
 * submodule cores/tekstuur-fm1, MPL-2.0; its app is eigen/: app.h, app.c, fm1.toml, dsp/) on fm1-base (Charles
 * Vestal, MPL-2.0) and felucca-bsp (MPL-2.0). Unlike the other fm1-base apps, Tekstuur's repository carries no fm1-base:
 * its eigen/bouw.sh copies eigen/ into an fm1-base checkout and builds there. Here fm1-base is a submodule of its own,
 * cores/tekstuur-fm1-base, pinned at 68fdaf8 (2026-11-13: the last fm1-base commit before Tekstuur's v2.0 of 2026-11-14;
 * its felucca-bsp 18eadad), the same device code as SY1's and LOOM-8's c8e0599 but for uac_mix.h's MASTER slide.
 *
 * Built with the fm1-base glue (core-glue/fm1base/README.md) after core_sy1.c (the same fm1-base) and
 * core_echofield.c (an app with APP_USB_CMD and its host reads): the include list follows fm1-base's device unity
 * build (base/device/unity.c) with the hardware left out: plat.h, the app's header, touch.h (APP_TOUCH), gfx.c,
 * fm1-base's defaults (fm1b_config.h), the platform's screen faces (sys_text.c), the store (storage.c on the NOR image,
 * the app's APP_STORE_MAP: state, panel, user presets), the updater's framing (felucca-bsp's chip/ota.c), the screen
 * reader (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false),
 * the device's plat.h implementation (plat_fm1.c), the keys' velocity and pressure (lib/touch.c, its host part:
 * APP_TOUCH) and the app (app.c, its device path: PLAT_HOST off; only app_expect is host-only). The engine
 * (dsp/tk_engine.c, fm1.toml "o2") is its own unit.
 *
 * The user's own sources (app.c: a bank at 0x97000..0xDBFFF, the data area read in place, plat_data_erase / write) live
 * on the NOR image, so they are kept with the plugin's state; tools/tekstuur_samples.py's upload (cmd 0x60 in the
 * update's framing: plat_host_read pulls the bank from the host) runs as on the device, on the SysEx service's
 * coroutine (FM1CORE_PL_SX_CORO) that yields where the device feeds its watchdog (ota.c's reads).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 2.0's: "2.0" and FM-1_27020000
 * (fm1.toml id_prefix FM-1_27 + 02 00 00); core_tekstuur.cmake derives both from its VERSION.
 * Left out, as in every core: the splash (APP_SPLASH: the wordmark; plat_glue.h starts the UI at power-on), the
 * render's own time (APP_OVERLOAD's pool shrinking: simulated time, the render never overloads), the screen mirror
 * (fm1.toml mirror = true: FM1B_MIRROR 0, the host shows the screen itself; app_redraw is never asked), USB audio
 * (plat_usb_audio_full: the core's output is the speaker's, MASTER applied, as main_fm1.c's uac_mix leaves it).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43, this
 * fm1-base's: no lease); Tekstuur has no app_sysex. Then, every pass, ota_service (the M-UPGRADE handshake) and usb_cmd
 * (no backup, no resources, no mirror in this build: the app's APP_USB_CMD only). The log readout says what an emulated
 * unit has: no crash recorded, then "end". The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not
 * taken (plat_sx.h). */
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
#error "APP_VERSION: build.py --release's (core_tekstuur.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_tekstuur.cmake)"
#endif
#define FM1B_BACKUP 0                     /* (fm1.toml has no backup) */
#define FM1B_MIRROR 0                     /* (fm1.toml mirror = true: not built, above) */
#define FM1B_CPU2 0                       /* (fm1.toml has no cpu2) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (an upload waits for the host's reads: the service's coroutine) */
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
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA) on the NOR image. The
 * update's own steps (ota_session: its screen, its commit) are never reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/tekstuur-fm1-base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_tk_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_tk_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_tk_ota_in(off, n))
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
#include "plat_fm1.c"
/* the second core (cpu2.c, not built): one core, never given up */
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

#ifdef APP_TOUCH
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#endif
#include "app.c"                          /* (the device's paths: PLAT_HOST off; only app_expect is host-only) */
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_tk_log_line(const char *t)
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
static int fm1core_tk_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_tk_log_line("no crash recorded");
    fm1core_tk_log_line("end");
    return 1;
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: the app's (APP_USB_CMD: the
 * sources' upload, cmd 0x60); no backup, resources or mirror in this build */
static void fm1core_tk_usb_cmd(uint32_t d)
{
    if (d < 7u || ota_dec[2] == 0x11u)
        return;
    app_usb_cmd(ota_dec, d);
    ota_try_ms = 5000u;                   /* (the app's patience, plat_host_patience, back to the default) */
    ota_deadline = 0;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_tk_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: a frame the screen reader, the log readout or the remote control took is done; then
 * ota_service (the M-UPGRADE handshake) and usb_cmd */
static void fm1core_tk_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_tk_log_frame(p, n) || fm1core_fb_remote_frame(p, n)))
        ota_frame_done();
    fm1core_tk_usb_cmd(ota_service());
}
#define FM1CORE_PL_SX_ACCEPT fm1core_tk_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_tk_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid's steady mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

#ifdef APP_KEY_SENSE
static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
    if (fm1_an.con)
        app_host_tick();
}
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
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
    app_init();                           /* the bank of own sources, the engine, the user presets, the last preset */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block: the speaker's output (MASTER) */
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
/* nothing is saved lazily: SAVE writes the user preset and the state at once, the upload its bank */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)
{
    uint32_t i, on = 0;
    for (i = 0; i < TK_NV; i++)
        on += T.v[i].on;
    snprintf(buf, n, "page %u preset %u layer %u voices %u sources %u dirty %u", (unsigned)ui.page,
             (unsigned)ui.preset, (unsigned)ui.layer, (unsigned)on, (unsigned)nsrc, (unsigned)ui.dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
