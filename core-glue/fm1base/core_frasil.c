/* SPDX-License-Identifier: GPL-3.0-only */
/* The FRASIL core: FRASIL by Styves Goupil (https://github.com/styvesgoupil/frasil, the pinned, unmodified submodule
 * cores/frasil, MPL-2.0, with its own submodules base = fm1-base and base/bsp = felucca-bsp, MPL-2.0) built with the
 * fm1-base glue (core-glue/fm1base/README.md), from Mutable Dreams' glue (core_mutabledreams.c: the same fm1-base
 * generation, with felucca-bsp's chip/ota.c and backup.c). The include list follows fm1-base's device unity build
 * (base/device/unity.c) with the hardware left out: plat.h, the app's header, touch.h (APP_TOUCH), gfx.c, fm1-base's
 * defaults (fm1b_config.h), the store (storage.c on the NOR image, the app's APP_STORE_MAP: the settings, the scenes,
 * the bass sequences), the updater's framing (felucca-bsp's chip/ota.c), the screen reader (announce.c), the panel
 * calibration (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false), the device's plat.h
 * implementation (plat_fm1.c), backup and restore (backup.c, fm1.toml backup = true: scenes.frasil), the keys'
 * velocity and pressure (lib/touch.c, its host part: APP_TOUCH) and the app (app.c with params.c, harmony.c, ui.c).
 * The DSP (app/dsp/*.c, fm1.toml "o2") is its own units (frasil_dsp_*.c).
 *
 * The FRASIL name and logo (app/assets/frasil-logo.png, compiled in as frasil_logo.h by the firmware's own generate
 * step) are built unchanged, as the author ships them (LICENSING.md); the core is HOSTED_DESPITE_GATE
 * (core_frasil.cmake).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release's, from core_frasil.cmake.
 * Left out, as in every core: the splash (app_splash, APP_SPLASH: plat_glue.h starts the UI at power-on), the
 * render's own time (APP_OVERLOAD's shedding and the budget: simulated time), the log's load lines.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47), the remote control and the app's own
 * (app_sysex, 7D 46 50: Perform, the scenes manager's page), then, every pass, ota_service (the M-UPGRADE handshake)
 * and usb_cmd: backup (0x42, streamed by bu_step) and restore (0x41: ota.c's read requests to the host, then the
 * app's question, SEL yes / FX no) of the app's objects (backup.c). Here the same, in that order, from the
 * submodule's files; the restore waits as on the device: the service runs on a coroutine (FM1CORE_PL_SX_CORO) that
 * yields where the device feeds its watchdog. The log readout says what an emulated unit has: no crash recorded, then
 * "end". The remote control (7D 4C 43) as main_fm1.c remote_frame (fm1base_remote.h). The update key
 * F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): an installer gets the identity, no read requests (it times out), and nothing is
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
#error "APP_VERSION: build.py --release's (core_frasil.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_frasil.cmake)"
#endif
#define FM1B_BACKUP 1                     /* fm1.toml backup = true (build.py opt_flags) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (a restore waits for the host and for SEL: the service's coroutine) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* felucca-bsp's mid level's phase (fm1_input.h fm1__mid_ph: the scan frame, of FM1_LED_MID_N = 8, whose frame lights
 * fm1_led_mid), which app.c's app_tick reads for OCT's soft blink: counted here, a frame every FM1_NCOL device ms (the
 * scan's column a ms), in the tick hook */
static uint8_t fm1__mid_ph, fm1core_fr_col;
static void fm1core_fr_scan_frame(void)
{
    if (++fm1core_fr_col >= FM1_NCOL) {
        fm1core_fr_col = 0;
        fm1__mid_ph = (uint8_t)(fm1__mid_ph + 1u >= 8u ? 0u : fm1__mid_ph + 1u);
    }
}

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool / .ram_text sections) */
#include "plat.h"
#include "app.h"
#ifdef APP_TOUCH
#include "touch.h"
#endif
#include "gfx.c"
#include "fm1b_config.h"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA; backup.c keeps its
 * journal record there) on the NOR image. The update's own steps (ota_session: its screen, its commit) are never
 * reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/frasil/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_fr_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_fr_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_fr_ota_in(off, n))
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
#include "plat_fm1.c"
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
    draw_text_box(0, 92, 240, &FONT_S, a, C_WHITE, 1);
    if (b)
        draw_text_box(0, 124, 240, &FONT_S, b, C_HI, 1);
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
    draw_text_box(0, 92, 240, &FONT_S, "RESTORE", C_WHITE, 1);
    draw_text_box(0, 124, 240, &FONT_S, b, C_HI, 1);
}
/* the question's two buttons (app.h APP_BACKUP_YES, APP_BACKUP_NO: SEL and HOME unless the app says), the only lights
 * on while it waits; its screen the app's if it draws one (APP_BACKUP_ASK: app_backup_ask_screen), else these lines */
