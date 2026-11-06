/* SPDX-License-Identifier: GPL-3.0-only */
/* The ECHOFIELD core: ECHOFIELD FM-1 by Philip Bos (https://github.com/Philip-Bos/echofield-fm1, the pinned, unmodified
 * submodule cores/echofield-fm1: MIT, its factory sounds CC0-1.0, with its own submodules base = fm1-base and
 * base/bsp = felucca-bsp, MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from FRASIL's glue
 * (core_frasil.c: felucca-bsp's chip/ota.c, backup.c, the service's coroutine, lib/touch.c). The include list follows
 * fm1-base's device unity build (base/device/unity.c) with the hardware left out: plat.h, the app's header, touch.h
 * (APP_TOUCH), gfx.c, fm1-base's defaults (fm1b_config.h), the platform's screen faces (sys_text.c), the store
 * (storage.c on the NOR image, the app's APP_STORE_MAP: the settings, the bank's directory, the presets, the session),
 * the updater's framing (felucca-bsp's chip/ota.c), the screen reader (announce.c), the panel calibration (panel.c),
 * the build with no radio (radio_none.c, fm1.toml ble = false), the device's plat.h implementation (plat_fm1.c),
 * backup and restore (backup.c, fm1.toml backup = true: settings, presets, session), the keys' velocity (lib/touch.c,
 * its host part: APP_TOUCH) and the app (app.c with params.c, presets.c, bank.c, proto.c). The engine (app/dsp/engine.c,
 * fm1.toml "o2") is its own unit (echofield_dsp_engine.c).
 *
 * app.c is compiled as fm1-base's browser emulator compiles it (build.py --web: PLAT_HOST, emcc): an empty bank gets
 * the 14 factory sounds at power-on (emu_factory: "an emulated FM-1 starts as a real one does after the flasher"; the
 * sounds are app/build/gen/factory_data.h, from companion/factory, CC0), and settle(), which on the device spins on
 * plat_ms while the audio interrupt fades the output around a flash write, returns at once ("the host has no time":
 * simulated time does not move inside a pass). A native build defines __EMSCRIPTEN__ around app.c for the same path.
 * Nothing else in app.c depends on either (its host simulator's hooks, app_host_cmd / app_expect, are compiled, unused).
 *
 * The sample bank (bank.c: 92 sectors of 4 KiB in the data windows, 0x93000.., 0xC1000.., 0xE7000..) is raw flash on
 * the NOR image (plat_data_erase / plat_data_write, read in place), so it is kept with the plugin's state, the
 * factory sounds included; the companion's UPLOAD writes there as on the device.
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release's, from core_echofield.cmake
 * (CHANGELOG.md's version: "0.2.9", FM-1_3246000209).
 * Left out, as in every core: the splash (APP_SPLASH: the logo and the boot screen; plat_glue.h starts the UI at
 * power-on), the render's own time (APP_OVERLOAD's shedding: simulated time, the render never overloads), the second
 * core (fm1.toml cpu2: FM1B_CPU2 0, the engine renders every grain itself, as with cpu2 given up; plat_cpu2_on and
 * plat_cpu2_gave_up say 0), the screen mirror (fm1.toml mirror: FM1B_MIRROR 0, the companion's Mirror gets no
 * answer: the host shows the screen itself), USB audio (fm1.toml usb_audio_in: the FM-1's output recorded by the
 * computer over USB audio, the companion's takes; the core's output is the host's audio).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43, this
 * fm1-base's: no lease); there is no app_sysex. Then, every pass, ota_service (the M-UPGRADE handshake) and usb_cmd:
 * backup (0x42, streamed by bu_step) and restore (0x41: ota.c's read requests to the host, then RESTORE FROM USB? SEL /
 * HOME) of the app's objects (backup.c), and the companion app's commands (APP_USB_CMD: proto.c's 0x70..0x7D in the
 * update's framing: HELLO, DIR, UPLOAD with its read requests, ERASE, RENAME, PARAMS, SET, PRESETS, PRESET OP, READ,
 * WIPE, PRESET GET / PUT, STATS; and its 0x7F notices out). Here the same, in that order, from the submodule's files;
 * a restore and an upload wait as on the device: the service runs on a coroutine (FM1CORE_PL_SX_CORO) that yields where
 * the device feeds its watchdog (ota.c's reads). The log readout says what an emulated unit has: no crash recorded,
 * then "end". The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): an installer
 * gets the identity, no read requests (it times out), and nothing is written. */
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
#error "APP_VERSION: build.py --release's (core_echofield.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_echofield.cmake)"
#endif
#define FM1B_BACKUP 1                     /* fm1.toml backup = true (build.py opt_flags) */
#define FM1B_MIRROR 0                     /* (fm1.toml mirror = true: not built, above) */
#define FM1B_CPU2 0                       /* (fm1.toml cpu2 = true: not built, above) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (a restore and an upload wait for the host: the service's coroutine) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool / .ram_text sections) */
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
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA; backup.c keeps its
 * journal record there) on the NOR image. The update's own steps (ota_session: its screen, its commit) are never
 * reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/echofield-fm1/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_ef_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_ef_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_ef_ota_in(off, n))
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

