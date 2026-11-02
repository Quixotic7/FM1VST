/* SPDX-License-Identifier: GPL-3.0-only */
/* The sid core: FM-1 SID (https://github.com/joakimeriksson/fm1-sid, the pinned, unmodified submodule cores/fm1-sid;
 * Joakim Eriksson's C64 SID player, GoatTracker 2 player and editor and SID synth) as its Dinghy build has it:
 * firmware/fm1sid.c on its own copy of Dinghy 0.3 (dinghy/src/platform.c), in the host branch (DINGHY_HOST), whose
 * fake FM-1 is this glue's sid_host.c (core_sid.cmake overlays it where platform.c includes "../../tests/host.c").
 * reSID 0.16 (Dag Lem) is fm1-dock's copy (cores/fm1-dock/modules/sid/resid: fm1-sid's tools/fetch_ext.sh fetches the
 * same tarball and makes the same change to it), the other units (the 6510 and PSID player, GoatTracker's
 * playroutine and editor, the own SID model and its synth driver, firmware/resid_glue.cc) its own, compiled as its
 * build_dinghy.sh compiles them. The family glue (dinghy_pre.h, dinghy_glue.h) runs app_main as a coroutine.
 *
 *   the tunes        GoatTracker 2's ten example songs, Dojo first, as FM-1 SID 0.3's release (dock/build_solo.sh):
 *                    the pack (sid_pack.h) at _pack, where the device's image ends
 *   the song store   the editor's saves (firmware/store.c: whole-sector records, 16 slots) in the image's flash from
 *                    FM1CORE_SID_STORE_AT to store.c's ST_END (0x92000), the app slot's end. On the device the store
 *                    starts after the image and its pack (st_init from the XIP address where the pack ends: not a
 *                    flash offset here), so the first pass of the main loop after app_init places it and reads it
 *                    (st_scan, list_store), before the app's first step
 *   the rescue       HOME held 5 s (the USB rescue) and OCT- + OCT+ held 3 s (UBOOT) stop the sound and show what
 *                    the device would be doing; HOME goes back to the app (the device: a restart)
 *   SysEx            every frame to Dinghy's ota.c (the M-UPGRADE identity, FM-1_900, build_dinghy.sh's FELUCCA_ID
 *                    and the release's); fm1-dock's file frames ("Play on my FM-1") are its module build's only
 *   MASTER           the device's (dinghy_glue.h: platform.c's curve, after app_audio) */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "fm1sid.c"

#undef ms                                  /* (fm1sid.c's fm1_ms: the glue's own parameter names) */
#include "exit_end.h"

#define FM1CORE_SID_STORE_AT 0x00060000u   /* the song store: 200 KiB, to store.c's ST_END */

/* ---- the poll: the coroutine's yield; once app_init has run, the song store in place */
static void platform_poll(void)
{
    static uint8_t placed;
    if (!placed && npack) {                /* (load_pack has run: app_init's st_init is done) */
        placed = 1;
        st_lo = FM1CORE_SID_STORE_AT;
        st_hi = ST_END;
        st_scan();
        list_store();
    }
    fm1core_dg_poll();
}

/* ---- the rescue / UBOOT: the sound off, the screen says it, HOME (pressed again) goes back */
static void fm1core_sid_park(const char *what)
{
    uint32_t r = 0;
    fm1_audio_stop();
    fill(0, 0, 240, 240, C_DARK);
    text(120 - 4 * (int)strlen(what), 104, what, C_WHITE);
    text(120 - 4 * 18, 124, "HOME: back to SID", C_GREY);
    lcd_frame();
    fm1_input_edges(0);
    while (!(fm1_input_edges(&r) & (1u << 8)))
        platform_poll();
    home_t = 0;
    hold = 0;
    shown_ok = 0;                          /* (the next frame redraws all of it) */
    fm1core_sid_audio_off = 0;
}
static void recovery_main(void) { fm1core_sid_park("USB MODE"); }
static void fm1_enter_uboot(void) { fm1core_sid_park("UBOOT (USB)"); }

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    host_note_taps |= notes & ~fm1_in.notes;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_released |= up;
    for (i = 0; i < FM1P_NENC; i++)
        host_enc[i] += enc[i];
}
/* LED id -> the app's column words (fm1_led lit, fm1_led_dim the glow), through the keymap */
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return fm1_led[c] >> r & 1u ? FM1P_LED_LIT : fm1_led_dim[c] >> r & 1u ? FM1P_LED_DIM : FM1P_LED_OFF;
    return FM1P_LED_OFF;
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
static void fm1core_dg_stores_load(void) {}   /* (store.c reads the image itself) */
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void) { return 1; }   /* (a song is written when the editor saves it) */
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%s, tune %u/%u, %s", trk ? "editor" : mode ? "synth" : "player", (unsigned)play_idx + 1u,
             (unsigned)ntunes, engine ? "own model" : "reSID");
}

/* the ISR's block (dinghy_glue.h's own output stage, with the rescue's silence) */
#define FM1CORE_DG_RENDER(o, frames, q12)                                                                    \
    do {                                                                                                     \
        uint32_t i_;                                                                                         \
        memset((o), 0, (size_t)(frames) * 2u * sizeof *(o));                                                \
        if (!fm1core_sid_audio_off)                                                                          \
            app_audio((o), (frames));                                                                        \
        for (i_ = 0; i_ < 2u * (frames); i_++) {                                                             \
            const int32_t v_ = (int32_t)(((int64_t)(o)[i_] * (q12)) >> 12);                                  \
            (o)[i_] = v_ > 32767 ? 32767 : v_ < -32767 ? -32767 : v_;                                        \
        }                                                                                                    \
    } while (0)

/* the installer's M-UPGRADE identity: the device's platform_poll runs ota_service every pass (dinghy_glue.h) */
#define FELUCCA_ID "FM-1_900"             /* firmware/build_dinghy.sh, and the 0.3 release's package (identity FM-1_900) */
#define FM1CORE_DG_OTA_C "../../cores/fm1-sid/dinghy/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#define fb fm1core_sid_lcd                 /* (the panel's frame buffer: fm1sid.c's fb is its 4-bit picture) */
#include "dinghy_glue.h"
