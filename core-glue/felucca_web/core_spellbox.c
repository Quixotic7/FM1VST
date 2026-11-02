/* SPDX-License-Identifier: GPL-3.0-only */
/* The SPELLBOX core (https://github.com/pjac1810/spellbox, the pinned, unmodified submodule cores/spellbox): a
 * talking spelling toy after the 1978 Speak & Spell (LPC speech, the VFD, SPELL / LETTER / SAY IT / SECRET CODE /
 * MYSTERY WORD, SING, LETTER SYNTH, SPEAK), on foma (konsumer/foma, Felucca's platform). SPELLBOX has no
 * felucca_web.c (nor fw_core.h): foma's app is three functions (setup / loop / audio over platform/fm1.h), and
 * its browser emulator is platform/web.c, the whole of fm1.h's API over a simulated FM-1. This unit #includes that
 * file, unmodified, and drives it as the page's worklet does, through the same calls and statics; the app
 * (src/main.c, src/speech.c, src/tts.c) is compiled as its own units (core_spellbox.cmake EXTRA_SOURCES), as
 * tools/build.py compiles it. The family's descriptor (fw_desc.h), as Hortator's and MONO2K's.
 *
 *   ABI                  web.c
 *   init                 the settings out of the NOR image (fm1.c's record: "FOMA", length, CRC-32 at 0xFC000) into
 *                        web_store, then web_boot (setup: "Hello", the first word), a first frame out
 *   frame / idle         the panel in (web_keys / web_buttons with the taps, web_encoder, web_master), then ONE
 *                        device millisecond: fm1.c's platform_poll part that is not hardware (ota_service), then
 *                        web.c's tick (input_snapshot, loop, screen_flush, the 256-frame blocks due into its ring);
 *                        the settings saved (web_store_gen moved: settings_save) written to the NOR image as fm1.c
 *                        writes them (the sectors erased, the bytes, the header last); the LEDs and the LCD out
 *   audio(128)           the ring tick filled (each sample v / 65536 as a float: exactly v again, the MASTER-scaled,
 *                        clipped s16 fm1.c's ISR gives the DAC), 256 frames of silence first (the blocks come 256
 *                        frames at a time: the lead keeps the ring from running dry)
 *   midi_in              usb.c: channel messages to web_midi_in (MIDI_USB), the clock and transport to the clock
 *                        helper (web_midi_in hands F8..FC to clock_in), SysEx frames to the glue's frame buffer
 *                        (sysex.h) for ota.c
 *   midi_out_take        the M-UPGRADE replies first, then web_midi_out (midi_send / the clock sent out)
 *   flash_sync           nothing waits: SAVE writes at once (main.c save_all -> settings_save)
 *
 * The identity: FM-1_573 (tools/build.py: firmware.json's "identity" FM-1_57 and the minor version's last digit),
 * from the firmware's own update entry (platform/src/ota.c, common/sysex_ota.h); APP_NAME / APP_VER / the FOMA_*
 * options as build.py gives them from firmware.json (core_spellbox.cmake). Not here: the update itself, UBOOT, the
 * rescue (OCT- at power-on), the boot guard, USB audio in (web.c: audio_in() is 0), the TRS jack (all MIDI is USB
 * here, as in the browser).
 *
 * Statics here are fm1core_sb_-prefixed (web.c, foma's libc.c / common.c / clock.c and ota.c are in this unit). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the NOR image */
#include "version.h"
#include "exit.h"                         /* (halted / shutdown: SPELLBOX never exits) */
#include "sysex.h"                        /* (core-glue/common) the frame buffer and the reply ring */

/* fm1.h first: its POOL is a .pool section off WebAssembly (Mach-O rejects the name): plain statics here, as in the
 * browser */
#include "fm1.h"
#undef POOL
#define POOL
/* foma's freestanding libc.c (web.c includes it) defines memset / memcpy / memcmp: kept to this unit under other
 * names, the host's libc stays the process's */
#undef memset
#undef memcpy
#undef memcmp
#define memset fm1core_sb_memset
#define memcpy fm1core_sb_memcpy
#define memcmp fm1core_sb_memcmp
/* web.c's EXPORT is wasm-ld's export_name: nothing exported from a core but fm1core_get */
#define export_name(x) unused
#include "../../cores/spellbox/platform/web.c"   /* the emulator: fm1.h's API over a simulated FM-1 */
#undef export_name
#undef memset
#undef memcpy
#undef memcmp

