/* SPDX-License-Identifier: GPL-3.0-only */
/* The CONDUCTOR core: CONDUCTOR (https://github.com/MammothCheese/conductor, the pinned, unmodified submodule
 * cores/conductor), an orchestral groovebox (strings, woodwinds, brass, percussion; patterns, the Composer, the
 * score) on Dinghy's platform.c over Felucca's, built as its host mode (DINGHY_HOST). platform.c then includes
 * "../../tests/host.c", a program (script, WAV, main()): core_conductor.cmake maps that path to conductor_host.c
 * (clang's -ivfsoverlay), whose platform_poll is the coroutine's yield. The family glue (dinghy_pre.h,
 * dinghy_glue.h) runs the main loop:
 *
 *   the input      into the shim's fm1_in, edges (host_edges / host_rel) and encoder steps (host_enc); the keys are
 *                  switches
 *   the flash      the device's own layout in the NOR image (conductor_host.c): the eight score slots (four sectors
 *                  each, "SLOT" ^ "CNDR") from 0x97000; SAVE writes at once, the work in progress (WORK_SLOT) 2 s
 *                  after a change when stopped and quiet, as on the device
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them: the
 *                  M-UPGRADE identity FM-1_1812 (tools/build.py: every build); out: its replies
 *   the LEDs       host_leds per id (0..13 buttons, 14 + k keys) lit, host_glow dim
 * MASTER is the glue's (the device's curve, after app_audio, the block zeroed first). Not here: USB audio, the TRS
 * MIDI in, the battery, the rescue, the boot guard (the device branch's). */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "conductor.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq hands app_audio a zeroed half), then MASTER and the
 * clip */
static void fm1core_cnd_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_cnd_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_rel |= up;
    for (i = 0; i < HOST_NENC; i++)
        host_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return id >= 64u ? FM1P_LED_OFF : host_leds >> id & 1u ? FM1P_LED_LIT : host_glow >> id & 1u ? FM1P_LED_DIM : FM1P_LED_OFF;
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
/* the stores are the NOR image itself (conductor_host.c): nothing to copy */
static void fm1core_dg_stores_load(void) {}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    return 1;                             /* (the work's deferred save is app_main's local state: not reachable; SAVE
                                             writes at once) */
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "tempo %u, %s", (unsigned)sc.tempo, running ? "playing" : "stopped");
}

/* the M-UPGRADE identity: platform.c's platform_poll gives every frame to ota_service */
#define FELUCCA_ID "FM-1_1812"            /* tools/build.py PRODUCT: FM-1_1812, every build */
#define FM1CORE_DG_OTA_C "../../cores/conductor/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
