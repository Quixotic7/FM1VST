/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's platform extras (app.h: "on the FM-1 in plat_loom.c, in the simulator in host.c") for the core. app/app.c
 * includes app/plat_loom.c when PLAT_HOST is off, as the core builds it (the device's code paths: plat_fm1.c, the
 * voices' engine order, the cost model); plat_loom.c itself is the hardware's (the second core of cpu1/: its
 * mailbox registers, the PC limits, its .c1_text entry; felucca-bsp's supplies and crash record), so the core's
 * compiler reads this file in its place (core_loom.cmake: a clang VFS overlay maps app/plat_loom.c here; app.c and
 * every other file of the submodule are compiled unmodified). The same names, as host.c simulates them:
 *
 * - THE SECOND CORE: its job runs at once, in order, on the host's one thread (plat_cpu2_run calls it and says "the
 *   second core has it", plat_cpu2_wait returns done): engine.c's split render (the upper half of the voices and the
 *   drum lines here while "core 1" renders the lower half, then the halves summed in the one-core order) runs as on
 *   a two-core FM-1, with the same sums, as host.c and the browser emulator run it. It is never given up or barred
 *   (no unit faults here), so a release starts with two cores (app_init: proj.one_core = barred = 0).
 * - THE LOAD: the render is timed on the device's 24 MHz clock, which is simulated (it does not move during a
 *   render): 0 %, as the browser emulator gives the overload guard (app.c: "a browser's timing is not the FM-1's").
 * - The supplies as fm1-base's main_fm1.c sets them at start (VDC14 level 7: 1.60 V, SYSVDD level 15: 1.38 V), no
 *   crash recorded, and the store's copy before the current one read from the NOR image as the device reads it
 *   (storage.c's A/B pair: the same statements as plat_loom.c's). */

/* the second core (X0X's plat_cpu2_*), simulated: a job runs at once (host.c's) */
static int loom_c2_on;
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
    fn(arg);
    return 1;
}
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }   /* (a job ran at once) */
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
void app_cstart(void) {}                 /* (fm1_cstart's: no second core left running by a reset) */

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

/* the copy before the current one (storage.c keeps two): a stream torn across objects falls back to the save before
 * (project.c load_stream); storage.c on the NOR image, compiled before the app in the same unit */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    int cur;
    if (!flash_ok)
        return -1;
    cur = st_current(obj, &h);
    if (cur < 0 || st_head(obj, (uint32_t)!cur, &h) || st_body(obj, (uint32_t)!cur, &h))
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}
int loom_store_save(uint32_t obj, const void *src, uint32_t len) { return plat_store_save(obj, src, len); }

static void plat_loom_init(void) {}
static int plat_loom_two_cores_ok(void) { return 1; }   /* (the device's one core after a failed start guards against
                                                         * core 1's faults, which a host never has) */
