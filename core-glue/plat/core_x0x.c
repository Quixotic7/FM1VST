/* SPDX-License-Identifier: GPL-3.0-only */
/* The X0X core: X0X (https://github.com/charlesvestal/fm1-x0x, the pinned, unmodified submodule cores/fm1-x0x)
 * built as one unit with the plat glue (core-glue/plat/README.md). The include list follows the device's unity build
 * (firmware/src/x0x.c) with the hardware left out, as the firmware's own host simulator (host/x0x_host.c) does:
 * gfx.c, plat.h, Felucca's storage.c (on the NOR image), the user sample slots (slots.c, read in place from the
 * image at 0xA0000), the screen reader (announce.c, nobody listening), the panel calibration (panel.c), the
 * device's plat.h implementation (plat_fm1.c), the project and the UI. The DSP, the sequencer and the engine are
 * their own units, as in the firmware's builds (x0x_*.c wrappers, -O2, no fused multiply-add).
 *
 * X0X's version is a build-time define (X0X_VERSION, "DEV" by default): the hub's version (core_x0x.cmake).
 *
 * THE MANAGERS (research/managers-2026-10-19/MANAGERS.md; plat_sx.h): X0X's backup page (web/backup/index.html, as
 * tools/x0x_backup.py) finds the unit by the M-UPGRADE identity and backs up / restores the project over the update's
 * framing (backup.c: 0x42 a backup streamed, 0x41 a restore pulled with ota.c's read requests, then SEL on the FM-1);
 * the break-loop page and tools/fm1_sample_upload.py talk to editor.c (7D 46 4C: INFO, PING, the sample slots); a
 * screen reader to announce.c (7D 53 52). The device's main loop (app/main_fm1.c fm1_main) runs, each pass: the
 * reader's frame, ed_service, backup_service(ota_service()); here the same, compiled from the submodule (ota.c,
 * editor.c, backup.c, announce.c) on the glue's transport (common/sysex.h), with main_fm1.c's hooks for backup.c
 * redone below on the core's HAL (main_fm1.c itself is the hardware's). A restore waits for the host's answers and
 * for SEL / HOME as on the device: the service runs on a coroutine and yields where the device feeds its watchdog
 * (FM1CORE_PL_SX_CORO), so the clock and the panel move meanwhile. Its writes go to the NOR image as the device's:
 * each object staged in its other copy, the journal record at 0xE1000 (the update's staging area), the headers last
 * (flash_dirty moves; rs_boot finishes a commit cut short at the next power-on). The update itself never runs: the
 * update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h), so the installer gets the
 * identity and no read requests (it times out) and nothing is written. */
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
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#define X0X_HOST 1                        /* the device's .pool section off (plat.h X0X_POOL), the host's checks in */
#define X0X_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_9010100"         /* the package identity: tools/build.py --release 1.1.0 (FM-1_9 01 01 00) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (a restore waits for the host and for SEL: the service's coroutine) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections, alignment hints) */
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
#include "storage.c"
/* usb.c's SysEx hooks (FELUCCA_OTA 1) on the glue's transport (plat_sx.h): one F0..F7 message out (whole or not at
 * all: the device gives up after 200 ms of a full ring), the frame waiting for the main loop, its release */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "ota.c"