#ifndef APP_BACKUP_YES
#define APP_BACKUP_YES B_SEL
#define APP_BACKUP_YES_NAME "SEL"
#endif
#ifndef APP_BACKUP_NO
#define APP_BACKUP_NO B_HOME
#define APP_BACKUP_NO_NAME "HOME"
#endif
static int rs_ask(void)                             /* yes, no; playing or 60 s: no. The hand's buttons only */
{
    uint32_t t0 = fm1_ms, was = plat_buttons() & ~mirror_in_buttons() & ~remote.btn;
#ifdef APP_BACKUP_ASK
    app_backup_ask_screen();
#else
    rs_screen("RESTORE FROM USB?", APP_BACKUP_YES_NAME " yes   " APP_BACKUP_NO_NAME " no");
    draw_text_box(0, 156, 240, &FONT_S, APP_BACKUP_WHAT, C_GRAY, 1);
#endif
    plat_lights(1u << APP_BACKUP_YES | 1u << APP_BACKUP_NO, 0, 0, 0, 0);
    ann_say("Restore from USB? It " APP_BACKUP_WHAT ". " APP_BACKUP_YES_NAME ": yes. " APP_BACKUP_NO_NAME ": no.", 0,
            ANN_NOW | ANN_KEEP);
    while (fm1_ms - t0 < 60000u) {
        uint32_t b = plat_buttons() & ~mirror_in_buttons() & ~remote.btn, down = b & ~was;
        fm1_wdt_feed();
        was = b;
        if (down >> APP_BACKUP_YES & 1u)
            return 1;
        if ((down >> APP_BACKUP_NO & 1u) || rs_playing())
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
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_fr_log_line(const char *t)
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
static int fm1core_fr_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_fr_log_line("no crash recorded");
    fm1core_fr_log_line("end");
    return 1;
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: backup and restore (the mirror
 * and APP_USB_CMD are not built) */
static void fm1core_fr_usb_cmd(uint32_t d)
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
    }
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (FRASIL's fm1-base: no lease) */
static int fm1core_fr_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u) || fm1core_pl_sx_is(f, n, 0x46u, 0x50u);
}
/* main_fm1.c fm1_main, each pass: a frame the screen reader, the log readout or the app took is done; then
 * ota_service (the M-UPGRADE handshake) and usb_cmd, then the backup streaming */
static void fm1core_fr_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_fr_log_frame(p, n) || fm1core_fb_remote_frame(p, n) ||
                                    app_sysex(p, n)))
        ota_frame_done();
    fm1core_fr_usb_cmd(ota_service());
    bu_step();                            /* a backup streaming to the host */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_fr_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_fr_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready || bu.on)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights' mid level (the pages' steady half-lit buttons, OCT's blink) */
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
        fm1core_fr_scan_frame();                                                                               \
        in_tick = 1;                                                                                           \
        app_tick();                       /* 1 kHz, in the timer interrupt on the device */                    \
        in_tick = 0;                                                                                           \
    } while (0)
#elif defined APP_KEY_SENSE
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
#endif
#ifdef APP_KEY_SENSE
/* the keys' velocity and pressure (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand
 * the app, through lib/touch.c's own host model (as core_amb.c) */
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
    app_init();                           /* the engine, then the saved state (or the first scene) */
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
static float fm1core_fr_spk_g = 1.0f;     /* main_fm1.c spk_g: the speaker's gain, gliding (plat_speaker) */
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (no USB audio) */
{
    app_render(lr, n, APP_GAIN(master_pos));
    if (spk_off || fm1core_fr_spk_g < 1.0f) {   /* SPEAKER off: the output faded out in some 6 ms, and back in */
        const float to = spk_off ? 0.0f : 1.0f;
        uint32_t i;
        for (i = 0; i < n; i++) {
            float d = to - fm1core_fr_spk_g;
            fm1core_fr_spk_g += d > 1.0f / 256.0f ? 1.0f / 256.0f : d < -1.0f / 256.0f ? -1.0f / 256.0f : d;
            lr[2u * i] = (int32_t)((float)lr[2u * i] * fm1core_fr_spk_g);
            lr[2u * i + 1u] = (int32_t)((float)lr[2u * i + 1u] * fm1core_fr_spk_g);
        }
    }
}
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
/* the lazy save now: the settings prefs_save was asked for (st.prefs_due: app_frame writes them once nothing sounds,
 * prefs_quiet, a write stopping the render a moment); while something sounds or a backup streams: deferred. The
 * scenes and the bass sequences are written when stored (SAVE): nothing else waits */
static int fm1core_pl_sync(void)
{
    if (!st.prefs_due)
        return 1;
    if (st.store_hold || !prefs_quiet())
        return 0;
    prefs_save();
    return !st.prefs_due;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u scene %u logo %u dirty %u prefs_due %u", (unsigned)st.page, (unsigned)st.scene,
             (unsigned)st.logo, (unsigned)st.dirty, (unsigned)st.prefs_due);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
