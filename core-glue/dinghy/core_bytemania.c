/* SPDX-License-Identifier: GPL-3.0-only */
/* The Bytemania core: Bytemania (https://github.com/llozd/bytemania, the pinned, unmodified submodule
 * cores/bytemania), a bytebeat synth on Dinghy's platform.c over Felucca's, built as its host mode (DINGHY_HOST).
 * platform.c then includes "../../tests/host.c", a program (script, WAV, main()): core_bytemania.cmake maps that
 * path to bytemania_host.c (clang's -ivfsoverlay), whose platform_poll is the coroutine's yield. The family glue
 * (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (enc_acc); the keys are
 *                  switches
 *   the flash      the device's own layout in the NOR image (bytemania_host.c): the settings record at 0xFC000
 *                  (DNGY), the library's four sectors (64 user slots, "BYT2") from 0x97000
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them: the
 *                  M-UPGRADE identity (FM-1_942: tools/build.py --release 1.3.0); out: its replies
 *   the LEDs       host_led per id (0..13 buttons, 14 + k keys): on = lit
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the rescue, the boot guard (the
 * device branch's). */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "bytemania.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq hands app_audio a zeroed half), then MASTER and the
 * clip */
static void fm1core_bm_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_bm_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_rel |= up;
    for (i = 0; i < 7u; i++)
        enc_acc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return id < 64u && host_led[id] ? FM1P_LED_LIT : FM1P_LED_OFF;
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
/* the stores are the NOR image itself (bytemania_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred saves, now: the settings (2 s after a change) and the library's changed sectors
     * (the highest first, as the loop does), both only when nothing sounds: an erase stalls the sound, so the device
     * waits, and so does this */
    uint32_t k;
    if (set.magic != 'C')
        return 1;                         /* (not up yet) */
    if (!silent())
        return !(dirty || lib_dirty);
    if (dirty) {
        settings_save(&set, sizeof set);
        dirty = 0;
    }
    if (lib_dirty) {
        for (k = LIB_SECTORS; k-- > 0;)
            if (lib_dirty >> k & 1u)
                lib_save(k, LIB_V2, &lib[k * SLOTS_PER], SLOTS_PER * sizeof(slot_t));
        lib_dirty = 0;
    }
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "patch %s%u %s, %s, %s view%s", cur >= USER ? "U" : "", (unsigned)(cur >= USER ? cur - USER + 1u : cur + 1u),
             patch_name(cur), vmode < MODES ? MODE_NAME[vmode] : "?", set.view == VIEW_SCOPE ? "scope" : "raster",
             dirty || lib_dirty ? ", unsaved" : "");
}

/* the M-UPGRADE identity: platform.c's platform_poll gives every frame to ota_service */
#define FELUCCA_ID "FM-1_942"             /* tools/build.py --release 1.3.0: FM-1_94 + the minor digit */
#define FM1CORE_DG_OTA_C "../../cores/bytemania/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