/* x0x.c's hooks for ota.c on the core's HAL: the device's ms, the wait (the watchdog fed: the coroutine yields), the
 * update's staging area (OTA_AREA, 5 x 4 KiB at 0xE0000; backup.c keeps its journal record in its second sector) on
 * the NOR image. The update's own steps (ota_session: its screen, its commit) are never reached here */
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_x0x_ota_in(uint32_t off, uint32_t n) { return off >= OTA_AREA && off + n <= OTA_AREA + OTA_AREA_LEN; }
static int ota_erase(uint32_t off)
{
    if (!fm1core_x0x_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_x0x_ota_in(off, n))
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
#define SMP_USER_XIP(k) ((const uint8_t *)fm1nor_image + 0xA0000u + (uint32_t)(k) * 0x14000u)
#include "app/slots.c"
#include "app/x0x.h"
#define ANN_NAME "X0X " X0X_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), not sent */
{
    return ota_wire_send(p, n);
}

/* what plat_fm1.c needs of the HAL beyond the plat family's (the second core, the guards, the crash record, the
 * performance counters): the second core runs its job at once, in order, as the host simulator's (the 909 and 808
 * render "there": the split path, the same sums) */
static int fm1_c1_on, fm1_c1_gave_up;
static struct {
    uint32_t bad;
} fm1_c1_mb;
static int fm1_cpu1_start(void)
{
    fm1_c1_on = 1;
    return 0;
}
static void fm1_cpu1_hold(void) { fm1_c1_on = 0; }
static int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1_c1_on)
        return 0;
    fn(arg);
    return 1;
}
static int fm1_cpu1_wait(void) { return 0; }
#define FM1_GUARD_PC 0u
static int fm1_guard_top_locked(void) { return 0; }
static void fm1_guard_unlock_top(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_guard_pc_open(void) {}
static void fm1_guard_enable(uint32_t g) { (void)g; }
#define FM1_CRASH_MAGIC 0x43525348u
static struct {
    uint32_t magic, vec, pc, emu, dbg, rets, count, core;
} fm1_crash;                              /* (no crash since power-on) */
#define FM1_C1_BAR_MAGIC 0x43314241u
static struct {
    uint32_t magic, barred;
} fm1_c1_bar;
static int fm1_perf_cycles_run(void) { return 0; }   /* (no CPU cycle counter: plat_cycles is the 24 MHz timer) */
static uint32_t fm1_perf_cycles(void) { return fm1_ticks(); }
static void fm1_perf_stalls(uint32_t s[3]) { s[0] = s[1] = s[2] = 0; }
static void fm1_perf_stalls_enable(int on) { (void)on; }
static uint32_t fm1core_x0x_irq_stack[64];
#define _sstack_lo fm1core_x0x_irq_stack
#define _sstack_top (fm1core_x0x_irq_stack + 64)

#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/project.c"
#include "app/ui.c"
/* what editor.c (the sample slots) needs of the HAL: the flash's erase and program on the NOR image (an erase stalls
 * the audio there, the device's audio_silence), the XIP cache (the image is read in place: nothing to drop) */
static void audio_silence(void) {}
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    *took = 0;
    return fm1nor_erase(off);
}
static int fl_write(uint32_t off, const void *p, uint32_t n) { return fm1nor_prog_span(off, p, n); }
static void fl_inval(uint32_t off, uint32_t n)
{
    (void)off;
    (void)n;
}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#include "app/editor.c"
#include "backup.c"

/* main_fm1.c's hooks for backup.c (its "backup and restore" section, on the core's HAL: the same statements) */
static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n) { return ota_read(addr, dst, n); }
static int rs_note(uint32_t addr)                   /* (past the question: no session deadline, 3 tries) */
{
    uint8_t b[8];
    ota_deadline = 0;
    return ota_read(addr, b, 8);
}
static int rs_playing(void) { return seq.playing || seq.req_stop; }
static void rs_begin(void)                          /* a backup is coming: patient now (5 s a read, 2 min in all) */
{
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
    draw_text_box(0, 92, 240, &FONT_B, "RESTORE FROM USB", C_WHITE, 1);
    draw_text_box(0, 124, 240, &FONT_S, b, C_HI, 1);
}
static int rs_ask(void)                             /* SEL yes, HOME no; playing or 60 s: no */
{
    uint32_t t0 = fm1_ms, was = plat_buttons(), sel = 1u << B_SEL, home = 1u << B_HOME;
    int yes = -1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 64, 240, &FONT_B, "RESTORE FROM USB?", C_WHITE, 1);
    draw_text_box(0, 92, 240, &FONT_S, "REPLACES THE PROJECT:", C_GRAY, 1);
    draw_text_box(0, 112, 240, &FONT_S, "SOUND, PATTERNS, SONG", C_GRAY, 1);
    draw_text_box(0, 150, 240, &FONT_B, "SEL  YES      HOME  NO", C_HI, 1);
    while (yes < 0 && fm1_ms - t0 < 60000u) {
        uint32_t b = plat_buttons(), down = b & ~was;
        fm1_wdt_feed();
        was = b;
        if (down & sel)
            yes = 1;
        else if ((down & home) || rs_playing())
            yes = 0;
    }
    for (t0 = fm1_ms; plat_buttons() && fm1_ms - t0 < 3000u;)
        fm1_wdt_feed();                             /* let go first: the UI must not take the press as its own */
    return yes > 0;
}
static void rs_reload(void) { app_restored(); }
static int rs_rec_erase(void) { return ota_erase(RS_REC); }
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n) { return ota_prog(RS_REC + off, src, n); }
static int rs_rec_read(uint32_t off, void *dst, uint32_t n) { return ota_fread(RS_REC + off, dst, n); }
static int bu_room(void)                            /* the SysEx ring empty: a frame goes in whole (a host is there) */
{
    return !fm1sx_out_queued(&fm1core_pl_sxo);
}
static int bu_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }
static void bu_flush(void) { ui_flush(); }
static int bu_unsaved(void) { return ui_dirty(); }
static void bu_hold(int on) { ui_hold_saves(on); }

