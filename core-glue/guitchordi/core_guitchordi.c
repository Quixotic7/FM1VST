/* SPDX-License-Identifier: GPL-3.0-only */
/* The GuitChordi core (https://github.com/stoogs/guitchordi, the pinned, unmodified submodule cores/guitchordi):
 * Stoogs' six-string chord instrument (16 string / bowed / plucked sounds on Clipper's physical string model, 50
 * chord styles, 14 guitar tunings, Freeplay / Strum / Arp). The firmware is a portable application behind the fw_*
 * calls (firmware/app.c, compiled here with the device's -DGM_FM1=1) over the string engine (dsp/engine.c, its own
 * unit: guitchordi_engine.c), and a device adapter (firmware/fm1/main.c) on X0X's Felucca HAL that does nothing but
 * feed it. That adapter is hardware from top to bottom (the ISRs, the LCD's SPI, the LED scan, the update), so this
 * unit redoes its work on the ABI, in its order:
 *
 *   main.c                                  here
 *   fm1_cstart: fw_boot, fw_master(0),      init: the same, then the first frame (the boot screen)
 *     screen_flush
 *   fm1_alnk0_irq, once per 128-frame half: audio(128): the same calls in the same order, on the input the tick
 *     fw_keys(notes), fw_button(role, held)   debounced (a press at once, a release after FM1CORE_GC_RELEASE_MS open:
 *     by button_map, fw_encoder(role,         the device's integrating debounce, FM1_DEBOUNCE frames, makes no
 *     steps) by encoder_map, fw_master on     shorter contact), button_map / encoder_map being Felucca's PANEL_DEFAULT
 *     a change, fw_render(128), then          (fm1p_panel_default: the same tables), then the samples through the
 *     gm_dac_sample (audio-output.h)          device's own gm_dac_sample (24-bit values, the quiet-beta ceiling
 *                                             ~-22.5 dBFS, silent at MASTER 0) and >> 8 to the ABI's int16
 *   the main loop, every 50 ms: MASTER      frame / idle (one device ms each): the same every 50 ms. The LEDs: the
 *     (gm_master_reading), the diagnostics, main loop's three planes (a held or lit function button full, every other
 *     the LEDs (fw_buttons by button_map,   function button faint; a key by fw_key_brightness: >= 192 full, >= 80
 *     fw_key_brightness), screen_flush      medium, else faint) at their PWM duty (led-levels.h: 100 %, 25 %, 8.3 %)
 *   OCT- + OCT+ held 5 s: UBOOT            the same: exit(3) (the core halts)
 *   usb.c + ota.c (FELUCCA_OTA 1)          the M-UPGRADE identity: the platform's own ota.c on common/sysex_ota.h,
 *                                           FELUCCA_ID = identity.h's GM_FIRMWARE_ID "FM-1_982" (the identity
 *                                           build-fm1.py stamps into the package and its loader)
 *
 * What the device does not do, this does not either: no flash writes (update.h: "No project writes"; SAVE is a RAM
 * snapshot, the screen says "SAVE: RAM ONLY FOR NOW"), so the image is common/nor.c's, read at power-on and never
 * written; no performance MIDI ("Instrument MIDI performance is not implemented; discard ordinary events"), no MIDI
 * out. The device's diagnostics strip (GM_FM1: peak render us, late halves, MASTER, invalid samples) is drawn with
 * the render time and late halves 0 (no wall clock here), MASTER and the invalid-sample count as the device counts
 * them. Left out: the boot guard (two failed boots: UBOOT; no failed boot here), the update session (its key, F0 22 24
 * 35 7F F7, and UBOOT's, 7D, are not taken: common/sysex_ota.h), the watchdog.
 *
 * Macros and statics here are fm1core_gc_-prefixed (the firmware is in this unit). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the 1 MiB image */
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"
#include "sysex.h"                        /* (core-glue/common) the frame buffer and the reply ring */

#include "../../cores/guitchordi/firmware/app.c"           /* the application (GM_FM1: the device's build) */
#include "../../cores/guitchordi/firmware/fm1/audio-output.h"   /* gm_dac_sample, gm_master_reading */
#include "../../cores/guitchordi/firmware/fm1/identity.h"       /* GM_FIRMWARE_ID */
#define FELUCCA_ID GM_FIRMWARE_ID         /* (update.h: the package identity ota.c answers) */