#include "exit_end.h"

/* ---- the update entry (ota.c): the M-UPGRADE identity on the glue's SysEx transport */
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_573"
#endif
static fm1sx_in_t fm1core_sb_sxi;
static fm1sx_out_t fm1core_sb_sxo;
static int fm1core_sb_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n); }
#define FM1SX_OTA_C "../../cores/spellbox/platform/src/ota.c"
#define FM1SX_OTA_IN fm1core_sb_sxi
#define FM1SX_OTA_OUT fm1core_sb_sxo
#define FM1SX_OTA_MS() now_ms
#include "sysex_ota.h"

/* ---- the settings in the NOR image: platform/fm1.c's record, its place and its order */
#define FM1CORE_SB_SET_AT 0x000FC000u     /* fm1.c SETTINGS_AT (Felucca's globals) */
#define FM1CORE_SB_SET_MAGIC 0x414D4F46u  /* "FOMA" */
static uint32_t fm1core_sb_crc32(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}
static void fm1core_sb_settings_in(void)  /* fm1.c settings_load's check, into the page's store */
{
    uint32_t h[3];
    if (fm1nor_read(FM1CORE_SB_SET_AT, h, sizeof h) || h[0] != FM1CORE_SB_SET_MAGIC || h[1] > SETTINGS_MAX)
        return;
    if (fm1nor_read(FM1CORE_SB_SET_AT + sizeof h, web_store(), h[1]))
        return;
    if (~fm1core_sb_crc32(0xFFFFFFFFu, web_store(), h[1]) != h[2])
        return;
    web_store_set_len((int32_t)h[1]);
}
static uint32_t fm1core_sb_store_gen;
static uint32_t fm1core_sb_saves, fm1core_sb_saves_failed;
static void fm1core_sb_settings_out(void) /* fm1.c settings_save: the sectors it needs, the bytes, the header last */
{
    uint32_t h[3], at, n;
    int rc = 0;
    if (web_store_gen() == fm1core_sb_store_gen || web_store_len() < 0)
        return;
    fm1core_sb_store_gen = web_store_gen();
    n = (uint32_t)web_store_len();
    h[0] = FM1CORE_SB_SET_MAGIC;
    h[1] = n;
    h[2] = ~fm1core_sb_crc32(0xFFFFFFFFu, web_store(), n);
    for (at = FM1CORE_SB_SET_AT; !rc && at < FM1CORE_SB_SET_AT + sizeof h + n; at += FM1NOR_SECTOR)
        rc = fm1nor_erase(at);
    if (!rc)
        rc = fm1nor_prog_span(FM1CORE_SB_SET_AT + sizeof h, web_store(), n);
    if (!rc)
        rc = fm1nor_prog_span(FM1CORE_SB_SET_AT, h, sizeof h);
    if (rc)
        fm1core_sb_saves_failed++;
    else
        fm1core_sb_saves++;
}

