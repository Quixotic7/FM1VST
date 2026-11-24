/* SPDX-License-Identifier: GPL-3.0-only */
/* The M-vance core: M-vance by Kris (bk40-home) (https://github.com/bk40-home/M-vance, the pinned, unmodified
 * submodule cores/M-vance), a JP-8000/8080-style virtual analog (his JT-8000 engine) on Dinghy's platform.c over
 * Felucca's, built as its host mode (DINGHY_HOST). platform.c then includes "../../tests/host.c", a program (script,
 * WAV, main()): core_mvance.cmake maps that path to mvance_host.c (clang's -ivfsoverlay), whose platform_poll is the
 * coroutine's yield. The model: M-Vsynth2's core (core_mvsynth2.c, the same author's Dinghy fork and host file); the
 * scaffold's paths were M-Vsynth2's. The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the main loop  mvance.c's own app_main (the boot, then platform_poll and the pass for ever)
 *   the engine     mv_dsp.c, its own unit as on the device (tools/build.py: -O2 apart), EXTRA_SOURCES; one core (the
 *                  host mode: mv_dsp.c leaves the second core out, every voice renders on the one)
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (host_enc); the keys are
 *                  switches
 *   the flash      store.h's FM-1 layout over the NOR image (mvance_host.c), under M-vance's STORE_MAGIC "MVNC": the
 *                  settings at 0xFC000, the programs in the blobs (mv_prog.c), the clips in slot 1 (mv_clip.c). The
 *                  main loop saves when nothing sounds (the settings 2 s after a change); flash_sync does it at once
 *                  (fm1core_dg_sync below)
 *   SysEx          in: a frame whose first byte is 41 (Roland's: the JP-8080 program transfer, mv_xfer.c) or 7E (the
 *                  Universal Device Inquiry) goes to app_sysex, the rest to ota.c's ota_service, as the device's
 *                  platform_poll hands them: the M-UPGRADE identity FM-1_968087 (tools/build.py --release 2.7.1:
 *                  "FM-1_96808" + the minor); out: their replies (sysex_send, ota_wire_send)
 *   the LEDs       host_leds per id (0..13 buttons, 14 + k keys): lit; host_glow (fm1_led_key_glow): dim
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio (uac_out.c), Bluetooth MIDI (fm1-kit's
 * fk_ble: FK_BLE 0), the TRS jack, the rescue, the boot guard, the second core (the device branch's). */
#include "dinghy_pre.h"

#define section(x) aligned(4)                   /* the .pool buffers (gfx.c's canvas, mvance.c's roll and snapshot):
                                                 * the device's linker's section, which Mach-O rejects */
#include "mvance.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq hands app_audio a zeroed half), then MASTER and the
 * clip */
static void fm1core_mva_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_mva_render((o), (frames), (q12))

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
/* the stores are the NOR image itself (mvance_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred saves (mvance.c app_main: the programs' blobs, a clip, the settings), now. Its own wait
     * for quiet (an erase stalls the device's sound) is skipped: flash_sync asks for them at once. The settings' dirty
     * flag is app_main's local, so they are written each sync (one sector) */
    uint32_t i;
    for (i = 0; i < 4u && mv_prog_dirty(&prog); i++)   /* (one record a call, as dock_release) */
        (void)mv_prog_flush(&prog);
    if (mv_prog_dirty(&prog))
        return 0;                         /* (a failed write: tried again at the next sync) */
#if MV_SEQ
    if (mv_clip_dirty(&clips)) {
        (void)mv_clip_flush(&clips);
        if (mv_clip_dirty(&clips))
            return 0;
    }
#endif
    return settings_save(&set, sizeof set) == 0;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u, program %u%s", (unsigned)ui.page, (unsigned)set.prog + 1u,
             mv_prog_dirty(&prog) ? "*" : "");
}

/* the M-UPGRADE identity and the program transfer: platform.c's platform_poll gives a frame starting with 41 or 7E to
 * the app, the rest to ota_service */
#define FELUCCA_ID "FM-1_968087"          /* tools/build.py --release 2.7.1 */
#define FM1CORE_DG_OTA_C "../../cores/M-vance/firmware/src/ota.c"
#if MV_XFER
#define FM1CORE_DG_SYSEX_IN(p, n) \
    ((n) && ((p)[0] == 0x41u || (p)[0] == 0x7Eu) ? APP_SYSEX((p), (n)) : fm1core_dg_ota_frame((p), (n)))
#else
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))
#endif

/* M-vance (the kit screens, MV_UI_KIT 1): the four cells as mv_kui_draw builds them, kui_knob (mv_kui.c, pure: it fills a kit_knob_t)
 * with HOME_LABEL on HOME, the column's own label elsewhere; kit_cell draws k->lab and k->val as they are. A P_NONE column is empty.
 * Nothing: the factory-bank question, a USB transfer, the piano roll (SEQ 2/2: no cells), DEBUG (its values are made inline). */
#include "rd_cols.h"
static void fm1core_mvance_rd(void)
{
    const ui_page_t *pg = &PAGES[ui.page];
    kit_knob_t kn;
    uint32_t k;
    rdc_begin();
    if (!KU.u || bank_ask)                      /* (KU.u: set by the first mv_kui_draw; kui_choice / kui_raw read it) */
        return;
#if MV_XFER
    if (xfer_slots && xfer_busy())
        return;
#endif
#if MV_SEQ
    if (!mv_ui_home(&ui) && pg->p[0] == P_SQBAR)
        return;
#endif
#if MV_DEBUG
    if (!mv_ui_home(&ui) && page_kind(pg) == G_DEBUG)
        return;
#endif
    for (k = 0; k < 4u; k++) {
        if (mv_ui_home(&ui))
            kui_knob(PAGES[0].p[k], HOME_LABEL[k], 0, &kn);
        else
            kui_knob(pg->p[k], 0, 0, &kn);
        rdc_col(k, kn.lab, kn.val);
    }
}
#define FM1CORE_DG_AFTER_FRAME fm1core_mvance_rd
#include "dinghy_glue.h"
