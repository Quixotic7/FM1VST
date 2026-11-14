/* SPDX-License-Identifier: GPL-3.0-only */
/* The Penumbra core: Penumbra (https://github.com/MadMooneyham/Penumbra, the pinned, unmodified submodule
 * cores/Penumbra; Lunar Eclipse Forge, a fork of X0X) built as one unit with the plat glue (core-glue/plat/README.md).
 * The include list follows the device's unity build (firmware/src/penumbra.c) with the hardware left out, as the
 * firmware's own host simulator (host/penumbra_host.c) does: gfx.c, plat.h, Felucca's storage.c (on the NOR image),
 * the app's header, the screen reader (announce.c, nobody listening), the panel calibration (panel.c), the device's
 * plat.h implementation (plat_fm1.c), the settings and journeys (settings.c) and the UI. The DSP, the clock, the
 * journey and looper and the engine are their own units (penumbra_*.c), as tools/build.py's O2_UNITS.
 *
 * Its version is a build-time define (PEN_VERSION, "DEV" by default): the hub's version (core_penumbra.cmake).
 * The installer finds the unit by its M-UPGRADE identity (ota.c through common/sysex_ota.h); the update key is not
 * taken (plat_sx.h), so nothing is written. */
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

#define PEN_HOST 1                        /* the device's .pool section off (plat.h PEN_POOL) */
#define PEN_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_73010400"        /* the package identity: tools/build.py --release 1.4 (FM-1_73 01 01 00) */
#define FM1CORE_PL_SX 1                   /* the installer's M-UPGRADE identity, the screen reader (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections, alignment hints) */
#include "gfx.c"
static void palette_set(uint32_t p) { (void)p; }   /* (Penumbra's gfx.c has one palette: plat_glue.h's call) */
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
#include "storage.c"
#include "app/penumbra.h"
#define ANN_NAME FW_NAME " " PEN_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), not sent */
{
    return fm1core_pl_sx_send(p, n);
}

/* what plat_fm1.c needs of the HAL beyond the plat family's (the second core, the guards, the crash record, the
 * performance counters): the second core runs its job at once, in order, as the host simulator's */
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
static uint32_t fm1core_penumbra_irq_stack[64];
#define _sstack_lo fm1core_penumbra_irq_stack
#define _sstack_top (fm1core_penumbra_irq_stack + 64)

#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/settings.c"
#include "app/ui.c"
#undef __attribute__
#undef _sstack_lo
#undef _sstack_top

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c's loop: the screen reader's frame, then ota_service (the firmware's ota.c, through common/sysex_ota.h).
 * The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h). */
#define FM1SX_OTA_C "../../cores/Penumbra/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
static int fm1core_penumbra_sx_accept(const uint8_t *f, uint32_t n)
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n);
}
static void fm1core_penumbra_sx_service(void)   /* main_fm1.c fm1_main, each pass (ota_req: not taken) */
{
    const uint8_t *p;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &p, &n) && ann_frame(p, n, fm1_ms))   /* a screen reader's, before the updater */
        fm1sx_done(&fm1core_pl_sxi);
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_penumbra_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_penumbra_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "Penumbra"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the buttons' glow and the dim keys: one scan frame in GLOW_DIV */
static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main, after panel_init and led_pos_init */
{
    plat_perf_init();
    engine_init();
    settings_load();                      /* (nothing saved yet, or another firmware's: the defaults) */
    settings_apply();
    journey_load_work();                  /* the journey that was playing (else ECLIPSE) */
    loop_load_work();                     /* and its loop */
    if (!cfg.one_core)                    /* (after a failed start, one core: no failed starts here) */
        plat_cpu2_set(1);
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BG);
    ui_init();
    ui_start();                           /* PLAY AT POWER-ON */
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block, one 256-frame part */
{
    engine_render(lr, n);
    engine_volume(lr, n);
    engine_load(0, n);                    /* the overload guard: no load measured (simulated time) */
}
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;        /* 0 .. ~4096 */
}
static void fm1core_pl_loop(void) { plat_cpu_mhz10(); }
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        ui_say("UPDATE MODE IN ", d);
    } else {
        ui_say("UPDATE MODE CANCELLED", 0);
    }
}
/* the lazy save now. The device's AUTOSAVE waits for a second of silence, which a journey playing (the drone) never
 * gives, so the host's flash_sync does what SAVE does (ui.c save_now: written at once, whatever plays), quietly; a
 * loop being recorded waits (loop_save_work 1: deferred) */
static int fm1core_pl_sync(void)
{
    if (ui.dirty & D_LOOP) {
        const int r = loop_save_work();
        if (r == 0)
            ui.dirty &= (uint8_t)~D_LOOP;
        else if (r > 0)
            return 0;
    }
    if ((ui.dirty & D_SET) && settings_save() == 0)
        ui.dirty &= (uint8_t)~D_SET;
    if ((ui.dirty & D_JR) && journey_save_work() == 0)
        ui.dirty &= (uint8_t)~D_JR;
    return !ui.dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u playing %u steps %u dirty %u", (unsigned)ui.view, (unsigned)eng_clock.playing,
             (unsigned)eng_steps, (unsigned)ui.dirty);
}

#include "plat_glue.h"
#include "exit_end.h"
