/* SPDX-License-Identifier: GPL-3.0-only */
/* The K-WASUB core: K-WASUB (https://github.com/Guizmoh/K-WASUB_Firmware_for_FM1, the pinned, unmodified submodule
 * cores/K-WASUB_Firmware_for_FM1), a bass synthesizer born from KAWA, on Dinghy's platform.c over Felucca's, built as
 * its host mode (DINGHY_HOST). platform.c then includes "../../tests/host.c", which K-WASUB's tree does not carry:
 * core_kwasub.cmake maps that path to kwasub_host.c (clang's -ivfsoverlay), whose platform_poll is the coroutine's
 * yield. The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the main loop  ksub_ui.c's app_main (app_init, then platform_poll and app_step for ever); g_frame_send polls
 *                  between the screen's bands, as on the device
 *   the voices     on one core (KS_DUAL 0, the device's default; the browser's build too)
 *   the input      into the shim's fm1_in, edges (press, release, the keys' note edges) and encoder steps; the keys
 *                  are switches
 *   the flash      the device's own layout in the NOR image (kwasub_host.c): the store's six sectors at 0x97000 (the
 *                  32 user patches, SEQ BASS / ARP / MACROS, SEQ BASS's long part, DRUMS, PARAMETER LOCKS), the
 *                  settings sector at 0xFC000 (DNGY record)
 *   SysEx          in: as the device's platform_poll hands them, the app's own first (ksub_link.c app_sysex: the DATA
 *                  page's backup / RESAMPLE link, 7D 4B 57 ..), the rest to ota.c's ota_service (the M-UPGRADE
 *                  identity FM-1_1990, tools/build.py --release 1.Y); out: their replies through ota_wire_send
 *   the LEDs       host_leds per id (0..13 buttons, 14 + k keys): 2 = lit, 1 = dim
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the rescue, the boot guard, the
 * second core (the device branch's). */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "ksub.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq zeroes the half), then MASTER and the clip; the tick
 * moves with the frames (fm1_ticks, as the browser's) */
static void fm1core_kw_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    fm1core_kw_frames += frames;
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_kw_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    w_note_edges |= notes & ~fm1_in.notes;
    fm1_in.notes = notes & 0x7FFFFFFu;
    fm1_in.buttons = held;
    w_pressed |= down;
    w_released |= up;
    for (i = 0; i < 7u; i++)
        w_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    if (id > 40u)
        return FM1P_LED_OFF;
    return w_leds[id] == 2u ? FM1P_LED_LIT : w_leds[id] == 1u ? FM1P_LED_DIM : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (mq_w - mq_r >= 64u)
        return 0;
    mq[mq_w % 64u][0] = st;
    mq[mq_w % 64u][1] = d1;
    mq[mq_w % 64u][2] = d2;
    mq_w++;
    return 1;
}
/* the stores are the NOR image itself (kwasub_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* app_step's deferred saves (2 s after a change, when nothing sounds), now: SEQ BASS / ARP / MACROS and the rest of
     * the store's second sector (perf_save), the settings (settings_write). The user patches are written when SAVE
     * stores one (store_save), at once */
    if (!booted)
        return 1;
    if (perf_dirty) {
        perf_dirty = 0;
        perf_save();
    }
    if (set_dirty) {
        set_dirty = 0;
        settings_write();
    }
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u, %s %u \"%s\"%s%s", (unsigned)page, set.bank ? "user" : "factory", (unsigned)set.idx + 1u,
             pname, sq_play ? ", playing" : "", ks_sounding ? ", sounding" : "");
}

/* SysEx in, as platform.c's platform_poll: the app's own frame first (the DATA page's link), then the updater's */
static void fm1core_kw_sysex(const uint8_t *p, uint32_t n)
{
    if (app_sysex(p, n))
        return;
    fm1core_kw_ota_p = p;
    fm1core_kw_ota_n = n;
    ota_service();
    fm1core_kw_ota_p = NULL;
}
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_kw_sysex((p), (n))

#include "dinghy_glue.h"
