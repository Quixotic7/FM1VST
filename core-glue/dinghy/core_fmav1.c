/* SPDX-License-Identifier: GPL-3.0-only */
/* The FMav-1 core: FMav-1 (https://github.com/wootr0p/fmav1, the pinned, unmodified submodule cores/fmav1), wootrop &
 * gpix's semi-modular mono synth, built on OberVave 3.3.2 (zvenson) and so on Dinghy's platform. Like OberVave's core
 * (core_obervave.c, the model) it runs on its own fake FM-1, tests/host.c, as OberVave's browser simulator builds it:
 * OBV_HOST + OBV_WASM (host.c's panel statics, RAM stores and MIDI queue; no script, no main; the glue supplies
 * platform_poll). FMav-1 has no browser simulator of its own; host.c is OberVave 3.3.2's with FMav-1's changes (the
 * backlight layer, three program sectors, the MIDI queue's cable byte). The family glue (dinghy_pre.h, dinghy_glue.h)
 * runs the main loop as a coroutine. The scaffold's template (HAWK CHUA's Felucca host build) does not fit this tree.
 *
 * host.c logs its saves and its MIDI out with printf ("settings saved (n B)", "programs i saved", "MIDI out st d1 d2"):
 * here it goes to fm1core_fma_printf, which takes those lines as the events they report: a save copies the RAM record
 * into the NOR image in platform.c's sector format at the device's offsets (settings 0xFC000, FMav-1's three program
 * sectors 0xD8000..: platform.c PSECT_AT, past OberVave's store), a MIDI out line becomes a USB-MIDI packet. Before the
 * boot the image's records are read back into host.c's arrays. FMav-1 writes no pattern blobs or slots (platform.c
 * FL_RANGE_OK leaves OberVave's out). The LEDs: lit, the glow (DIM) and FMav-1's backlight layer (fm1_led_key_bg,
 * GLOBAL > LIGHTS: FAINT while its pulse is on).
 * The .pool section attribute Mach-O rejects: core_fmav1.cmake's FLAGS. */
#define OBV_HOST 1
#define OBV_WASM 1
#include "dinghy_pre.h"

static int fm1core_fma_printf(const char *fmt, ...);
#define printf(...) fm1core_fma_printf(__VA_ARGS__)

#include "fmav1.c"

#undef printf
#include "exit_end.h"

/* platform.c's non-SMK layout (its device branch: not compiled here) */
#define FM1CORE_FMA_SETTINGS_AT 0x000FC000u
#define FM1CORE_FMA_PSECT_AT 0x000D8000u

/* the poll (host.c declares it under OBV_WASM: sim.c's, a millisecond of the page's clock) */
static void platform_poll(void) { fm1core_dg_poll(); }

/* (0.6) the ISR as platform.c's fm1_alnk0_irq: app_audio, then the FM-1's own voice where READER > OUTPUT sends it
 * (app_audio_usb: MAIN, with the music; app_audio_out: HEADPHONES; USB: USB audio only, so not here). Under OBV_HOST
 * the voice is mixed at full MASTER (VO_MASTER): the glue's MASTER and the clip come after, over both */
static void fm1core_fma_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    app_audio(o, frames);
    (void)app_audio_usb(o, frames);
    app_audio_out(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_fma_render((o), (frames), (q12))

/* (0.6) the screen reader's SysEx out (announce.c: host.c's is in its script branch, not OBV_WASM's): never waits,
 * not sent at all while the MIDI out queue has no room for it, as platform.c's ann_wire_send */
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
    if (FM1CORE_DG_MQ - (fm1core_dg_mo_w - fm1core_dg_mo_r) < (n + 2u) / 3u)
        return -1;
    fm1core_dg_sysex_out(p, n);
    return 0;
}