/* ---------------------------------------------------- the M-UPGRADE identity --- */
static fm1sx_in_t fm1core_gc_sxi;
static fm1sx_out_t fm1core_gc_sxo;
static uint32_t fm1core_gc_ms;            /* the device's milliseconds (main.c's) */
static int fm1core_gc_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n); }
#define FM1SX_OTA_C "../../cores/guitchordi/third_party/fm1-platform/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_gc_sxi
#define FM1SX_OTA_OUT fm1core_gc_sxo
#define FM1SX_OTA_MS() fm1core_gc_ms
#include "sysex_ota.h"

#include "exit_end.h"

/* ---- the glue's state ---- */
fm1panel_t fm1core_gc_panel;
#define FM1CORE_GC_PANEL (&fm1core_gc_panel)
#define FM1CORE_GC_RELEASE_MS 5u          /* fm1_input.h: FM1_DEBOUNCE 8 frames of ~0.6 ms */
#define FM1CORE_GC_MASTER_DEFAULT 620     /* MASTER when the host gives none: ~60 %, the factory volume */
static const uint8_t fm1core_gc_button_map[14] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};   /* main.c */
static const uint8_t fm1core_gc_encoder_map[7] = {0, 1, 6, 2, 3, 4, 5};                           /* main.c */
static const int8_t fm1core_gc_keymap_t[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;   /* = fm1_input.h FM1_KEYMAP */
static uint8_t fm1core_gc_ready;
static uint32_t fm1core_gc_notes, fm1core_gc_buttons;          /* debounced, as fm1_in.notes / .buttons */
static uint8_t fm1core_gc_open_ms[FM1P_NKEY + FM1P_NBTN];      /* ms open while still on */
static int32_t fm1core_gc_enc_steps[FM1P_NENC];                /* fm1_in.enc_steps, matrix order */
static uint32_t fm1core_gc_master_value, fm1core_gc_applied_master = 0xffffffffu;
static int32_t fm1core_gc_master_smooth;
static uint32_t fm1core_gc_frame_time, fm1core_gc_octave_time;
static uint32_t fm1core_gc_halves, fm1core_gc_invalid;

/* ------------------------------------------------------------- input --- */
/* the panel's held masks and taps into the debounced state, once per device ms (tick); the detents, always */
static void fm1core_gc_debounce(uint32_t held, uint32_t *state, uint32_t n, uint8_t *open_ms)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        const uint32_t bit = 1u << i;
        if (held & bit) {
            *state |= bit;
            open_ms[i] = 0;
        } else if ((*state & bit) && ++open_ms[i] >= FM1CORE_GC_RELEASE_MS) {
            *state &= ~bit;
            open_ms[i] = 0;
        }
    }
}
static void fm1core_gc_in(int step)
{
    fm1panel_t *p = FM1CORE_GC_PANEL;
    const uint32_t k = (p->keys | fm1p_take_key_taps(p)) & ((1u << FM1P_NKEY) - 1u);
    const uint32_t b = (p->buttons | fm1p_take_button_taps(p)) & ((1u << FM1P_NBTN) - 1u);
    uint32_t i;
    if (step) {
        fm1core_gc_debounce(k, &fm1core_gc_notes, FM1P_NKEY, fm1core_gc_open_ms);
        fm1core_gc_debounce(b, &fm1core_gc_buttons, FM1P_NBTN, fm1core_gc_open_ms + FM1P_NKEY);
    } else {                              /* (frame / idle: a press at once, releases wait for the tick) */
        fm1core_gc_notes |= k;
        fm1core_gc_buttons |= b;
        for (i = 0; i < FM1P_NKEY; i++)
            if (k & (1u << i))
                fm1core_gc_open_ms[i] = 0;
        for (i = 0; i < FM1P_NBTN; i++)
            if (b & (1u << i))
                fm1core_gc_open_ms[FM1P_NKEY + i] = 0;
    }
    for (i = 0; i < FM1P_NENC; i++)
        fm1core_gc_enc_steps[i] += fm1p_take_enc(p, i);
}

