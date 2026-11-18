/* SPDX-License-Identifier: GPL-3.0-only */
/* The M-Vsynth2 core: M-Vsynth2 by Kris (bk40-home) (https://github.com/bk40-home/M-Vsynth2, the pinned, unmodified
 * submodule cores/M-Vsynth2), Synth2 (the open-source recreation of Synth1) on Dinghy's platform.c over Felucca's,
 * built as its host mode (DINGHY_HOST). platform.c then includes "../../tests/host.c", a program (script, WAV, main()):
 * core_mvsynth2.cmake maps that path to mvsynth2_host.c (clang's -ivfsoverlay), whose platform_poll is the coroutine's
 * yield. The model: CULTBOX's core (core_cultbox.c, the same Dinghy host file); the scaffold's felucca_web template
 * does not fit this tree. The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the main loop  mvsynth2.c's own app_main (the boot, then platform_poll and the pass for ever)
 *   the engine     vs_dsp.c, its own unit as on the device (tools/build.py: -O2 apart), EXTRA_SOURCES; one core (the
 *                  host mode: vs_dsp.c leaves the second core out, every voice renders on the one)
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (host_enc); the keys are
 *                  switches
 *   the flash      store.h's FM-1 layout over the NOR image (mvsynth2_host.c), under M-Vsynth2's STORE_MAGIC "MVS2":
 *                  the settings at 0xFC000, the program banks in the slots from 0x97000 (vs_prog.c). The main loop
 *                  saves when nothing has sounded for 0.5 s (the settings 2 s after a change); flash_sync does it at
 *                  once (fm1core_dg_sync below)
 *   SysEx          in: a frame whose first byte is APP_SYSEX_ID (7D: the patch transfer of the library page,
 *                  vs_xfer.c) goes to app_sysex, the rest to ota.c's ota_service, as the device's platform_poll hands
 *                  them: the M-UPGRADE identity FM-1_9681003 (tools/build.py --release 1.3: "FM-1_968100" + the minor);
 *                  out: their replies (sysex_send, ota_wire_send)
 *   the LEDs       host_leds per id (0..13 buttons, 14 + k keys): lit; host_glow (fm1_led_key_glow): dim
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio (uac_out.c), the TRS jack, the
 * rescue, the boot guard, the second core (the device branch's). */
#include "dinghy_pre.h"

#include "mvsynth2.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq hands app_audio a zeroed half), then MASTER and the
 * clip */
static void fm1core_mv_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_mv_render((o), (frames), (q12))

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
/* the stores are the NOR image itself (mvsynth2_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred saves (mvsynth2.c app_main: the bank being edited, the settings), now. Its own wait
     * for quiet (an erase stalls the device's sound) is skipped: flash_sync asks for them at once */
    if (prog.dirty && vs_prog_flush(&prog))
        return 0;                         /* (a failed write: tried again at the next sync) */
    if (set_dirty) {
        set_dirty = 0;
        set_t = fm1_ms;
        if (settings_save(&set, sizeof set))
            return 0;
    }
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "screen %u, bank %u, program %u%s, voices %u", (unsigned)scr, (unsigned)prog.bank,
             (unsigned)slot + 1u, edited ? "*" : "", (unsigned)eng.act);
}

/* the M-UPGRADE identity and the patch transfer: platform.c's platform_poll gives a frame starting with APP_SYSEX_ID
 * to the app, the rest to ota_service */
#define FELUCCA_ID "FM-1_9681003"         /* tools/build.py --release 1.3 */
#define FM1CORE_DG_OTA_C "../../cores/M-Vsynth2/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) \
    ((n) && (p)[0] == APP_SYSEX_ID ? APP_SYSEX((p), (n)) : fm1core_dg_ota_frame((p), (n)))

#include "dinghy_glue.h"
