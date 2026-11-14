/* SPDX-License-Identifier: GPL-3.0-only */
/* The SID-1 core: SID-1 (https://github.com/putchdev/sid-1, the pinned, unmodified submodule cores/sid-1), two
 * Commodore 64 SIDs (sid.c, a C port of reSID 0.16 by Dag Lem, GPL-2.0-or-later) as a synth on Dinghy's platform.c
 * over Felucca's, built as its host mode (SID1_HOST). platform.c then includes "../../tests/host.c", a program
 * (script, WAV, main()): core_sid1.cmake maps that path to sid1_host.c (clang's -ivfsoverlay), whose platform_poll is
 * the coroutine's yield. sid.c is its own unit in the firmware's build; here it follows sid1.c in this one (its
 * statics meet none of sid1.c's: run_host.sh's two units, one). The family glue (dinghy_pre.h, dinghy_glue.h) runs
 * the main loop:
 *
 *   the input      into the shim's fm1_in, host_edges (presses) and host_enc[]; the keys are switches
 *   the flash      sid1.c's host store (host_flash: the device's bytes from STORE_AT 0xA7000 to KIT_AT + KIT_LEN
 *                  0xB0000: the library, the parts in its last sector, the sample kit) copied from the NOR image
 *                  before the boot and back, sector by sector as the device writes them, after each pass. SAVE and
 *                  the page's "keep" (CC 4E) write it at once, as on the device; unkept edits stay in RAM
 *   the page       its protocol is CCs on channel 16 (docs/MIDI.md), answered by the app itself (midi_send)
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them (the
 *                  M-UPGRADE identity FM-1_960: tools/build.py --release 1.0.1)
 *   the LEDs       fm1_led_key per id (0..13 buttons, 14 + k keys), lit or off
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the TRS MIDI in, the rescue, the
 * boot guard (the device branch's). */
#define SID1_HOST 1
#include "dinghy_pre.h"

#include "sid1.c"
#include "sid.c"

#include "exit_end.h"

/* the device's range of the store (platform.c FL_RANGE_OK: STORE_AT .. KIT_AT + KIT_LEN), sector-aligned */
_Static_assert(sizeof host_flash == 0x9000u && STORE_AT == 0x000A7000u, "SID-1's store: 0xA7000 .. 0xB0000");
#define FM1CORE_SID1_SECTORS (sizeof host_flash / FM1NOR_SECTOR)

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq zeroes the half), then MASTER and the clip */
static void fm1core_sid1_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_sid1_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    (void)up;                             /* (host.c's fm1_input_edges reports presses only; sid1.c reads no more) */
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    for (i = 0; i < 7u; i++)
        host_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return id < 64u && (fm1core_sid1_led >> id & 1u) ? FM1P_LED_LIT : FM1P_LED_OFF;
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
static void fm1core_dg_stores_load(void)
{
    memcpy(host_flash, fm1nor_image + STORE_AT, sizeof host_flash);
}
static void fm1core_dg_stores_poll(void)
{
    uint32_t s;
    for (s = 0; s < FM1CORE_SID1_SECTORS; s++)
        fm1core_dg_raw_sync(STORE_AT + s * FM1NOR_SECTOR, host_flash + s * FM1NOR_SECTOR);
}
static int fm1core_dg_sync(void)
{
    return 1;                             /* (no deferred saves: SAVE and CC 4E write at once; the rest is RAM) */
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    static const char *const SETUP[] = {"single", "layer", "split", "mixed"};
    const uint32_t k = setup_kind();
    snprintf(buf, n, "page %u, slot %u (%.11s)%s, %s, oct %d", (unsigned)page, (unsigned)cur_slot, cur.name,
             edited ? ", edited" : "", midi_mode == MODE_VOICES ? "voices" : k < 4u ? SETUP[k] : "?", (int)ui_oct);
}

/* the M-UPGRADE identity: platform.c's platform_poll gives every SysEx frame to ota_service */
#define FELUCCA_ID "FM-1_960"             /* tools/build.py --release 1.0.1: FM-1_96 + the minor digit */
#define FM1CORE_DG_OTA_C "../../cores/sid-1/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
