/* SPDX-License-Identifier: GPL-3.0-only */
/* The Chipbox core: Chipbox (https://github.com/pbarilla/chipbox, the pinned, unmodified submodule cores/chipbox),
 * sound chips as channels (NES, Game Boy, SMS, PC Engine, OPL, SID, ...) in a step rack, on Dinghy's platform.c over
 * Felucca's, built as its host mode (CHIPBOX_HOST). platform.c then includes "../../tests/host.c", a program
 * (script, WAV, main()): core_chipbox.cmake maps that path to chipbox_host.c (clang's -ivfsoverlay), whose
 * platform_poll is the coroutine's yield. The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the boot       the power-on screen as the device shows it, the next of the 14 each boot (the settings remember
 *                  the last): core_chipbox.cmake turns the device's condition on in a build-tree copy of chipbox.c
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (enc_acc); the keys are
 *                  switches
 *   the flash      the device's own layout in the NOR image (chipbox_host.c): the 12 projects' two banks each at
 *                  PROJ_AT 0x97000 (proj.c), the settings sector at 0xFC000 (DNGY record)
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them: the
 *                  M-UPGRADE identity (FM-1_337, tools/build.py PRODUCT) and, with OTA_PROJ_IO as on the device, the
 *                  website's Projects page's commands 0x50..0x52 (projio.c, its device half on, and the main loop's
 *                  open of a project that came in: both overlaid, core_chipbox.cmake); out: their replies, the
 *                  status CCs (midi_send)
 *   the LEDs       host_led per id (0..13 buttons, 14 + k keys): on = lit, glow = dim
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the rescue, the boot guard (the
 * device branch's). */
#define CHIPBOX_HOST 1
#define OTA_PROJ_IO 1                     /* (chipbox.c sets it for the device build only) */
#include "dinghy_pre.h"

#include "chipbox.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (chip_render adds into it, as platform.c's fm1_alnk0_irq zeroes the half), then
 * MASTER and the clip */
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
        enc_acc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    if (id >= 64u)
        return FM1P_LED_OFF;
    return host_led[id] == 2 ? FM1P_LED_LIT : host_led[id] == 1 ? FM1P_LED_DIM : FM1P_LED_OFF;
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
/* the stores are the NOR image itself (chipbox_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred project save (2 s after an edit, stopped and silent), now: the open project when it
     * has unsaved edits (the demos, slots 1 and 2, are never saved). While it plays the device waits: so does this.
     * The settings (octave, the open project, the boot screen) are app_main's local: the loop saves them 2 s after
     * a change, quiet, as on the device. */
    if (!proj_dirty || proj_cur < PROJ_DEMOS)
        return 1;
    if (seq_run)
        return 0;
    if (proj_save(proj_cur) == 0) {
        proj_dirty = 0;
        proj_t_saved = fm1_ms | 1u;
        return 1;
    }
    return 0;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "project %u, %u channels, page %u, %u bpm%s%s", (unsigned)proj_cur + 1u, (unsigned)chan_n,
             (unsigned)seq_pat + 1u, (unsigned)seq_bpm, seq_run ? ", playing" : "", proj_dirty ? ", unsaved" : "");
}

/* the M-UPGRADE identity and the projects' SysEx: platform.c's platform_poll gives every frame to ota_service */
#define FELUCCA_ID "FM-1_337"             /* tools/build.py PRODUCT (fixed across versions) */
#define FM1CORE_DG_OTA_C "../../cores/chipbox/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
