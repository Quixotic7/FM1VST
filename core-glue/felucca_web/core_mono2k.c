/* SPDX-License-Identifier: GPL-3.0-only */
/* The MONO2K core (https://github.com/InigoHernandez/mono2k, the pinned, unmodified submodule cores/mono2k):
 * Sequencist's six mono machines after the Monomachine, a 64-step sequencer and a lofi bus. MONO2K's core
 * (firmware/src, the unity build m2k_core.c) has no platform code: the FM-1 build (firmware/fm1/m2k_fm1.c, on CS-1's
 * platform after Dinghy and Felucca), the browser (web/m2k_web.c) and the host (host/m2k_host.c) each drive it
 * through m2k.h. This unit drives it as m2k_fm1.c does, built as tools/fm1/build.py builds the FM-1's (M2K_FW: the
 * store a stream into the flash; one project of 64 patterns, two unpacked, a 46 KB image), and compiles the FM-1's
 * own store and Patch Editor link (firmware/fm1/m2k_link.c) and update entry (firmware/fm1/src/ota.c, Felucca's)
 * unmodified over the glue's NOR and SysEx transport. Not felucca_web.c (MONO2K has none, nor fw_core.h): the
 * family's descriptor (fw_desc.h) over the entry points here, as Hortator's.
 *
 *   ABI                  m2k_fm1.c
 *   init                 app_init: m2k_init, store_load (the store at 0xC0000 in the NOR image: the factory's bank
 *                        when there is none), a first frame
 *   frame / idle         app_loop's pass: the panel (take_panel: the platform's button and encoder ids to the core's),
 *                        MIDI in (midi_get: everything here is USB), the platform's SysEx (platform_poll: the app's
 *                        hook app_sysex first, then ota_service), a frame when 33 ms have gone (screen_frame: 1-bit,
 *                        ink black on white paper), the LEDs (lights: on from a quarter), lk_service and the save once
 *                        the sequencer stands
 *   audio(128)           m2k_render of the block (the device renders ahead into a ring the interrupt copies), MASTER
 *                        applied as platform.c's ISR does (master_q12 = k10^2 >> 8, then the clip), MIDI out
 *                        (midi_drain) as it is played; a sector erase is silence (the device's loop stalls)
 *   midi_in              usb.c midi_in_event: channel messages, the clock and transport (also inside SysEx, CIN 5 or
 *                        F), the song position; SysEx frames to the platform's one frame buffer (sysex.h)
 *   flash_sync           the save due (SAVE, a setting's change after its 3 s wait) now, if the sequencer stands
 *
 * The identity: FM-1_980 (tools/fm1/build.py PRODUCT, every release); the version 1.6.2 (build.py --release, as
 * web/firmware/mono2k.json says the site's package is), APP_VER / M2K_VER (core_mono2k.cmake). Not here: the
 * update itself (F0 22 24 35 7F F7: no update inside a core), UBOOT (F0 22 24 35 7D F7), the rescue (OCT- at
 * power-on), the boot guard, the second core (the core's m2k_par_begin unset: it renders every track in place, as
 * in the browser), the CPU meter (m2k_cpu: GLO > PLAY shows dashes, as in the browser), USB audio, key velocity.
 *
 * Statics here are fm1core_m2_-prefixed (the core, ota.c and m2k_link.c are in this unit). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the NOR image (A) */
#include "version.h"
#include "exit.h"                         /* (halted / shutdown: MONO2K never exits) */
#include "sysex.h"                        /* (core-glue/common) the frame buffer and the reply ring */

#include "../../cores/mono2k/firmware/src/m2k_core.c"   /* the core: m2k.h's API (and its statics) */

#include "exit_end.h"

/* ---- the platform's names, as m2k_fm1.c sets them and platform.c gives them */
#define APP_NAME "MONO2K"
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FELUCCA_ID "FM-1_980"             /* tools/fm1/build.py PRODUCT */
#define BANK_AT 0x000C0000u               /* m2k_fm1.c: the store's region */
#define BANK_LEN 0x00020000u
static uint32_t fm1_ms;                   /* the device's milliseconds (tick) */
static uint8_t flash_ok = 1;
static void fm1_wdt_feed(void) {}
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    *took = FM1NOR_ERASE_MS;
    return fm1nor_erase(off);
}
static int fl_write(uint32_t off, const void *p, uint32_t n) { return fm1nor_prog_span(off, p, n); }