/* ------------------------------------------------------------- output --- */
#define FM1CORE_GC_FULL 255u              /* led-levels.h: full 100 %, medium 25 %, faint 1 of 12 frames */
#define FM1CORE_GC_MEDIUM 64u
#define FM1CORE_GC_FAINT 21u
static void fm1core_gc_leds(void)        /* main.c's loop: the three planes */
{
    fm1panel_t *p = FM1CORE_GC_PANEL;
    const uint32_t active = fw_buttons();
    uint32_t physical = 0, role, col, row;
    for (role = 0; role < 14u; role++)
        if (active & (1u << role))
            physical |= 1u << fm1core_gc_button_map[role];
    for (col = 0; col < FM1P_NCOL; col++)
        for (row = 1; row < 5u; row++) {
            const int id = fm1core_gc_keymap_t[row][col];
            uint8_t v = 0;
            if (id >= 0 && id < 14)
                v = (physical & (1u << id)) ? FM1CORE_GC_FULL : FM1CORE_GC_FAINT;
            else if (id >= 14) {
                const int br = fw_key_brightness(id - 14);
                v = br >= 192 ? FM1CORE_GC_FULL : br >= 80 ? FM1CORE_GC_MEDIUM : br ? FM1CORE_GC_FAINT : 0;
            }
            p->led[col][row - 1u] = v;
        }
    p->led_play_green = p->led[FM1P_PLAY_GREEN_POS >> 3][(FM1P_PLAY_GREEN_POS & 7u) - 1u];
}
static void fm1core_gc_screen(void) { fm1p_lcd_out_native(FM1CORE_GC_PANEL, fw_frame()); }

/* ------------------------------------------------------- entry points --- */
int fm1core_gc_halted(void) { return fm1core_halted(); }
void fm1core_gc_shutdown(void) { fm1core_run_exit(); }

void fm1core_gc_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (nothing is ever written) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

void fm1core_gc_init(int demo)
{
    fm1panel_t *p = FM1CORE_GC_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_open();                        /* (the image the host keeps; the firmware never reads or writes it) */
    fm1sx_in_init(&fm1core_gc_sxi, fm1core_gc_sx_accept);
    fm1sx_out_reset(&fm1core_gc_sxo);
    fm1p_panel_default(p);                /* = main.c's button_map / encoder_map, every knob clockwise + */
    if (p->master <= 0)
        p->master = FM1CORE_GC_MASTER_DEFAULT;
    fm1core_gc_ms = 0;
    fm1core_gc_frame_time = fm1core_gc_octave_time = 0;
    fm1core_gc_master_value = 0;          /* (cold start: silent until the first ADC read) */
    fm1core_gc_master_smooth = 0;
    fm1core_gc_applied_master = 0xffffffffu;
    fw_boot();                            /* main.c fm1_cstart's order */
    fw_master(0);
    fm1core_gc_ready = 1;
    p->ready = 1;
    fm1core_gc_in(0);                     /* (held at power-on: in the input from the start) */
    fm1core_gc_leds();
    fm1core_gc_screen();
}

void fm1core_gc_tick(uint32_t ms)
{
    if (fm1core_halted() || !fm1core_gc_ready)
        return;
    fm1core_gc_ms = ms;
    fm1core_gc_in(1);
}

static void fm1core_gc_main(void)        /* main.c's loop, one pass per device ms */
{
    if (fm1core_halted() || !fm1core_gc_ready)
        return;
    fm1core_gc_in(0);
    /* recovery entry: OCT- + OCT+ (matrix buttons 0 and 1) held 5 s */
    if ((fm1core_gc_buttons & 3u) != 3u)
        fm1core_gc_octave_time = fm1core_gc_ms;
    else if (fm1core_gc_ms - fm1core_gc_octave_time >= 5000u) {
        printf("%s: OCT- + OCT+ held 5 s: UBOOT\n", FM1CORE_ID);
        fm1core_exit(3);                  /* (the device: fm1_audio_stop, fm1_enter_uboot) */
        return;
    }
    ota_service();                        /* (update.h update_service: the M-UPGRADE handshake) */
    if (fm1core_gc_ms - fm1core_gc_frame_time >= 50u) {
        fm1core_gc_frame_time = fm1core_gc_ms;
        fm1core_gc_master_value = gm_master_reading((int)FM1CORE_GC_PANEL->master, &fm1core_gc_master_smooth);
        fw_audio_diagnostics(0, 0, fm1core_gc_master_value, fm1core_gc_invalid);
        fm1core_gc_leds();
        fm1core_gc_screen();
    }
}
void fm1core_gc_frame(void) { fm1core_gc_main(); }
void fm1core_gc_idle(void) { fm1core_gc_main(); }