/* ---- the glue's state (hidden: reached only through the descriptor) */
fm1panel_t fm1core_sb_panel;
#define FM1CORE_SB_PANEL (&fm1core_sb_panel)
static const int8_t fm1core_sb_kmap[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;
static uint8_t fm1core_sb_ready;
static uint32_t fm1core_sb_lead;          /* the frames of silence still to give before the ring */
static uint32_t fm1core_sb_led_k = ~0u, fm1core_sb_led_b = ~0u, fm1core_sb_led_bg = ~0u;

/* ------------------------------------------------------------- input --- */
/* the platform's ids are the FM-1's matrix ids, as the panel's: buttons 0 OCT-, 1 OCT+, 2..13 FX .. REC (fm1.h's
 * BTN_* order), encoders SELECT ALGORITHM K1..K4 PRESETS (ENC_*), keys 0 = F3 .. 26 = G5 */
static void fm1core_sb_in(void)
{
    fm1panel_t *p = FM1CORE_SB_PANEL;
    uint32_t kt = fm1p_take_key_taps(p) & ((1u << KEY_COUNT) - 1u), bt = fm1p_take_button_taps(p) & 0x3FFFu, i;
    uint32_t kh = p->keys & ((1u << KEY_COUNT) - 1u), bh = p->buttons & 0x3FFFu;
    if (kt)
        web_keys(kh | kt);                /* (a tap: pressed this pass, released the next: web.c's taps_k) */
    web_keys(kh);
    if (bt)
        web_buttons(bh | bt);
    web_buttons(bh);
    for (i = 0; i < ENC_COUNT; i++) {
        int32_t d = fm1p_take_enc(p, i);
        if (d)
            web_encoder((int)i, d);
    }
    if (p->master > 0)
        web_master((uint32_t)(p->master > 1023 ? 1023 : p->master));
}

/* ------------------------------------------------------------- output --- */
static void fm1core_sb_out(void)
{
    fm1panel_t *p = FM1CORE_SB_PANEL;
    uint32_t lk = web_led_keys(), lb = web_led_buttons(), bg = web_led_backlight(), i;
    if (lk != fm1core_sb_led_k || lb != fm1core_sb_led_b || bg != fm1core_sb_led_bg) {
        /* fm1.c led_backlight: every key and button glows dimly behind the lit ones (0 off, 1 low, 2 full) */
        const uint8_t off = bg >= 2u ? FM1P_LED_DIM : bg ? FM1P_LED_FAINT : FM1P_LED_OFF;
        for (i = 0; i < KEY_COUNT; i++)
            fm1p_led_id(p, &fm1core_sb_kmap[0][0], (int)(14u + i), lk >> i & 1u ? FM1P_LED_LIT : off);
        for (i = 0; i < BTN_COUNT; i++)
            fm1p_led_id(p, &fm1core_sb_kmap[0][0], (int)i, lb >> i & 1u ? FM1P_LED_LIT : off);
        fm1core_sb_led_k = lk;
        fm1core_sb_led_b = lb;
        fm1core_sb_led_bg = bg;
    }
    fm1p_lcd_out(p, web_fb(), web_fb_gen());   /* (fb is stored byte-swapped, as the panel takes it: big-endian) */
}

/* ------------------------------------------------------------ the pass --- */
static void fm1core_sb_pass(void)
{
    if (fm1core_halted() || !fm1core_sb_ready)
        return;
    fm1core_sb_in();
    ota_service();                        /* (fm1.c platform_poll, before the pass) */
    tick();                               /* web.c: one device millisecond, one pass of loop() */
    fm1core_sb_settings_out();
    fm1core_sb_out();
}

/* ------------------------------------------------------- entry points --- */
int fm1core_sb_halted(void) { return fm1core_halted(); }
static void fm1core_sb_cleanup(void) { fm1nor_close(); }
void fm1core_sb_shutdown(void) { fm1core_run_exit(); }

void fm1core_sb_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (every write reaches the image at once; shutdown closes the file) */
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
    fm1core_exit_cleanup = fm1core_sb_cleanup;
}

