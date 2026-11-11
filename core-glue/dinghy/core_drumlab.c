/* SPDX-License-Identifier: GPL-3.0-only */
/* The Drumlab core: Drumlab (https://github.com/pbarilla/drumlab, the pinned, unmodified submodule cores/drumlab), a
 * drum machine that works like the PO-32 Tonic (Microtonic's drum synth: 16 sounds, 16 patterns, the LAB, kits) and
 * sends to one, on Dinghy's platform.c over Felucca's, built from its own entry point firmware/src/lab.c in its host
 * mode (CHIPBOX_HOST, the macro Drumlab kept from Chipbox, as its web/build.sh does). platform.c then includes
 * "../../tests/host.c", a program (script, WAV, main()): core_drumlab.cmake maps that path to drumlab_host.c (clang's
 * -ivfsoverlay), whose platform_poll is the coroutine's yield. The family glue (dinghy_pre.h, dinghy_glue.h) runs the
 * main loop:
 *
 *   the boot       the power-on screen as the device shows it (the atom, then the name: LAB_BOOT, lab.c's own switch
 *                  for its host sim), then on a fresh flash the quick start, locked (HOME puts it away)
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (enc_acc); the keys are
 *                  switches
 *   the flash      the device's own layout in the NOR image (drumlab_host.c): the 12 projects' two banks each at
 *                  PROJ_AT 0x97000 (labproj.c), the settings sector at 0xFC000 (DNGY record)
 *   MIDI in        the channel messages into the shim's ring (midi_get); clock, start, continue and stop also counted
 *                  as usb.c does on the device (LAB_CLK: mi_clk ..), so MIDI CLOCK FOLLOW works
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them: the
 *                  M-UPGRADE identity (FM-1_032, tools/build.py PRODUCT) and, with OTA_PROJ_IO as on the device, the
 *                  website's Projects page's commands 0x50..0x52 (labio.c, its device half on, and the main loop's
 *                  open of a project that came in: both overlaid, core_drumlab.cmake); out: their replies, the
 *                  status CCs (midi_send) and the MIDI clock out (midi_isr_send)
 *   the LEDs       host_led per id (0..13 buttons, 14 + k keys): on = lit, glow = dim
 * MASTER is the glue's (the device's curve, after app_audio). Not here: the Bluetooth radio (firmware/ble/, its own
 * unit on the device: Brothers Link, BLE MIDI; lab.c's own stubs stand in, as in its host sim: Bluetooth MIDI is off),
 * USB audio, the battery reading (labui.c's host value), the rescue, the boot guard (the device branch's). The PO-32
 * transfer (SEND view) is plain audio out of app_audio, so it is here. */
#define CHIPBOX_HOST 1
#define LAB_BOOT 1                        /* (lab.c: the power-on picture in the host sim too) */
#define OTA_PROJ_IO 1                     /* (lab.c sets it for the device build only) */
#include "dinghy_pre.h"

#include "lab.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (core_audio adds into it, as platform.c's fm1_alnk0_irq zeroes the half), then
 * MASTER and the clip */
static void fm1core_dl_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_dl_render((o), (frames), (q12))

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
    if (st == 0xF8u)                      /* usb.c (LAB_CLK): the clock and transport counted, and queued too */
        mi_clk++;
    else if (st == 0xFAu)
        mi_start++;
    else if (st == 0xFBu)
        mi_cont++;
    else if (st == 0xFCu)
        mi_stop++;
    mq[mq_w % 64u][0] = st;
    mq[mq_w % 64u][1] = d1;
    mq[mq_w % 64u][2] = d2;
    mq_w++;
    return 1;
}
/* the stores are the NOR image itself (drumlab_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* labui.c's deferred project save (3 s after an edit, stopped, not sending, no pad held), now: the open project
     * when it has unsaved edits (lab_save itself skips the demo in slot 1 and the Bluetooth demo; a lesson's edits
     * are never kept). While it plays or sends to a PO-32 the device waits: so does this. The settings are written
     * when they change (settings_save), as on the device. */
    if (!lab_dirty || les_on)
        return 1;
    if (sq_run || sending)
        return 0;
    if (lab_save(lab_slot) == 0) {
        lab_dirty = 0;
        return 1;
    }
    return 0;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    static const char *const VIEWS[] = {"PATTERN", "LAB", "SEND", "?"};
    snprintf(buf, n, "project %u, %s view, pattern %u, sound %u, %u bpm%s%s%s", (unsigned)lab_slot + 1u,
             VIEWS[view < 3u ? view : 3u], (unsigned)cur_pat + 1u, (unsigned)sel + 1u, (unsigned)bpm,
             sq_run ? ", playing" : "", sending ? ", sending" : "", lab_dirty ? ", unsaved" : "");
}

/* the M-UPGRADE identity and the projects' SysEx: platform.c's platform_poll gives every frame to ota_service */
#define FELUCCA_ID "FM-1_032"             /* tools/build.py PRODUCT (fixed across versions) */
#define FM1CORE_DG_OTA_C "../../cores/drumlab/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
