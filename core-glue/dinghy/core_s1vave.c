/* SPDX-License-Identifier: GPL-3.0-only */
/* The S1Vave core: S1Vave (https://github.com/AndrewGG88/S1Vave, the pinned, unmodified submodule cores/s1vave; Andrew
 * GG's 8-voice virtual-analog fork of zvenson's OberVave) on its own fake FM-1, tests/host.c, the way OberVave's
 * glue (core_obervave.c) builds its parent: OBV_HOST + OBV_WASM (host.c's panel statics, RAM stores and MIDI queue;
 * no script, no main; the glue supplies platform_poll). The family glue (dinghy_pre.h, dinghy_glue.h) runs the main
 * loop as a coroutine.
 *
 * host.c logs its saves and its MIDI out with printf ("settings saved (n B)", "blob i saved", "programs i saved",
 * "MIDI out st d1 d2": S1Vave also sends a CC for each parameter turned): here printf goes to fm1core_s1v_printf,
 * which takes those lines as the events they report: a save copies the RAM record into the NOR image in platform.c's
 * sector format at the device's offsets (settings 0xFC000, the 8 pattern blobs 0xF4000.., the 2 program sectors
 * 0x97000..), a MIDI out line becomes a USB-MIDI packet. Before the boot the image's records are read back into
 * host.c's arrays. The .pool section attribute (gfx.c, obv_fx.h) Mach-O rejects: core_s1vave.cmake's FLAGS. */
#define OBV_HOST 1
#define OBV_WASM 1
#include "dinghy_pre.h"

static int fm1core_s1v_printf(const char *fmt, ...);
#define printf(...) fm1core_s1v_printf(__VA_ARGS__)

#include "s1vave.c"

#undef printf
#include "exit_end.h"

/* platform.c's non-SMK layout (its device branch: not compiled here) */
#define FM1CORE_S1V_SETTINGS_AT 0x000FC000u
#define FM1CORE_S1V_BLOBS_AT 0x000F4000u
#define FM1CORE_S1V_PSECT_AT 0x00097000u

/* the poll (host.c declares it under OBV_WASM: sim.c's, a millisecond of the page's clock) */
static void platform_poll(void) { fm1core_dg_poll(); }

static uint8_t *fm1core_s1v_rec(uint32_t kind, uint32_t i, uint32_t *at)   /* 0 settings, 1 blob, 2 program sector */
{
    if (kind == 0) {
        *at = FM1CORE_S1V_SETTINGS_AT;
        return host_store;
    }
    if (kind == 1 && i < NBLOB) {
        *at = FM1CORE_S1V_BLOBS_AT + i * 0x1000u;
        return host_blob[i];
    }
    if (kind == 2 && i < NPSECT) {
        *at = FM1CORE_S1V_PSECT_AT + i * 0x1000u;
        return host_psect[i];
    }
    return NULL;
}
static void fm1core_s1v_saved(uint32_t kind, uint32_t i, uint32_t n)
{
    uint32_t at = 0;
    uint8_t *r = fm1core_s1v_rec(kind, i, &at);
    if (r && r[0] == 1u && n <= SETTINGS_MAX)
        fm1core_dg_sector_save(at, r + 4, n);
}
static int fm1core_s1v_printf(const char *fmt, ...)
{
    va_list a;
    unsigned ms, x, y, z;
    if (!strstr(fmt, " ms  "))
        return 0;                         /* (only host.c's event lines: "%6u ms  ...") */
    va_start(a, fmt);
    ms = va_arg(a, unsigned);
    (void)ms;
    if (strstr(fmt, "MIDI out")) {
        x = va_arg(a, unsigned);
        y = va_arg(a, unsigned);
        z = va_arg(a, unsigned);
        fm1core_dg_midi_out(x, y, z);
    } else if (strstr(fmt, "settings saved")) {
        x = va_arg(a, unsigned);
        fm1core_s1v_saved(0, 0, x);
    } else if (strstr(fmt, "blob %u saved")) {
        x = va_arg(a, unsigned);
        y = va_arg(a, unsigned);
        fm1core_s1v_saved(1, x, y);
    } else if (strstr(fmt, "programs %u saved")) {
        x = va_arg(a, unsigned);
        y = va_arg(a, unsigned);
        fm1core_s1v_saved(2, x, y);
    }
    va_end(a);
    return 0;
}

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;                   /* (fm1_input_edges: taken by the app's pass) */
    host_rel |= up;
    for (i = 0; i < FM1P_NENC; i++) {
        host_enc[i] += enc[i];
        host_enc_pos[i] += enc[i];
    }
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return host_leds >> id & 1u ? FM1P_LED_LIT : host_glow >> id & 1u ? FM1P_LED_DIM : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)   /* sim.c sim_midi */
{
    if (st == 0xFEu)
        return 1;
    if (mq_w - mq_r >= 64u)
        return 0;
    mq[mq_w % 64u][0] = st;
    mq[mq_w % 64u][1] = d1;
    mq[mq_w % 64u][2] = d2;
    mq_w++;
    return 1;
}
static void fm1core_dg_stores_load(void)
{
    uint32_t k, i, at = 0, n = 0;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < (k == 1 ? NBLOB : k == 2 ? NPSECT : 1u); i++) {
            uint8_t *r = fm1core_s1v_rec(k, i, &at);
            memset(r, 0, SETTINGS_MAX + 4u);
            if (!fm1core_dg_sector_read(at, r + 4, SETTINGS_MAX, &n))
                r[0] = 1;
        }
}
static void fm1core_dg_stores_poll(void) {}   /* (each save is copied when host.c reports it) */
static int fm1core_dg_sync(void)
{
    uint32_t i;
    /* the main loop's settings save, now (it waits until 2 s after the change and silence) */
    if (set_dirty || ps_dirty) {
        for (i = 0; i < NPSECT; i++)
            if (ps_dirty >> i & 1u)
                psect_save(i, &psect[i], sizeof psect[i]);
        ps_dirty = 0;
        if (set_dirty)
            settings_save(&set, sizeof set);
        set_dirty = 0;
    }
    /* a pattern save the app has queued (it runs when stopped and quiet): not yet */
    return !(pat_dirty || pend_slot || copy_to);
}
static void fm1core_dg_info(char *buf, uint32_t n) { snprintf(buf, n, "page %u, program %u", (unsigned)page, (unsigned)prog + 1u); }

/* the installer's M-UPGRADE identity: the device's platform_poll runs ota_service every pass (dinghy_glue.h) */
#define FELUCCA_ID "FM-1_950"             /* tools/build.py --release 1.0: FM-1_95 + the minor version */
#define FM1CORE_DG_OTA_C "../../cores/s1vave/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
