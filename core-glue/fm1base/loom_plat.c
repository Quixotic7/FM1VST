/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's platform extras (app/plat_loom.c: "its platform extras beyond fm1-base's plat.h") for the core. app/app.c
 * includes app/plat_loom.c when PLAT_HOST is off, as the core builds it (the device's code paths: plat_fm1.c, the
 * voices' engine order, the cost model); plat_loom.c itself reads the hardware (the 24 MHz timer around each render,
 * felucca-bsp's supplies and crash record), so the core's compiler reads this file in its place (core_loom.cmake: a
 * clang VFS overlay maps app/plat_loom.c here; app.c and every other file of the submodule are compiled unmodified).
 * The same names, as fm1-base's host simulator (host/plat_host.c) gives them:
 *
 * - THE SECOND CORE (fm1.toml cpu2 = true: fm1-base's plat_cpu2_*, whose device half, cpu2.c, is not built here): a
 *   job runs at once, in order, on the host's one thread, plat_core() 1 while it runs, as plat_host.c simulates it:
 *   engine.c's split render gives the same sums as on a two-core FM-1. It is never given up or barred.
 * - THE LOAD: the render is timed on the device's 24 MHz clock, which is simulated (it does not move during a
 *   render): 0 %, as the browser emulator gives the overload guard.
 * - The supplies as fm1-base's main_fm1.c sets them at start (VDC14 level 7: 1.60 V, SYSVDD level 15: 1.38 V), no
 *   crash recorded, and the store's copy before the current one: storage.c's st_load_prev, as plat_loom.c reads it. */

/* the second core, simulated (plat_host.c's) */
static int loom_c2_on, loom_c2_in_job;
int plat_cpu2_set(int on)
{
    loom_c2_on = on != 0;
    return 0;
}
int plat_cpu2_on(void) { return loom_c2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!loom_c2_on)
        return 0;
    loom_c2_in_job = 1;
    fn(arg);
    loom_c2_in_job = 0;
    return 1;
}
uint32_t plat_core(void) { return (uint32_t)loom_c2_in_job; }   /* (plat_fm1.c's, fm1_cnum, renamed away) */
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }   /* (a job ran at once) */
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }

/* the render's load (app_render): simulated time, 0 % */
uint32_t plat_cpu_peak_pct(void) { return 0; }
static uint32_t render_t0(void) { return fm1_ticks(); }
static uint32_t render_pct(uint32_t t0, uint32_t n)
{
    (void)t0;
    (void)n;
    return 0;
}
uint32_t plat_cycles(void) { return fm1_ticks(); }   /* the profiler's clock: the 24 MHz timer, as on the FM-1 */

/* the supplies as fm1_main sets them (fm1_vdc14_raise(7), fm1_sysvdd_raise(15)), the last crash: none */
int plat_vdc14_mv(void) { return 1250 + 50 * 7; }
int plat_sysvdd_mv(void) { return 930 + 30 * 15; }
int plat_last_crash(uint32_t v[5])
{
    (void)v;
    return 0;
}

/* the copy before the current one (storage.c keeps it while it can): a stream torn across objects falls back to the
 * save before (project.c load_stream); storage.c on the NOR image, in the same unit */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load_prev(obj, dst, max) : -1; }
int loom_store_save(uint32_t obj, const void *src, uint32_t len) { return plat_store_save(obj, src, len); }

static void plat_loom_init(void) {}