/* ---- the platform's SysEx: one frame (usb.c sx_frame), the app's hook first, then the update entry (ota.c) */
static fm1sx_in_t fm1core_m2_sxi;
static fm1sx_out_t fm1core_m2_sxo;
static int fm1core_m2_sx_accept(const uint8_t *f, uint32_t n)   /* the link's 7D 'M' '2' 'K', M-UPGRADE's 00 32 */
{
    static const uint8_t LK[4] = {0x7Du, 'M', '2', 'K'};
    return fm1sx_is_mupg(f, n) || (n >= 1u && !memcmp(f, LK, n < 4u ? n : 4u));
}
#define FM1SX_OTA_C "../../cores/mono2k/firmware/fm1/src/ota.c"
#define FM1SX_OTA_IN fm1core_m2_sxi
#define FM1SX_OTA_OUT fm1core_m2_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

#include "../../cores/mono2k/firmware/fm1/m2k_link.c"   /* the store in the flash and the Patch Editor's link */

/* ---- the glue's state (hidden: reached only through the descriptor) */
fm1panel_t fm1core_m2_panel;
#define FM1CORE_M2_PANEL (&fm1core_m2_panel)
static const int8_t fm1core_m2_kmap[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;
static uint8_t fm1core_m2_ready;
static fm1p_edges_t fm1core_m2_bedge, fm1core_m2_kedge;
static int32_t fm1core_m2_knob = 724 * 16, fm1core_m2_q12 = 2047;   /* platform.c knob_avg, master_q12 */
static uint32_t fm1core_m2_frame_ms;
#define FM1CORE_M2_MQ 256u                /* usb.c's MIDI ring (MQ) */
static uint32_t fm1core_m2_mi[FM1CORE_M2_MQ], fm1core_m2_mi_w, fm1core_m2_mi_r;
#define FM1CORE_M2_OQ 1024u
static uint32_t fm1core_m2_mo[FM1CORE_M2_OQ], fm1core_m2_mo_w, fm1core_m2_mo_r;

/* ------------------------------------------------------------- input --- */
/* m2k_fm1.c take_panel: the platform's ids are the FM-1's matrix ids (0 OCT-, 1 OCT+, 2..13 FX .. REC; encoders
 * SELECT ALGO K1..K4 PRESETS), the panel's too */
static int fm1core_m2_button_of(uint32_t id) { return id == 0u ? B_OCTD : id == 1u ? B_OCTU : (int)id - 2; }
static const uint8_t FM1CORE_M2_ENC_OF[7] = {E_SELECT, E_ALGO, E_K1, E_K2, E_K3, E_K4, E_PRESET};

static void fm1core_m2_in(void)
{
    fm1panel_t *p = FM1CORE_M2_PANEL;
    uint32_t down, up, i;
    int32_t a;
    fm1p_edges(&fm1core_m2_bedge, p->buttons & 0x3FFFu, fm1p_take_button_taps(p) & 0x3FFFu, &down, &up);
    for (i = 0; i < 14u; i++) {
        if (down >> i & 1u) m2k_button(fm1core_m2_button_of(i), 1);
        if (up >> i & 1u) m2k_button(fm1core_m2_button_of(i), 0);
    }
    fm1p_edges(&fm1core_m2_kedge, p->keys & ((1u << M2K_KEYS) - 1u), fm1p_take_key_taps(p) & ((1u << M2K_KEYS) - 1u),
               &down, &up);
    for (i = 0; i < M2K_KEYS; i++) {
        if (down >> i & 1u) m2k_key((int)i, 1);
        if (up >> i & 1u) m2k_key((int)i, 0);
    }
    for (i = 0; i < 7u; i++) {
        int32_t d = fm1p_take_enc(p, i);
        if (d) m2k_encoder(FM1CORE_M2_ENC_OF[i], d);
    }
    /* MASTER: platform_poll's IIR on the pot, its square law (724 when the host gives none) */
    a = p->master > 0 ? (p->master > 1023 ? 1023 : p->master) : 724;
    fm1core_m2_knob += (a * 16 - fm1core_m2_knob) / 8;
    {
        uint32_t k10 = (uint32_t)(fm1core_m2_knob / 16);
        fm1core_m2_q12 = (int32_t)((k10 * k10) >> 8);
    }
    /* MIDI in (midi_get: src 0, USB) */
    while (fm1core_m2_mi_r != fm1core_m2_mi_w) {
        uint32_t pk = fm1core_m2_mi[fm1core_m2_mi_r++ % FM1CORE_M2_MQ];
        m2k_midi_from(M2K_USB, (int)(pk >> 8 & 0xFFu), (int)(pk >> 16 & 0xFFu), (int)(pk >> 24 & 0xFFu));
    }
}

/* ------------------------------------------------------------- output --- */
static void fm1core_m2_lights(void)       /* m2k_fm1.c lights: on from a quarter (the panel's LEDs are on / off) */
{
    fm1panel_t *p = FM1CORE_M2_PANEL;
    const uint8_t *l = m2k_leds();
    uint32_t i;
    for (i = 0; i < M2K_KEYS; i++)
        fm1p_led_id(p, &fm1core_m2_kmap[0][0], (int)(14u + i), l[i] >= 64u ? FM1P_LED_LIT : FM1P_LED_OFF);
    for (i = 0; i < 14u; i++)
        fm1p_led_id(p, &fm1core_m2_kmap[0][0], (int)i,
                    l[M2K_KEYS + fm1core_m2_button_of(i)] >= 64u ? FM1P_LED_LIT : FM1P_LED_OFF);
}
static void fm1core_m2_screen(void)       /* m2k_fm1.c screen_frame: the 1-bit frame, ink 0x0000 on paper 0xFFFF */
{
    fm1panel_t *p = FM1CORE_M2_PANEL;
    const uint8_t *f = m2k_frame(fm1_ms);
    uint32_t i, diff = 0;
    fm1core_m2_frame_ms = fm1_ms;
    for (i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++) {
        const uint16_t c = (f[i >> 3] >> (7u - (i & 7u)) & 1u) ? 0x0000u : 0xFFFFu;
        diff |= (uint32_t)(p->lcd[i] ^ c);
        p->lcd[i] = c;
    }
    if (diff)
        p->lcd_writes++;
    fm1core_m2_lights();
}

/* ------------------------------------------------------------ the pass --- */
static void fm1core_m2_save_poll(void)    /* app_loop's end: SAVE kept it, into the flash once the sequencer stands */
{
    if (m2k_dirty()) save_due = 1;
    lk_service();
    if (save_due && !m2k_playing() && !lk_open) {
        store_save();
        save_due = 0;
    }
}
static void fm1core_m2_pass(void)
{
    const uint8_t *sp;
    uint32_t sn;
    if (fm1core_halted() || !fm1core_m2_ready)
        return;
    fm1core_m2_in();
    if (fm1sx_frame(&fm1core_m2_sxi, &sp, &sn) && app_sysex(sp, sn))   /* platform_poll: the app's first */
        fm1sx_done(&fm1core_m2_sxi);
    ota_service();
    if (fm1_ms - fm1core_m2_frame_ms >= 33u)   /* (frame_gap: 30 a second when the core has room) */
        fm1core_m2_screen();
    fm1core_m2_save_poll();
}

/* ------------------------------------------------------- entry points --- */
int fm1core_m2_halted(void) { return fm1core_halted(); }
static void fm1core_m2_cleanup(void) { fm1nor_close(); }
void fm1core_m2_shutdown(void) { fm1core_run_exit(); }

void fm1core_m2_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (every write reaches the image at once; shutdown closes the file) */
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
    fm1core_exit_cleanup = fm1core_m2_cleanup;
}

