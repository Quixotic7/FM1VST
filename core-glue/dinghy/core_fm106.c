/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-106 core: FM-106 (https://github.com/PatttF/fm-106, the pinned, unmodified submodule cores/fm-106) on this
 * glue's own fake FM-1, fm106_host.c: platform.c includes "../../tests/host.c" (FM-106's, broken at Beta4), which
 * core_fm106.cmake maps to fm106_host.c with clang's -ivfsoverlay. The family glue (dinghy_pre.h, dinghy_glue.h)
 * runs the main loop as a coroutine.
 *
 *   the second core  FM-106's Melodee-style CPU1 worker (audio_worker.h) is the device branch's (DINGHY_DUAL_CORE):
 *                    here audio_worker_online is 0 and fm106_engine.h renders every voice inline
 *   the flash        fm106_host.c keeps the device's formats on the NOR image itself: the settings record (DNGY
 *                    sector, 0xFC000) and the user presets (8 KiB at 0x98000)
 *   SysEx            in: the librarian's messages (41 ..) reach fm106_sysex_handle before the next pass, as the
 *                    device's platform_poll hands them over; out: its replies as USB-MIDI SysEx packets
 *   the backlight    fm1_led_bg (the buttons' dim plane) as FM1P_LED_FAINT */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "fm106.c"

#include "exit_end.h"

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_released |= up;
    for (i = 0; i < FM1P_NENC; i++)
        host_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id) { return host_led[id] ? FM1P_LED_LIT : FM1P_LED_OFF; }
#define FM1CORE_DG_LED_BG(col) (fm1_led_bg_ns ? fm1_led_bg[col] : 0u)
#define FM1CORE_DG_SYSEX_IN(p, n) fm106_sysex_handle((p), (n))
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
static void fm1core_dg_stores_load(void) {}   /* (fm106_host.c reads the image itself) */
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void) { return 1; }   /* (every save is written when the app makes it) */
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "patch %u, page %u", (unsigned)g_synth.patch_idx, (unsigned)g_current_page);
}

#include "dinghy_glue.h"