/* one audio half per 128 frames, as fm1_alnk0_irq */
static void fm1core_gc_half(int16_t *out, uint32_t frames)
{
    uint32_t i;
    float *samples;
    fw_keys(fm1core_gc_notes);
    for (i = 0; i < 14u; i++)
        fw_button((int)i, (int)((fm1core_gc_buttons >> fm1core_gc_button_map[i]) & 1u));
    for (i = 0; i < 7u; i++) {
        const int steps = fm1core_gc_enc_steps[fm1core_gc_encoder_map[i]];
        fm1core_gc_enc_steps[fm1core_gc_encoder_map[i]] = 0;
        if (steps)
            fw_encoder((int)i, steps);
    }
    if (fm1core_gc_applied_master != fm1core_gc_master_value) {
        fm1core_gc_applied_master = fm1core_gc_master_value;
        fw_master((int)fm1core_gc_applied_master);
    }
    samples = fw_render((int)frames);
    for (i = 0; i < frames; i++) {
        const float value = samples ? samples[i] : 0;
        int32_t s;
        if (!(value >= -1.0f && value <= 1.0f))
            fm1core_gc_invalid++;
        s = gm_dac_sample(value, fm1core_gc_master_value) >> 8;   /* (24-bit value -> int16) */
        out[2u * i] = out[2u * i + 1u] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    fm1core_gc_halves++;
}
void fm1core_gc_audio(int16_t *out, uint32_t frames)
{
    if (fm1core_halted() || !fm1core_gc_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    while (frames) {
        const uint32_t n = frames > GM_BLOCK ? GM_BLOCK : frames;
        fm1core_gc_half(out, n);
        out += 2u * n;
        frames -= n;
    }
}

int fm1core_gc_midi_in(uint32_t pkt)
{
    if (fm1core_halted() || !fm1core_gc_ready)
        return 1;                         /* (halted: taken and dropped, never "retry") */
    if (fm1sx_in_would_refuse(&fm1core_gc_sxi, pkt))
        return 0;                         /* (a handshake waits for the main loop: this one after it) */
    fm1sx_in_pkt(&fm1core_gc_sxi, pkt);   /* (everything else is discarded, as main.c's mi_r = mi_w) */
    return 1;
}
int fm1core_gc_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    return fm1sx_take(&fm1core_gc_sxo, pkt);   /* (the device sends nothing but SysEx replies) */
}

void fm1core_gc_dump(void)
{
    printf("%s: ms %u  buttons %04X notes %07X  master %u (volume %d)  halves %u invalid %u\n", FM1CORE_ID,
           (unsigned)fm1core_gc_ms, (unsigned)fm1core_gc_buttons, (unsigned)fm1core_gc_notes,
           (unsigned)fm1core_gc_master_value, fw_volume(), (unsigned)fm1core_gc_halves, (unsigned)fm1core_gc_invalid);
    printf("  style %d '%s'  tone %d '%s'  slot %d '%s'  play mode %d  bpm %d  tuning %d  locked %d  page %d  "
           "voices %d\n",
           fw_mode(), fw_mode_name(fw_mode()), fw_tone(), gm_tone_name(fw_tone()), fw_selected() + 1,
           fw_slot_name(fw_selected()), fw_play_mode(), fw_bpm(), fw_tuning(), fw_locked(), fw_page(), gm_active());
    printf("  flash: %s (never written)\n", fm1nor_path()[0] ? fm1nor_path() : "RAM only");
}
void fm1core_gc_ui_info(char *buf, uint32_t n)
{
    static const char *const play[3] = {"FREEPLAY", "STRUM", "ARP"};
    snprintf(buf, n, "%s page %d style '%s' tone '%s' chord '%s' %s%s", fw_boot_screen() ? "boot" : "play", fw_page(),
             fw_mode_name(fw_mode()), gm_tone_name(fw_tone()), fw_slot_name(fw_selected()),
             play[(unsigned)fw_play_mode() % 3u], fw_locked() ? " locked" : "");
}
void fm1core_gc_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = (uint32_t)gm_steals();
    *cpu_pct = 0;
}
const int8_t *fm1core_gc_keymap(void) { return &fm1core_gc_keymap_t[0][0]; }

/* ---- the flash (fm1core.h ABI 2): the image only; GuitChordi writes none ---- */
uint8_t *fm1core_gc_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_gc_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_gc_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_gc_flash_sync(void) { return 1; }
