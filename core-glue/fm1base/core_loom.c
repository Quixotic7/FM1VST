/* SPDX-License-Identifier: GPL-3.0-only */
/* The LOOM-8 core: LOOM-8 (https://github.com/charlesvestal/fm1-loom, the pinned, unmodified submodule cores/fm1-loom,
 * GPL-3.0-only, with its submodules base = fm1-base and base/bsp = felucca-bsp, MPL-2.0) built as one unit with the
 * fm1-base glue (core-glue/fm1base/README.md). The include list follows fm1-base's device unity build
 * (base/device/unity.c) with the hardware left out: plat.h, the app's header, gfx.c, fm1-base's defaults
 * (fm1b_config.h), the platform's screens in the app's faces (sys_text.c, APP_SYS_TEXT), the store (storage.c on the
 * NOR image, the app's APP_STORE_MAP), the updater's framing (felucca-bsp's chip/ota.c: the M-UPGRADE identity, and
 * the pulls of a restore), the screen reader (announce.c), the panel calibration (panel.c), the build with no radio
 * (radio_none.c), the device's plat.h implementation (plat_fm1.c), backup and restore (backup.c, fm1.toml backup =
 * true) and the app (app.c: the drawing, the engine, the project, the pages, the input, the user sample bank). The
 * DSP and the sequencer are their own -O2 units (loom_*.c, fm1.toml "o2").
 *
 * app.c is the device's (PLAT_HOST off: plat_fm1.c's paths, the voices' engine order, the cost model); the one file
 * of the app the core does not compile is app/plat_loom.c, the second core's hardware, read from loom_plat.c instead
 * (a clang VFS overlay, core_loom.cmake): the second core's job runs at once, in order, as LOOM-8's host simulator
 * and browser emulator run it (the split render, the same sums).
 *
 * LOOM-8's version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release's for the pinned tag:
 * "0.6-BETA" (the release in capitals) and FM-1_10000600 (fm1.toml id_prefix FM-1_10 + 00 06 00); core_loom.cmake
 * derives both from its VERSION.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives each SysEx
 * frame to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47), the remote control and the app's
 * own (app_sysex: LOOM-8's user sample bank, samples.c, 7D 46 4C: INFO, PING, the bank's SMP_* commands,
 * tools/loom_samples.py and the samples page), then runs ota_service (the M-UPGRADE handshake) and hands the frames
 * in the update's framing it leaves to usb_cmd: backup (0x42, streamed by bu_step) and restore (0x41: ota.c's read
 * requests to the host, then RESTORE FROM USB? on the screen, SEL / HOME) of the projects and the user presets
 * (backup.c; base/web/backup, base/tools/fm1_backup.py). Here the same, in that order, from the submodule's files;
 * the restore waits as on the device: the service runs on a coroutine (FM1CORE_PL_SX_CORO) that yields where the
 * device feeds its watchdog. A restore writes the NOR image as the device does (each object staged in its other
 * copy, the journal record in the update's staging area, the headers last; rs_boot at power-on). The log readout
 * says what an emulated unit has: no crash recorded, then "end". The remote control (7D 4C 43) is not answered. The
 * update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): an installer gets the
 * identity, no read requests (it times out), and nothing is written. */
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
#define APP_VERSION FM1CORE_VERSION       /* (core_loom.cmake gives build.py --release's: the version in capitals) */
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_loom.cmake)"
#endif
#define FM1B_BACKUP 1                     /* fm1.toml backup = true (build.py opt_flags) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (a restore waits for the host and for SEL: the service's coroutine) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA, 5 x 4 KiB at 0xE0000;
 * backup.c keeps its journal record there) on the NOR image. The update's own steps (ota_session: its screen, its
 * commit) are never reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/fm1-loom/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_loom_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_loom_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_loom_ota_in(off, n))
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
#include "app.c"                          /* (its app/plat_loom.c: loom_plat.c, the VFS overlay) */
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last
 * (the device's lines about its supplies' registers, Bluetooth's log and its timer have no counterpart here) */
static void fm1core_loom_log_line(const char *t)
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
static int fm1core_loom_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_loom_log_line("no crash recorded");
    fm1core_loom_log_line("end");
    return 1;
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: backup and restore (the mirror
 * and APP_USB_CMD are not built) */
static void fm1core_loom_usb_cmd(uint32_t d)
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
static int fm1core_loom_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, SB_HDR1, SB_HDR2);
}
static void fm1core_loom_sx_service(void) /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_loom_log_frame(p, n) || app_sysex(p, n)))
        ota_frame_done();
    fm1core_loom_usb_cmd(ota_service());  /* M-UPGRADE handshake; backup and restore (other SysEx is dropped) */
    bu_step();                            /* a backup streaming to the host */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_loom_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_loom_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready || bu.on)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid: the Arrange page's level meters, the beats */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_pl_flash_boot(void)  /* fm1_main: a restore's commit cut short by a power loss finished */
{
    rs_boot();
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the user bank, the project, the presets, the engine, the second core */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (USB audio full: no USB audio) */
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
/* the lazy save now: ui.c's autosave (a change saved when stopped, not recording, nothing asked or listed, into the
 * project's slot) without its waits (3 s after the last touch, half a second of quiet); AUTOSAVE off, or a new
 * project with no slot yet (SAVE TO gives it one): only SAVE writes, the RAM is all there is, as on the device.
 * Playing, recording or asking: deferred */
static int fm1core_pl_sync(void)
{
    if (!ui.dirty || (proj.rsv[2] & AUTOSAVE_OFF) || proj_slot >= NPROJ)
        return 1;
    if (playing() || seq.rec || seq.rec_wait || ui.ask || lst.open || store_held)
        return 0;
    if (project_save(proj_slot) == 0)
        ui.dirty = 0;
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page '%s' track %u scene %u playing %u slot %u dirty %u", ui_page_name(),
             (unsigned)proj.trk_sel + 1u, (unsigned)cur_scene() + 1u, (unsigned)playing(), (unsigned)proj_slot + 1u,
             (unsigned)ui.dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