#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static uint8_t backup_streaming(void);
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), a backup
                                                          * streaming: not sent (unity.c's) */
{
    if (backup_streaming())
        return -1;
    return fm1core_pl_sx_send(p, n);
}
#include "panel.c"
#include "radio_none.c"
static volatile uint8_t midi_usb_lost;   /* (usb.c's: the host's link never drops here; plat_usb_lost says 0) */
#include "plat_fm1.c"
/* the second core (cpu2.c, not built): one core, never given up (plat.h; app.c's STATS and SYSTEM page read these) */
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
#include "backup.c"
/* unity.c's hooks for backup.c (fm1-base's "backup and restore" section, on the core's HAL: the same statements) */
static uint8_t backup_streaming(void) { return bu.on; }
static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n) { return ota_read(addr, dst, n); }
static int rs_note(uint32_t addr)                   /* (past the question: no session deadline, 3 tries) */
{
    uint8_t b[8];
    ota_deadline = 0;
    return ota_read(addr, b, 8);
}
static int rs_playing(void) { return app_backup_playing() != 0; }
static void rs_screen(const char *a, const char *b)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, a, C_WHITE, 1);
    if (b)
        sys_box(0, 124, 240, SYS_S, b, C_HI, 1);
}
static void rs_begin(void)                          /* a backup is coming: patient now (5 s a read, 2 min in all) */
{
    app_panic();
    ota_try_ms = 5000u;
    ota_deadline = ota_now_ms() + 120000u;
}
static void rs_show(uint32_t pct)
{
    char b[8];
    uint32_t i = 0;
    if (pct >= 100u)
        b[i++] = '1';
    if (pct >= 10u)
        b[i++] = (char)('0' + pct / 10u % 10u);
    b[i++] = (char)('0' + pct % 10u);
    b[i++] = '%';
    b[i] = 0;
    if (!pct)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, "RESTORE", C_WHITE, 1);
    sys_box(0, 124, 240, SYS_S, b, C_HI, 1);
}
static int rs_ask(void)                             /* SEL yes, HOME no; playing or 60 s: no. The hand's buttons only */
{
    uint32_t t0 = fm1_ms, was = plat_buttons() & ~mirror_in_buttons() & ~remote.btn;
    rs_screen("RESTORE FROM USB?", "SEL yes   HOME no");
    sys_box(0, 156, 240, SYS_S, APP_BACKUP_WHAT, C_GRAY, 1);
    ann_say("Restore from USB? It " APP_BACKUP_WHAT ". SEL: yes. HOME: no.", 0, ANN_NOW | ANN_KEEP);
    while (fm1_ms - t0 < 60000u) {
        uint32_t b = plat_buttons() & ~mirror_in_buttons() & ~remote.btn, down = b & ~was;
        fm1_wdt_feed();
        was = b;
        if (down >> B_SEL & 1u)
            return 1;
        if ((down >> B_HOME & 1u) || rs_playing())
            return 0;
    }
    return 0;
}
static void rs_reload(void)
{
    app_panic();
    app_backup_restored();
}
static int rs_rec_erase(void) { return ota_erase(RS_REC); }
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n) { return ota_prog(RS_REC + off, src, n); }
static int rs_rec_read(uint32_t off, void *dst, uint32_t n) { return ota_fread(RS_REC + off, dst, n); }
static int bu_room(void)                            /* the SysEx ring empty: a frame goes in whole (a host is there) */
{
    return !fm1sx_out_queued(&fm1core_pl_sxo);
}
static int bu_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }
static void bu_flush(void) { app_backup_flush(); }
static void bu_hold(int on) { app_backup_hold(on); }
#ifdef APP_TOUCH
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#endif
/* app.c as the browser emulator compiles it (above): PLAT_HOST, and __EMSCRIPTEN__ (emcc's own; a native build's here) */
#define PLAT_HOST 1
#ifndef __EMSCRIPTEN__
#define __EMSCRIPTEN__ 1
#define FM1CORE_EF_EMU 1
#endif
#include "app.c"
#ifdef FM1CORE_EF_EMU
#undef __EMSCRIPTEN__
#endif
#undef PLAT_HOST
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_ef_log_line(const char *t)
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
static int fm1core_ef_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_ef_log_line("no crash recorded");
    fm1core_ef_log_line("end");
    return 1;
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: backup and restore, then the
 * app's (APP_USB_CMD: the companion's proto.c); the mirror is not built */
