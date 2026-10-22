/* SPDX-License-Identifier: GPL-3.0-only */
/* The GR!MM core: GR!MM (https://github.com/GRiMMBIENT/grimm, the pinned, unmodified submodule cores/grimm), a Dinghy
 * app, on its browser shim web/fm1_web.c (GRIMM_WEB: platform.c includes it in place of the hardware), built natively.
 * The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop as a coroutine; what the shim does its own way:
 *
 *   the yield     fm1_web.c's platform_poll is one device ms (fm1_ms++), then yields to the page (web_yield) once
 *                 its audio ring holds web_want frames: web_want is 0 here, so every poll yields (one pass a tick), and
 *                 web_yield is the coroutine's (fm1core_dg_poll). Its two imports are defined below (their wasm
 *                 import attributes neutralised while the firmware is compiled)
 *   the audio     the shim's own rendering in platform_poll is kept out (web_owed reset each tick): the glue's ISR
 *                 renders app_audio's 256-frame blocks with the device's MASTER curve, as for every Dinghy core
 *   the input     into the shim's fm1_in, edges (web_pressed / web_released / web_notes_pressed) and encoder steps
 *   MIDI / glow   the shim drops MIDI out and the keys' glow: grimm_hook/analog_tables.h points midi_send and
 *                 fm1_led_glow at the glue
 *   the flash     the shim's RAM stores, copied into the NOR image at the device's offsets in the device's formats
 *                 (platform.c): the settings (a DNGY record at 0xFC000), the six save sectors (store.c: raw, 0xDF000,
 *                 0xE5000..0xE7000, 0xFD000, 0xFE000), the sample area (samples.c: raw, 0x97000..0xDEFFF; at boot
 *                 the firmware writes its four factory samples into an erased one, as on a new unit); read back
 *                 before the boot
 *   SysEx         the device's split (platform.c platform_poll): a frame F0 7D .. is the app's (editor.c and
 *                 samples.c: F0 7D 47 52, the editor page and the sample upload), one at a time, dropped after 1 s
 *                 if the app has not taken it; any other frame goes to ota.c (the M-UPGRADE identity FM-1_966).
 *                 The app's replies (sysex_send: whole F0 .. F7 messages in the shim's buffer) leave as USB-MIDI
 *                 SysEx packets after each pass, as many as the MIDI out queue has room for
 * Not here: SCREEN OFF's backlight (the shim keeps the screen lit), USB audio, the rescue, the boot guard (the shim
 * has neither). The .pool section attribute Mach-O rejects: core_grimm.cmake's FLAGS. */
#define GRIMM_WEB 1
#include "dinghy_pre.h"

#define import_module(x) unused           /* (fm1_web.c's wasm imports: plain declarations here, defined below) */
#define import_name(x) unused
#include "grimm.c"
#undef import_module
#undef import_name
#include "exit_end.h"

/* platform.c's device layout (its device branch: not compiled here) */
#define FM1CORE_GRM_SETTINGS_AT 0x000FC000u
#define FM1CORE_GRM_SAMPLE_AREA 0x00097000u
static const uint32_t FM1CORE_GRM_SAVE_SECT[SAVE_NSECT] = {0xDF000u, 0xE5000u, 0xE6000u, 0xE7000u, 0xFD000u, 0xFE000u};
_Static_assert(SAMPLE_AREA_LEN == 0x48000u, "GR!MM's sample area: platform.c's 288 KiB");

/* ---- the shim's imports */
uint32_t web_ticks(void) { return fm1t_ticks24(); }
void web_yield(void) { fm1core_dg_poll(); }

/* ---- MIDI out, the glow (grimm_hook/analog_tables.h) */
static void fm1core_grm_midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }
static uint64_t fm1core_grm_glow;          /* LED id -> the glow layer */
static void fm1core_grm_led_glow(uint32_t id, int on)
{
    if (id >= 64u)
        return;
    if (on)
        fm1core_grm_glow |= 1ull << id;
    else
        fm1core_grm_glow &= ~(1ull << id);
}