void fm1core_sb_init(int demo)
{
    fm1panel_t *p = FM1CORE_SB_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1core_sb_cleanup;
    fm1sx_in_init(&fm1core_sb_sxi, fm1core_sb_sx_accept);
    fm1sx_out_reset(&fm1core_sb_sxo);
    fm1nor_open();
    fm1p_panel_default(p);                /* (Felucca's wiring: the platform's ids) */
    if (p->master > 0)
        web_master((uint32_t)(p->master > 1023 ? 1023 : p->master));
    fm1core_sb_settings_in();
    fm1core_sb_store_gen = web_store_gen();
    web_boot();                           /* setup() */
    fm1core_sb_lead = HALF_FRAMES;       /* one block of web.c's: 256 frames */
    fm1core_sb_ready = 1;
    p->ready = 1;
    fm1core_sb_out();
}
void fm1core_sb_tick(uint32_t ms) { (void)ms; } /* (web.c keeps the device's ms: one per pass) */
void fm1core_sb_frame(void) { fm1core_sb_pass(); }
void fm1core_sb_idle(void) { fm1core_sb_pass(); }
void fm1core_sb_audio(int16_t *out, uint32_t frames)
{
    uint32_t k;
    if (fm1core_halted() || !fm1core_sb_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    if (ring_w - ring_r > RING)           /* (no audio() for a while: the oldest are gone) */
        ring_r = ring_w - RING;
    for (k = 0; k < frames; k++) {
        int16_t l = 0, r = 0;
        if (k % FM1P_BLOCK == 0u && fm1nor_stall_take()) {   /* an erase: the loop stalls, a block of silence */
            const uint32_t drop = ring_w - ring_r < FM1P_BLOCK ? ring_w - ring_r : FM1P_BLOCK;
            memset(out + 2u * k, 0, FM1P_BLOCK * 2u * sizeof *out);
            ring_r += drop;               /* (what it would have played: the lead stays the same) */
            k += FM1P_BLOCK - 1u;
            continue;
        }
        if (fm1core_sb_lead)
            fm1core_sb_lead--;
        else if (ring_w != ring_r) {      /* (the ring: v / 65536 as a float, exactly v back) */
            uint32_t i = (ring_r++ % RING) * 2u;
            l = (int16_t)(ring[i] * 65536.0f);
            r = (int16_t)(ring[i + 1u] * 65536.0f);
        }
        out[2u * k] = l;
        out[2u * k + 1u] = r;
    }
}

int fm1core_sb_midi_in(uint32_t pkt)      /* usb.c midi_in_event (cable 0) */
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0xFFu, d2 = (pkt >> 24) & 0xFFu;
    if (fm1core_halted() || !fm1core_sb_ready)
        return 1;
    if (fm1sx_in_would_refuse(&fm1core_sb_sxi, pkt))
        return 0;                         /* (a frame waits for the pass: this one after it) */
    pkt &= ~0xF0u;
    if (cin >= 4u && cin <= 7u) {         /* SysEx (CIN 5: also a lone realtime byte) */
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++) {
            uint32_t b = (pkt >> (8u * (k + 1u))) & 0xFFu;
            if (b >= 0xF8u)
                web_midi_in(b, 0, 0);
        }
        fm1sx_in_pkt(&fm1core_sb_sxi, pkt);
        return 1;
    }
    fm1sx_in_pkt(&fm1core_sb_sxi, pkt);   /* (a status ends a frame) */
    if (cin == 0xFu && st >= 0xF8u) {     /* the clock and transport: the clock helper's (MIDI_RT) */
        web_midi_in(st, 0, 0);
        return 1;
    }
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !(d1 & 0x80u) && !(d2 & 0x80u))
        web_midi_in(st, d1, d2);
    return 1;
}
int fm1core_sb_midi_out_take(uint32_t *pkt)
{
    int32_t m;
    if (fm1core_halted())
        return 0;
    if (fm1sx_take(&fm1core_sb_sxo, pkt))
        return 1;                         /* (usb.c: SysEx first) */
    if ((m = web_midi_out()) < 0)
        return 0;
    {
        const uint32_t st = (uint32_t)m & 0xFFu;   /* fm1.c midi_send: CIN = status >> 4 */
        *pkt = (st >> 4) | st << 8 | ((uint32_t)m >> 8 & 0x7Fu) << 16 | ((uint32_t)m >> 16 & 0x7Fu) << 24;
    }
    return 1;
}
void fm1core_sb_dump(void)
{
    printf("%s: now_ms %u  fb_gen %u  settings %d bytes (saves %u, failed %u)  flash %s\n", FM1CORE_ID,
           (unsigned)now_ms, (unsigned)fb_gen, (int)web_store_len(), (unsigned)fm1core_sb_saves,
           (unsigned)fm1core_sb_saves_failed, fm1nor_path()[0] ? fm1nor_path() : "RAM only");
}
void fm1core_sb_ui_info(char *buf, uint32_t n) { snprintf(buf, n, "ms %u", (unsigned)now_ms); }
void fm1core_sb_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = 0;
}
const int8_t *fm1core_sb_keymap(void) { return &fm1core_sb_kmap[0][0]; }

/* ---- the flash (fm1core.h ABI 2) */
uint8_t *fm1core_sb_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_sb_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_sb_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_sb_flash_sync(void)
{
    if (fm1core_halted() || !fm1core_sb_ready)
        return 1;
    fm1core_sb_settings_out();
    return 1;
}
