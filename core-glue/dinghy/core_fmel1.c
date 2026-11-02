/* SPDX-License-Identifier: GPL-3.0-only */
/* The FMEL-1 core: FMEL-1 (https://github.com/AntonioAltea/fmel-1, the pinned, unmodified submodule cores/fmel-1),
 * MEL, a Mellotron-style tape keyboard on Dinghy's platform.c over Felucca's, built as its host mode (DINGHY_HOST).
 * platform.c then includes "../../tests/host.c", a program (script, WAV, main()): core_fmel1.cmake maps that path to
 * fmel1_host.c (clang's -ivfsoverlay), whose platform_poll is the coroutine's yield. The family glue (dinghy_pre.h,
 * dinghy_glue.h) runs the main loop:
 *
 *   the main loop  mel.c's own app_main (app_init, then platform_poll and app_loop for ever)
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (host_enc); the keys are
 *                  switches (velocity 100, as on the device: MEL plays every key at one level)
 *   the flash      the settings (every parameter, the SEQ's steps, each sound's PLAY) as on the device: one DNGY record
 *                  at 0xFC000 (fmel1_host.c over the NOR image), saved 2 s after a change when silent, or by SAVE
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them (the
 *                  M-UPGRADE identity FM-1_950: tools/build.py --release 1.0 stamps FM-1_95<minor>); out: the keys'
 *                  notes (midi_send)
 *   the LEDs       host_led per id (0..13 buttons, 14 + k keys), lit or off
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the rescue, the boot guard (the
 * device branch's). */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "mel.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (as platform.c's fm1_alnk0_irq zeroes the half), then MASTER and the clip */
static void fm1core_mel_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_mel_render((o), (frames), (q12))

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
    return id < 64u && (host_led[id >> 5] >> (id & 31u) & 1u) ? FM1P_LED_LIT : FM1P_LED_OFF;
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
/* the store is the NOR image itself (fmel1_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* app_loop's deferred save (2 s after a change, or SAVE), now. The device waits for silence because an erase
     * stalls its sound; here the flash is the image (no stall), so it saves at once, as app_loop then would */
    if (!dirty && !save_req)
        return 1;
    play_keep();
    settings_save(&set, sizeof set);
    dirty = 0;
    save_req = 0;
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %s, sound %s%s%s%s%s", page < NPG ? PG_NAME[page] : "?",
             (uint32_t)PV(P_SOUND) < MEL_NSETS ? MEL_SET_NAME[PV(P_SOUND)] : "?", hold ? ", hold" : "",
             arp_on ? ", arp" : "", seq_play ? ", seq playing" : "", dirty ? ", unsaved" : "");
}

/* the M-UPGRADE identity: platform.c's platform_poll gives every SysEx frame to ota_service */
#define FELUCCA_ID "FM-1_950"             /* tools/build.py --release 1.0: PRODUCT FM-1_95<minor> */
#define FM1CORE_DG_OTA_C "../../cores/fmel-1/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