/* ---- SysEx (dinghy_glue.h FM1CORE_DG_SYSEX_IN / _READY) */
static void fm1core_dg_ota_frame(const uint8_t *p, uint32_t n);
static uint32_t fm1core_grm_sx_t0;
static int fm1core_grm_sx_ready(void)     /* the app's last frame taken (or left 1 s: platform.c drops it) */
{
    if (!web_sx_in_ready)
        return 1;
    if (fm1_ms - fm1core_grm_sx_t0 > 1000u) {
        web_sx_in_ready = 0;
        return 1;
    }
    return 0;
}
static void fm1core_grm_sx_in(const uint8_t *p, uint32_t n)
{
    if (n && p[0] == 0x7Du) {             /* the app's (sysex_get) */
        if (n > sizeof web_sx_in)
            return;                       /* (longer than a frame can be: dropped) */
        memcpy(web_sx_in, p, n);
        web_sysex_in(n);
        fm1core_grm_sx_t0 = fm1_ms;
    } else {
        fm1core_dg_ota_frame(p, n);       /* the updater's */
    }
}
static void fm1core_grm_sx_out(void)      /* sysex_send's messages into the MIDI out queue, while there is room */
{
    uint32_t at = 0;
    while (at < web_sx_out_n) {
        uint32_t e = at, len;
        while (e < web_sx_out_n && web_sx_out[e] != 0xF7u)
            e++;
        if (e == web_sx_out_n)
            break;                        /* (no F7 yet: never, sysex_send takes whole messages) */
        len = e - at + 1u;
        if (FM1CORE_DG_MQ - (fm1core_dg_mo_w - fm1core_dg_mo_r) < (len + 2u) / 3u)
            break;                        /* (the rest after the host has taken some) */
        fm1core_dg_sysex_out(web_sx_out + at, len);
        at += len;
    }
    if (at) {
        memmove(web_sx_out, web_sx_out + at, web_sx_out_n - at);
        web_sx_out_n -= at;
    }
}

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    web_owed = 0;                         /* (the shim's own audio stays unrendered: the glue's ISR renders) */
    web_notes_pressed |= notes & ~web_key_mask;
    web_key_mask = notes;
    web_key_tap = 0;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    web_pressed |= down;
    web_released |= up;
    for (i = 0; i < 7u; i++)
        web_enc_steps[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    const uint32_t lit = id < 14u ? web_led_btn >> id & 1u : id < 41u ? web_led_key >> (id - 14u) & 1u : 0u;
    return lit ? FM1P_LED_LIT : (fm1core_grm_glow >> id & 1u) ? FM1P_LED_DIM : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)   /* fm1_web.c web_midi */
{
    if (mq_w - mq_r >= 64u)
        return 0;
    mq[mq_w % 64u][0] = st;
    mq[mq_w % 64u][1] = d1;
    mq[mq_w % 64u][2] = d2;
    mq_w++;
    return 1;
}
static uint32_t fm1core_grm_set_writes, fm1core_grm_passes;
static void fm1core_dg_stores_load(void)
{
    uint32_t n = 0, i;
    web_want = 0;                         /* (every poll yields: one pass a tick) */
    if (!fm1core_dg_sector_read(FM1CORE_GRM_SETTINGS_AT, web_set, sizeof web_set, &n))
        web_set_len = (int32_t)n;         /* (platform.c settings_load: the length must match the app's) */
    else
        web_set_len = -1;
    fm1core_grm_set_writes = web_set_writes;
    (void)save_flash(0);                  /* (its first call erases the RAM sectors: before they are filled) */
    for (i = 0; i < SAVE_NSECT; i++)
        memcpy(fake_save[i], fm1nor_image + FM1CORE_GRM_SAVE_SECT[i], 4096u);
    memcpy(fake_flash, fm1nor_image + FM1CORE_GRM_SAMPLE_AREA, SAMPLE_AREA_LEN);
}
static void fm1core_grm_stores(int samples)
{
    uint32_t i;
    if (web_set_writes != fm1core_grm_set_writes && web_set_len >= 0) {
        fm1core_grm_set_writes = web_set_writes;
        fm1core_dg_sector_save(FM1CORE_GRM_SETTINGS_AT, web_set, (uint32_t)web_set_len);
    }
    for (i = 0; i < SAVE_NSECT; i++)
        fm1core_dg_raw_sync(FM1CORE_GRM_SAVE_SECT[i], fake_save[i]);
    if (samples) {                        /* (288 KiB compared: every 64 passes, and at a sync. No erase stall: the copy
                                           * comes up to 64 ms after the app's own writes, a first boot's factory
                                           * samples ~70 sectors at once; the audio is not held for them) */
        fm1nor_set_stall_ms(0);
        for (i = 0; i < SAMPLE_AREA_LEN; i += FM1NOR_SECTOR)
            fm1core_dg_raw_sync(FM1CORE_GRM_SAMPLE_AREA + i, fake_flash + i);
        fm1nor_set_stall_ms(FM1NOR_ERASE_MS);
    }
}
static void fm1core_dg_stores_poll(void)  /* after each pass: the stores, then the app's SysEx replies */
{
    fm1core_grm_stores(++fm1core_grm_passes % 64u == 0u);
    fm1core_grm_sx_out();
}
static int fm1core_dg_sync(void)
{
    int ok = 1;
    /* the main loop's deferred saves, now: the settings (2 s after a change, when silent), the autosave (the work and
     * the slots: the sequencer standing, the hands at rest 2.5 s, at most every 20 s, no voice sounding) */
    if (set_dirty) {
        settings_save(&set, sizeof set);
        set_dirty = 0;
    }
    if (!playing && !ed_open() && autosave_due()) {
        if (!voices_silent())
            ok = 0;                       /* (no flash while a voice sounds: it waits) */
        else if (autosave())
            ok = 0;                       /* (a failed one: tried again at the next sync) */
    }                                     /* (the app's own pass then finds nothing due: autosave_due) */
    fm1core_grm_stores(1);
    return ok;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u, track %u%s%s%s%s%s", (unsigned)page, (unsigned)sel + 1u, playing ? ", playing" : "",
             voices_silent() ? "" : ", voices", tape_idle() ? "" : ", tape", hall_idle() ? "" : ", space",
             grain_idle() ? "" : ", grain");
}

/* the M-UPGRADE identity and the app's mailbox: platform.c's platform_poll split (above) */
#define FELUCCA_ID "FM-1_966"             /* platform.c: GR!MM 1.x */
#define FM1CORE_DG_OTA_C "../../cores/grimm/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_grm_sx_in((p), (n))
#define FM1CORE_DG_SYSEX_READY() fm1core_grm_sx_ready()

#include "dinghy_glue.h"
