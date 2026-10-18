/* SPDX-License-Identifier: GPL-3.0-only */
/* The X0X core: X0X (https://github.com/charlesvestal/fm1-x0x, the pinned, unmodified submodule cores/fm1-x0x)
 * built as one unit with the plat glue (core-glue/plat/README.md). The include list follows the device's unity build
 * (firmware/src/x0x.c) with the hardware left out, as the firmware's own host simulator (host/x0x_host.c) does:
 * gfx.c, plat.h, Felucca's storage.c (on the NOR image), the user sample slots (slots.c, read in place from the
 * image at 0xA0000), the screen reader (announce.c, nobody listening), the panel calibration (panel.c), the
 * device's plat.h implementation (plat_fm1.c), the project and the UI. The DSP, the sequencer and the engine are
 * their own units, as in the firmware's builds (x0x_*.c wrappers, -O2, no fused multiply-add).
 *
 * X0X's version is a build-time define (X0X_VERSION, "DEV" by default): the hub's version (core_x0x.cmake). */
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
#include "plat_hal.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections, alignment hints) */
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
#include "storage.c"
#define SMP_USER_XIP(k) ((const uint8_t *)fm1nor_image + 0xA0000u + (uint32_t)(k) * 0x14000u)
#include "app/slots.c"
#include "app/x0x.h"
#define ANN_NAME "X0X " X0X_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* (no SysEx path to a screen reader: not sent) */
{
    (void)p;
    (void)n;
    return -1;
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
#undef __attribute__
#undef _sstack_lo
#undef _sstack_top

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "X0X"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the glow and the dim keys: one scan frame in GLOW_DIV */
static uint8_t fm1core_x0x_last_pat = 0xFF;
static void fm1core_pl_flash_boot(void)  /* fm1_main: the user sample slots, read in place */
{
    uint32_t k;
    for (k = 0; k < SMP_USER_SLOTS; k++)
        smp_user_scan(k);
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
