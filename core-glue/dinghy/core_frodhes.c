/* SPDX-License-Identifier: GPL-3.0-only */
/* The Frodhes core: Frodhes (https://github.com/leonardoMirabal/frodhes, the pinned, unmodified submodule
 * cores/frodhes) on its web route, web/web_hal.c (DINGHY_HOST + DINGHY_WEB: the shim its browser emulator uses),
 * built natively with a stub <emscripten.h> (frodhes_inc/: the exports plain functions, emscripten_sleep the
 * coroutine's yield). The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop as a coroutine; web_hal.c's
 * own main() and web_audio() are not used (the glue calls app_audio and applies MASTER as platform.c does).
 *
 *   MIDI out   web_hal.c drops it; the app has it (the keys' notes, the arpeggiator, the profiler's CCs): its no-op is
 *              renamed below and frodhes_hook/rhodes_tables.h gives the name back to the glue's midi_send
 *   the flash  the app's RAM stores under DINGHY_HOST, copied into the NOR image at the device's offsets in the
 *              device's formats: the settings record (web_store_buf: platform.c's DNGY sector at 0xFC000), the preset
 *              banks A / B (presets.c host_bank: raw sectors 0x97000 / 0x98000) and the pattern recorder's A / B
 *              (seq.c sq_host_fl: 0x99000 / 0x9A000); read back before the boot */
#define DINGHY_HOST 1
#define DINGHY_WEB 1
#include "dinghy_pre.h"

#define main fm1core_frd_web_main                  /* (web_hal.c's main: unused) */
#define midi_send fm1core_frd_web_midi_send        /* (web_hal.c's no-op: frodhes_hook/rhodes_tables.h undoes it) */
#include "frodhes.c"
#undef main
#undef midi_send
#include "exit_end.h"

#define FM1CORE_FRD_SETTINGS_AT 0x000FC000u        /* platform.c SETTINGS_AT */

static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_released |= up;
    for (i = 0; i < FM1P_NENC; i++)
        host_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return host_led[id] ? FM1P_LED_LIT : host_glow[id] ? FM1P_LED_DIM : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)   /* web_hal.c web_midi */
{
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
    uint32_t n = 0;
    memset(web_store_buf, 0, sizeof web_store_buf);
    if (!fm1core_dg_sector_read(FM1CORE_FRD_SETTINGS_AT, web_store_buf + 4, sizeof web_store_buf - 4u, &n) &&
        n == sizeof set)
        web_store_buf[0] = 1;                       /* (platform.c settings_load: the length must match) */
    memcpy(host_bank[0], fm1nor_image + PR_BANK_A, 4096);
    memcpy(host_bank[1], fm1nor_image + PR_BANK_B, 4096);
    memcpy(sq_host_fl[0], fm1nor_image + SQ_A, 4096);
    memcpy(sq_host_fl[1], fm1nor_image + SQ_B, 4096);
    web_store_dirty = 0;
}
static void fm1core_dg_stores_poll(void)
{
    if (web_store_dirty) {
        web_store_dirty = 0;
        fm1core_dg_sector_save(FM1CORE_FRD_SETTINGS_AT, web_store_buf + 4, sizeof set);
    }
    fm1core_dg_raw_sync(PR_BANK_A, host_bank[0]);
    fm1core_dg_raw_sync(PR_BANK_B, host_bank[1]);
    fm1core_dg_raw_sync(SQ_A, sq_host_fl[0]);
    fm1core_dg_raw_sync(SQ_B, sq_host_fl[1]);
}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred saves, now: the settings (2 s after a change, when quiet), a preset SAVE (when
     * quiet), the recorded pattern (1.5 s after an edit, stopped and quiet) */
    if (set_dirty) {
        settings_save(&set, sizeof set);
        set_dirty = 0;
    }
    if (pr_save_pending) {
        pr_save_pending = 0;
        ui_say(presets_save_bank() ? "SAVE FAILED" : "SAVED");
    }
    if (sq_dirty && !seq_play) {
        sq_change_t = fm1_ms - 1501u;
        seq_save_poll();                            /* (still waits for the voices and effects to fall quiet) */
    }
    return !sq_dirty;
}
static void fm1core_dg_info(char *buf, uint32_t n) { snprintf(buf, n, "page %s", web_page()); }

#include "dinghy_glue.h"
