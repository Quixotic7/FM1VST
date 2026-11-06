/* SPDX-License-Identifier: GPL-3.0-only */
/* The CULTBOX core: CULTBOX by beatcult (https://github.com/beatcult/CULTBOX, the pinned, unmodified submodule
 * cores/CULTBOX), a three-channel groovebox (Elka drums, the Polivoks-style PLVKS, the DUBMARINE siren) on Dinghy's
 * platform.c over Felucca's, built as its host mode (CULTBOX_HOST: its fork's name for Dinghy's DINGHY_HOST).
 * platform.c then includes "../../tests/host.c", a program (script, WAV, main()): core_cultbox.cmake maps that path to
 * cultbox_host.c (clang's -ivfsoverlay), whose platform_poll is the coroutine's yield. The family glue (dinghy_pre.h,
 * dinghy_glue.h) runs the main loop:
 *
 *   the main loop  cultbox.c's own app_main (the boot, then platform_poll and the pass for ever; the splash takes no
 *                  time in the host mode, as in its own host sim)
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (host_enc); the keys are
 *                  switches
 *   the flash      store.h's FM-1 layout over the NOR image (cultbox_host.c): the settings at 0xFC000 and the blobs
 *                  (blob 0: the 16 user presets) under "DNGY", the slots (0: the drum patterns, 1: PLVKS's; 4 sectors
 *                  each from 0x97000) under "SLOT" ^ "DNGY". The main loop saves when quiet (1.5 s without sound, no
 *                  pattern running); flash_sync does it at once (fm1core_dg_sync below)
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them: the
 *                  M-UPGRADE identity FM-1_333 (tools/build.py FM1_IDENTITY, every release); out: its replies
 *   the LEDs       host_leds per id (0..13 buttons, 14 + k keys): lit; host_glow (fm1_led_key_glow): dim
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the TRS jack, the rescue, the boot
 * guard (the device branch's). */
#define CULTBOX_HOST 1
#include "dinghy_pre.h"

#include "cultbox.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq hands app_audio a zeroed half), then MASTER and the
 * clip */
static void fm1core_cb_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_cb_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_rel |= up;
    for (i = 0; i < 7u; i++)
        host_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    if (id >= 64u)
        return FM1P_LED_OFF;
    return host_leds >> id & 1u ? FM1P_LED_LIT : host_glow >> id & 1u ? FM1P_LED_DIM : FM1P_LED_OFF;
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
/* the stores are the NOR image itself (cultbox_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
/* the settings as the main loop writes them (the panel's values, the preset), the page, octave and channel live */
static void fm1core_cb_settings(settings_t *s)
{
    uint32_t p;
    *s = set;
    for (p = 0; p < NPARAM; p++)
        s->p[p] = val[p];
    s->preset = (uint8_t)cur;
}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred saves, now. Its own wait for quiet (an erase stalls the device's sound) is skipped:
     * flash_sync asks for them at once. The settings' dirty flag is app_main's local, so the record is written when
     * it differs from the one in flash; the patterns as SAVE writes them (explicit_save) */
    settings_t s, old;
    uint32_t k;
    if (!ready)
        return 1;                         /* (not up yet) */
    for (k = 0; k < 2u; k++) {
        if (trk[k].dirty) {
            explicit_save = 1;
            pattern_store(k, cur_pat_t(k));
            explicit_save = 0;
        } else if (trk[k].flash_due) {
            slot_save(k, &pbank[k], sizeof pbank[k]);
            trk[k].flash_due = 0;
        }
    }
    fm1core_cb_settings(&s);
    if (settings_load(&old, sizeof old) || memcmp(&old, &s, sizeof s)) {
        set = s;
        settings_save(&set, sizeof set);
    }
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    static const char *const ENG[3] = {"PLVKS", "DUBMARINE", "DRUMS"};
    const uint32_t e = engine_val();
    char pr[24];
    if (cur < NFACTORY)
        snprintf(pr, sizeof pr, "%s", FACTORY_NAME[cur]);
    else
        snprintf(pr, sizeof pr, "U%02u", (unsigned)(cur - NFACTORY + 1u));
    snprintf(buf, n, "%s, preset %s%s, %s%s%s%s", e < 3u ? ENG[e] : "?", pr, modified ? "*" : "",
             mixview ? "mix" : seqmode ? "seq" : "edit", trk[0].q.run ? ", drums playing" : "",
             trk[1].q.run ? ", plvks playing" : "", trk[0].dirty || trk[1].dirty ? ", unsaved pattern" : "");
}

/* the M-UPGRADE identity: platform.c's platform_poll gives every frame to ota_service */
#define FELUCCA_ID "FM-1_333"             /* tools/build.py FM1_IDENTITY: CULTBOX's own, every release */
#define FM1CORE_DG_OTA_C "../../cores/CULTBOX/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