static uint8_t *fm1core_fma_rec(uint32_t kind, uint32_t i, uint32_t *at)   /* 0 settings, 2 program sector */
{
    if (kind == 0) {
        *at = FM1CORE_FMA_SETTINGS_AT;
        return host_store;
    }
    if (kind == 2 && i < NPSECT) {
        *at = FM1CORE_FMA_PSECT_AT + i * 0x1000u;
        return host_psect[i];
    }
    return NULL;
}
static void fm1core_fma_saved(uint32_t kind, uint32_t i, uint32_t n)
{
    uint32_t at = 0;
    uint8_t *r = fm1core_fma_rec(kind, i, &at);
    if (r && r[0] == 1u && n <= SETTINGS_MAX)
        fm1core_dg_sector_save(at, r + 4, n);
}
static int fm1core_fma_printf(const char *fmt, ...)
{
    va_list a;
    unsigned ms, x, y, z;
    if (!strstr(fmt, " ms  "))
        return 0;                         /* (only host.c's event lines: "%6u ms  ...") */
    va_start(a, fmt);
    ms = va_arg(a, unsigned);
    (void)ms;
    if (strstr(fmt, "MIDI out")) {
        x = va_arg(a, unsigned);
        y = va_arg(a, unsigned);
        z = va_arg(a, unsigned);
        fm1core_dg_midi_out(x, y, z);
    } else if (strstr(fmt, "settings saved")) {
        x = va_arg(a, unsigned);
        fm1core_fma_saved(0, 0, x);
    } else if (strstr(fmt, "programs %u saved")) {
        x = va_arg(a, unsigned);
        y = va_arg(a, unsigned);
        fm1core_fma_saved(2, x, y);
    }
    va_end(a);
    return 0;
}

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;                   /* (fm1_input_edges: taken by the app's pass) */
    host_rel |= up;
    for (i = 0; i < FM1P_NENC; i++) {
        host_enc[i] += enc[i];
        host_enc_pos[i] += enc[i];
    }
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return host_leds >> id & 1u ? FM1P_LED_LIT : host_glow >> id & 1u ? FM1P_LED_DIM
         : host_bg_ns && host_bg >> id & 1u ? FM1P_LED_FAINT : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)   /* OberVave's sim.c sim_midi (cable 0: USB) */
{
    if (st == 0xFEu)
        return 1;
    if (mq_w - mq_r >= 64u)
        return 0;
    mq[mq_w % 64u][0] = st;
    mq[mq_w % 64u][1] = d1;
    mq[mq_w % 64u][2] = d2;
    mq[mq_w % 64u][3] = 0;
    mq_w++;
    return 1;
}
static void fm1core_dg_stores_load(void)
{
    uint32_t k, i, at = 0, n = 0;
    for (k = 0; k < 3u; k += 2u)
        for (i = 0; i < (k == 2 ? NPSECT : 1u); i++) {
            uint8_t *r = fm1core_fma_rec(k, i, &at);
            memset(r, 0, SETTINGS_MAX + 4u);
            if (!fm1core_dg_sector_read(at, r + 4, SETTINGS_MAX, &n))
                r[0] = 1;
        }
}
static void fm1core_dg_stores_poll(void) {}   /* (each save is copied when host.c reports it) */
static int fm1core_dg_sync(void)
{
    uint32_t i;
    /* fmav1.c saves(): the program sectors (one a pass, when the engine is quiet) and the settings (2 s after the
     * change), now */
    for (i = 0; i < NPSECT; i++)
        if (psect_dirty >> i & 1u) {
            psect[i].tag = PS_TAG;
            psect_save(i, &psect[i], sizeof psect[i]);
        }
    psect_dirty = 0;
    if (set_dirty)
        settings_save(&set, sizeof set);
    set_dirty = 0;
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "section %u, track %u, program %u", (unsigned)sec, (unsigned)ctrk + 1u,
             (unsigned)set.tprog[ctrk] + 1u);
}

/* the installer's M-UPGRADE identity: the device's platform_poll runs ota_service every pass (dinghy_glue.h), after the
 * screen reader (fm1-reader: F0 7D 53 52 .., announce.c) has looked at the frame */
#define FELUCCA_ID "FM-1_3628"            /* FMav-1's one package identity, every version (platform.c, tools/build.py) */
#define FM1CORE_DG_OTA_C "../../cores/fmav1/firmware/src/ota.c"
static void fm1core_dg_ota_frame(const uint8_t *p, uint32_t n);
static void fm1core_fma_sx_in(const uint8_t *p, uint32_t n)   /* (0.6) a screen reader's frame first (platform_poll) */
{
    if (!ann_frame(p, n, fm1_ms))
        fm1core_dg_ota_frame(p, n);
}
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_fma_sx_in((p), (n))

#include "dinghy_glue.h"