static void fm1core_m2_at_exit(void)      /* the save due, as the loop would make it once stopped */
{
    if (fm1core_m2_ready && !m2k_playing()) {
        if (set_wait) { set_wait = 0; dirty = 1; }
        fm1core_m2_save_poll();
    }
}

void fm1core_m2_init(int demo)
{
    fm1panel_t *p = FM1CORE_M2_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1core_m2_cleanup;
    fm1sx_in_init(&fm1core_m2_sxi, fm1core_m2_sx_accept);
    fm1sx_out_reset(&fm1core_m2_sxo);
    fm1nor_open();
    fm1p_panel_default(p);                /* (Felucca's wiring: the platform's ids) */
    if (p->master > 0) {
        fm1core_m2_knob = (p->master > 1023 ? 1023 : p->master) * 16;
        fm1core_m2_q12 = (int32_t)(((uint32_t)fm1core_m2_knob / 16u) * ((uint32_t)fm1core_m2_knob / 16u) >> 8);
    }
    /* app_init: the core, the store (the factory's bank stays when there is none), a first frame */
    m2k_init(1u);                         /* (the device seeds it with its ticks: a fixed seed here, reproducible) */
    store_load();
    fm1core_atexit(fm1core_m2_at_exit);
    fm1core_m2_ready = 1;
    p->ready = 1;
    fm1core_m2_screen();
}
void fm1core_m2_tick(uint32_t ms) { fm1_ms = ms; }
void fm1core_m2_frame(void) { fm1core_m2_pass(); }
void fm1core_m2_idle(void) { fm1core_m2_pass(); }
void fm1core_m2_audio(int16_t *out, uint32_t frames)
{
    uint32_t f, i;
    uint8_t m[3];
    int n;
    if (fm1core_halted() || !fm1core_m2_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    for (f = 0; f < frames; f += FM1P_BLOCK) {
        int16_t *o = out + 2u * f;
        if (fm1nor_stall_take()) {        /* an erase: the loop stalls, the ring runs dry */
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        m2k_render(o, FM1P_BLOCK);
        for (i = 0; i < FM1P_BLOCK * 2u; i++) {   /* platform.c's ISR: MASTER, the clip */
            int32_t v = (o[i] * fm1core_m2_q12) >> 12;
            o[i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        while ((n = m2k_midi_out(m, m2k_samples())) > 0) {   /* midi_drain / midi_send: USB only */
            const uint32_t st = m[0], cin = st == 0xF2u ? 3u : st >> 4;
            if (fm1core_m2_mo_w - fm1core_m2_mo_r >= FM1CORE_M2_OQ)
                break;
            fm1core_m2_mo[fm1core_m2_mo_w++ % FM1CORE_M2_OQ] =
                cin | st << 8 | (uint32_t)(n > 1 ? m[1] & 0x7Fu : 0u) << 16 | (uint32_t)(n > 2 ? m[2] & 0x7Fu : 0u) << 24;
        }
    }
}

static void fm1core_m2_mi_put(uint32_t pkt)
{
    if (fm1core_m2_mi_w - fm1core_m2_mi_r < FM1CORE_M2_MQ)
        fm1core_m2_mi[fm1core_m2_mi_w++ % FM1CORE_M2_MQ] = pkt;
}
static void fm1core_m2_rt(uint32_t b)     /* usb.c midi_in_rt: the clock and transport */
{
    if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu)
        fm1core_m2_mi_put(0xFu | b << 8);
}
int fm1core_m2_midi_in(uint32_t pkt)      /* usb.c midi_in_event */
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (fm1core_halted() || !fm1core_m2_ready)
        return 1;
    if (FM1CORE_M2_MQ - (fm1core_m2_mi_w - fm1core_m2_mi_r) < 8u)
        return 0;                         /* (ep1_take: the host keeps the packet) */
    if (fm1sx_in_would_refuse(&fm1core_m2_sxi, pkt))
        return 0;                         /* (a frame waits for the pass: this one after it) */
    pkt &= ~0xF0u;                        /* cable 0 */
    if (cin >= 4u && cin <= 7u) {         /* SysEx (CIN 5: also a lone realtime byte) */
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++) {
            uint32_t b = (pkt >> (8u * (k + 1u))) & 0xFFu;
            if (b >= 0xF8u)
                fm1core_m2_rt(b);
        }
        fm1sx_in_pkt(&fm1core_m2_sxi, pkt);   /* (it skips the realtime bytes itself) */
        return 1;
    }
    fm1sx_in_pkt(&fm1core_m2_sxi, pkt);   /* (a status ends a frame) */
    if (cin == 0xFu && st >= 0xF8u) {
        fm1core_m2_rt(st);
        return 1;
    }
    if (cin == 3u && st == 0xF2u && !((pkt >> 16) & 0x80u) && !(pkt & 0x80000000u)) {
        fm1core_m2_mi_put(pkt);
        return 1;
    }
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        (cin == 0xCu || cin == 0xDu || !(pkt & 0x80000000u)))
        fm1core_m2_mi_put(pkt);
    return 1;
}
int fm1core_m2_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    if (fm1sx_take(&fm1core_m2_sxo, pkt))
        return 1;                         /* (usb.c ep1_tx: SysEx first) */
    if (fm1core_m2_mo_r == fm1core_m2_mo_w)
        return 0;
    *pkt = fm1core_m2_mo[fm1core_m2_mo_r++ % FM1CORE_M2_OQ];
    return 1;
}
void fm1core_m2_dump(void)
{
    printf("%s: fm1_ms %u  pattern %d track %d playing %d  master_q12 %d  store %s, saves failed %u\n", FM1CORE_ID,
           (unsigned)fm1_ms, cur_pat, ui.trk, m2k_playing(), (int)fm1core_m2_q12,
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)saves_failed);
}
void fm1core_m2_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "pattern %c%02d track %d sub %u playing %d", 'A' + cur_pat / 16, cur_pat % 16 + 1, ui.trk + 1,
             (unsigned)ui.sub, m2k_playing());
}
void fm1core_m2_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = 0;
}
const int8_t *fm1core_m2_keymap(void) { return &fm1core_m2_kmap[0][0]; }

/* ---- the flash (fm1core.h ABI 2) */
uint8_t *fm1core_m2_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_m2_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_m2_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
/* make the image current: the save due (SAVE, or a setting changed and waiting its 3 s) now; 1: current, 0: the
 * sequencer plays (the device writes its flash only once it stands) */
int fm1core_m2_flash_sync(void)
{
    if (fm1core_halted() || !fm1core_m2_ready)
        return 1;
    if (set_wait && !m2k_playing()) { set_wait = 0; dirty = 1; }
    fm1core_m2_save_poll();
    return !save_due && !set_wait;
}