static void fm1core_ef_usb_cmd(uint32_t d)
{
    if (d < 7u || ota_dec[2] == 0x11u)
        return;
    if (d == 11u && ota_dec[2] == BU_REQ && bu_rd32(ota_dec + 6) == BU_MAGIC && flash_ok) {
        bu_start(app_backup_playing());   /* a backup to the host: streamed by bu_step, playing on */
        return;
    }
    if (d == 15u && ota_dec[2] == RS_REQ && flash_ok) {   /* a restore from the host */
        uint32_t len = ota_rd32(ota_dec + 6), crc = ota_rd32(ota_dec + 10);
        int begun = 0, rc;
        if (bu.on)
            bu_stop();                    /* (a backup streaming: given up) */
        ota_try_ms = 300u;                /* its head first: a host that does not answer costs a second */
        ota_deadline = ota_now_ms() + 3000u;
        rc = restore_rx(len, crc, &begun);
        ota_try_ms = 5000u;
        ota_deadline = 0;
        if (begun)
            app_ui_init();                /* the screen again (restored: everything reloaded) */
        app_say(rc == RS_E_PLAYING ? "STOP FIRST" : "RESTORE FROM USB",
                !rc ? "RESTORED" : rc == RS_E_PLAYING ? "then restore" : rc == RS_E_NO ? "NOTHING CHANGED" : "NOT RESTORED");
        return;
    }
    app_usb_cmd(ota_dec, d);
    ota_try_ms = 5000u;                   /* (the app's patience, plat_host_patience, back to the default) */
    ota_deadline = 0;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_ef_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: a frame the screen reader, the log readout or the remote control took is done; then
 * ota_service (the M-UPGRADE handshake) and usb_cmd, then the backup streaming */
static void fm1core_ef_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_ef_log_frame(p, n) || fm1core_fb_remote_frame(p, n)))
        ota_frame_done();
    fm1core_ef_usb_cmd(ota_service());
    bu_step();                            /* a backup streaming to the host */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_ef_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_ef_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready || bu.on)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid's steady mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
#ifdef APP_KEY_SENSE
    if (fm1_an.con)
        app_host_tick();
#endif
}
#ifdef APP_KEY_SENSE
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
/* the keys' velocity (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand the app,
 * through lib/touch.c's own host model (as core_amb.c) */
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

static void fm1core_pl_flash_boot(void)  /* fm1_main: a restore's commit cut short by a power loss finished */
{
    rs_boot();
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the settings, presets, the bank (the factory sounds into an empty one),
                                           * the engine, the session's sample */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (USB audio full off: no USB audio) */
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
/* the lazy save now: app_frame's session save (the parameters, the sample, the locks, 3 s after the last change while
 * nothing sounds) without its waits; while something sounds or a backup holds it: deferred. The settings, the presets
 * and the bank are written when they change: nothing else waits */
static int fm1core_pl_sync(void)
{
    if (!ses_dirty)
        return 1;
    if (backup_hold || app_backup_playing())
        return 0;
    session_save();
    return !ses_dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    uint32_t s, used = 0;
    for (s = 0; s < BANK_SLOTS; s++)
        used += dir.slot[s].used;
    snprintf(buf, n, "page %u slot %d samples %u play %u mode %u dirty %u", (unsigned)ui.page, (int)cur_slot,
             (unsigned)used, (unsigned)ef_ctl()->play, (unsigned)pv[P_MODE], (unsigned)ses_dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