/* main_fm1.c backup_service: the host's backup and restore requests (ota.c's framing; ota_service hands them over) */
static void backup_service(uint32_t req)
{
    if (req == 11u && ota_dec[2] == BU_REQ && bu_rd32(ota_dec + 6) == BU_MAGIC && flash_ok)
        bu_start();                                 /* a backup to the host: streamed by bu_step, playing on */
    bu_step();
    if (req == 15u && ota_dec[2] == RS_REQ && flash_ok) {
        uint32_t len = ota_rd32(ota_dec + 6), crc = ota_rd32(ota_dec + 10);
        int begun = 0, rc;
        if (bu.on)
            bu_stop();                              /* (a backup streaming: given up) */
        ota_try_ms = 300u;                          /* its head first: a host that does not answer costs a second */
        ota_deadline = ota_now_ms() + 3000u;
        rc = restore_rx(len, crc, &begun);
        ota_try_ms = 5000u;
        ota_deadline = 0;
        if (begun && rc)
            ui_redraw();                            /* (restored: app_restored drew it all again) */
        (rc ? ui_warn : ui_say)(!rc ? "RESTORED FROM USB" : rc == RS_E_PLAYING ? "STOP FIRST, THEN RESTORE"
                                : rc == RS_E_NO ? "NOT RESTORED" : "RESTORE FAILED", 0);
    }
}
#undef __attribute__
#undef _sstack_lo
#undef _sstack_top

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
static int fm1core_x0x_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || fm1sx_is_fl(f, n) || FM1CORE_PL_SX_READER(f, n);
}
static void fm1core_x0x_sx_service(void)  /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && ann_frame(p, n, fm1_ms))   /* a screen reader's, before the editor and the updater */
        ota_frame_done();
    ed_service();                         /* sample upload SysEx */
    backup_service(ota_service());        /* M-UPGRADE handshake; backup and restore */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_x0x_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_x0x_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready || bu.on)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "X0X"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the glow and the dim keys: one scan frame in GLOW_DIV */
static uint8_t fm1core_x0x_last_pat = 0xFF;
static void fm1core_pl_flash_boot(void)  /* fm1_main: a restore's commit finished, the user sample slots read in place */
{
    uint32_t k;
    rs_boot();                            /* a restore's commit cut short by a power loss: finished */
    for (k = 0; k < SMP_USER_SLOTS; k++)
        smp_user_scan(k);
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* fm1_main, after panel_init and led_pos_init */
{
    plat_perf_init();
    engine_init(proj.pat, &proj.arr.song, proj.arr.lane, &proj.sound);
    if (project_load() != 0)
        project_defaults();               /* nothing saved yet (or another format) */
    engine_apply_sound(&proj.sound);
    seq.bpm = (float)proj.set.bpm_x10 / 10.0f;
    seq.accent_q7 = proj.set.accent_q7;
    project_apply_io();
    engine_brk_loops();
    fm1core_x0x_last_pat = 0xFF;
    if (!proj.set.one_core)               /* (after a failed start, one core: no failed starts here) */
        plat_cpu2_set(1);
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block, one 256-frame part */
{
    engine_render(lr, n);
    engine_volume(lr, n);
    engine_load(0, n);                    /* the overload guard: no load measured (simulated time; the browser's) */
}
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;        /* 0 .. ~4096 */
}
static void fm1core_pl_loop(void)
{
    if (seq.ppat[TRK_BRK] != fm1core_x0x_last_pat) {   /* the break's new pattern may name other loops */
        fm1core_x0x_last_pat = seq.ppat[TRK_BRK];
        engine_brk_loops();
    }
}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        ui_say("UPDATE MODE IN ", d);
    } else {
        ui_say("UPDATE MODE CANCELLED", 0);
    }
}
/* the lazy save now: AUTOSAVE writes a change once stopped and quiet; SAVE pressed while playing waits for the stop.
 * Off (GLO > AUTOSAVE), only SAVE writes: the RAM is all there is, as on the device. Playing: deferred */
static int fm1core_pl_sync(void)
{
    if (!ui.dirty || (proj.set.autosave_off && !save_wait))
        return 1;
    if (seq.playing || seq.req_stop || save_hold)
        return 0;
    ui_flush();
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u part %u overlay %u playing %u bpm %.1f dirty %u", (unsigned)ui.view, (unsigned)ui.part,
             (unsigned)ui.overlay, (unsigned)seq.playing, (double)seq.bpm, (unsigned)ui.dirty);
}

#include "plat_glue.h"
#include "exit_end.h"
