/* SPDX-License-Identifier: GPL-3.0-only */
/* The felucca_web family's core (CORES-PLAN.md 4, family A): ONE generic glue for every firmware that ships Felucca
 * 1.6's browser unit, web/emu/felucca_web.c (Felucca 1.6.1, Rainbow Mode, GHOULBOX, JIANT FM, DUELL). That file is a
 * complete host build of the device, written and maintained by each firmware's author: tests/hostsim.c's sound
 * side, the audio ISR (audio.c), the UI, main.c and master.c, the storage on a 1 MiB NOR array, and its own device
 * clock (web_ms: one device millisecond, the audio halves due, then main.c's 15 ms frame or ui_input in between).
 * This header #includes it, unmodified, and drives its statics from the same translation unit:
 *
 *   ABI                  the web unit
 *   options / init       the flash image (its nor[]: erased, then the staged bytes or a file), web_power_on
 *   tick(ms)             the device ms recorded (nothing runs)
 *   frame / idle         the panel's input into fm1_in / web_pressed / web_enc_steps / web_master_adc (as web_buttons,
 *                        web_keys, web_enc, web_master do), then ONE web_ms(): the device's millisecond as the browser
 *                        runs it (its own 15 ms frame cadence, from its own boot time), then the LEDs and the LCD out.
 *                        The firmware's frame is therefore web_ms's, not the host's frame() call: the host's
 *                        frame / idle split only says when the output is read (both are one device ms here).
 *   audio(128)           the ring web_ms filled (one I2S half each, already rendered by the audio ISR), as s16:
 *                        abuf (Q15 << OUT_SHIFT) >> OUT_SHIFT, i.e. Q15 = 0 dBFS as ChoralRoot's emulator and the
 *                        old Felucca glue (the browser plays it 6 dB lower: abuf / 2^23)
 *   midi_in              usb.c midi_enqueue (any USB-MIDI packet, as the USB endpoint takes it)
 *   midi_out_take        usb.c midi_out_q (the same unit: usb.config = 1, a host is there)
 *   flash / flash_dirty  nor[] / web_flash_writes
 *   flash_sync           settings_poll (persist_pending forced) and the autosave (project.c: its quiet / gap timers
 *                        run out, so the music is written now if it changed and nothing sounds)
 *
 * Added here (what the web unit leaves out of main.c): the boot guard (fm1_cstart: a second failed boot in a row
 * enters UBOOT = exit(3)), OCT- + OCT+ held 5 s (UBOOT, its countdown) and the USB UBOOT SysEx (exit(3)); a 30 s
 * clean run clears the guard. Not here: the erase stall (the web unit's st_erase is a static the glue cannot wrap),
 * USB audio, the editor's SysEx, the second core (every web unit builds it off).
 *
 * A firmware's unit (core_<id>.c) sets, then includes this file:
 *   FM1CORE_FW_P           the entry points' prefix (fm1core_<P>_init ..)                     (required)
 *   FM1CORE_FW_WEB_C       "<path>/web/emu/felucca_web.c" (relative to this folder)           (required)
 *   FM1CORE_FW_DEFAULT_FLASH  the flash file when options() gets NULL outside headless       ("build/<id>/flash.bin")
 *   FM1CORE_FW_ANIM_IDLE   1: the web unit has 1.2's idle LED animation (web_anim_idle)        (0)
 *   FM1CORE_FW_BOOTGUARD   0: no fm1_cstart guard (boot_options NULL)                          (1)
 *   FM1CORE_FW_BEFORE_BOOT()  before web_power_on (JIANT: the session seed)
 *   FM1CORE_FW_AFTER_MS()     after each web_ms (DUELL: its reboot request)
 *   FM1CORE_FW_LED(c, r, lv)  raise lv (a uint32_t 0..255) for the LED at column c, row bit r from the firmware's
 *                             own extra planes (JIANT hi, DUELL's keyboard sets)
 *   FM1CORE_FW_DUMP()         more lines for dump()
 *   FM1CORE_FW_UI_HOME()      HOME is showing (default ui.home; DUELL has no such flag)
 *   FM1CORE_FW_RESTORE_ON()   the autosave is on (default: !(ui_prefs & PREF_RESTORE_OFF); JIANT: PREF_RESTORE_ON)
 * Then the descriptor (core_<id>_desc.c, fw_desc.h) names the entry points with the same prefix.
 *
 * Macros and statics here are fm1core_-prefixed (the firmware is in this unit: an unprefixed name would collide). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"                      /* the panel (before hostsim's __attribute__ macro) */
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the staged bytes (fm1nor_stage) */
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */

#ifndef FM1CORE_FW_P
#error "FM1CORE_FW_P: the entry points' prefix"
#endif
#ifndef FM1CORE_FW_WEB_C
#error "FM1CORE_FW_WEB_C: the firmware's web/emu/felucca_web.c"
#endif

#include FM1CORE_FW_WEB_C

#include "exit_end.h"

#ifndef FM1CORE_FW_DEFAULT_FLASH
#define FM1CORE_FW_DEFAULT_FLASH "build/" FM1CORE_ID "/flash.bin"
#endif
#ifndef FM1CORE_FW_ANIM_IDLE
#define FM1CORE_FW_ANIM_IDLE 0
#endif
#ifndef FM1CORE_FW_BOOTGUARD
#define FM1CORE_FW_BOOTGUARD 1
#endif
#ifndef FM1CORE_FW_BEFORE_BOOT
#define FM1CORE_FW_BEFORE_BOOT() ((void)0)
#endif
#ifndef FM1CORE_FW_AFTER_MS
#define FM1CORE_FW_AFTER_MS() ((void)0)
#endif
#ifndef FM1CORE_FW_LED
#define FM1CORE_FW_LED(c, r, lv) ((void)0)
#endif
#ifndef FM1CORE_FW_DUMP
#define FM1CORE_FW_DUMP() ((void)0)
#endif
#ifndef FM1CORE_FW_UI_HOME
#define FM1CORE_FW_UI_HOME() (ui.home != 0)   /* HOME is showing (ui_info, dump) */
#endif
#ifndef FM1CORE_FW_RESTORE_ON
#define FM1CORE_FW_RESTORE_ON() (!(ui_prefs & PREF_RESTORE_OFF))   /* MENU > RESTORE LAST: the autosave is written */
#endif

#define FM1CORE_FW_CAT2(a, b) a##b
#define FM1CORE_FW_CAT(a, b) FM1CORE_FW_CAT2(a, b)
#define FM1CORE_FW(n) FM1CORE_FW_CAT(FM1CORE_FW_CAT(fm1core_, FM1CORE_FW_P), FM1CORE_FW_CAT(_, n))

/* ---- the glue's state (hidden: reached only through the descriptor) ---- */
fm1panel_t FM1CORE_FW(panel);
#define FM1CORE_PANEL (&FM1CORE_FW(panel))
static uint8_t fm1core_fw_ready;          /* 1 after the power-on (the params map reads it) */
static uint32_t fm1core_fw_ms;            /* the device ms tick() gave */

/* ------------------------------------------------------------- flash --- */
/* the image is the web unit's nor[] (web_nor): erased at power-on, then the staged bytes (ABI 2) or a file. A file
 * is read at power-on and rewritten whole after a frame / idle in which the firmware wrote (web_flash_writes moved)
 * and at shutdown: the image is RAM, the file a copy (an erase or a program is never torn into it) */
static char fm1core_fw_path[1024];        /* "" RAM only, FM1CORE_FLASH_STAGED, or the file */
static uint32_t fm1core_fw_file_writes;   /* web_flash_writes when the file was last written */
static void fm1core_fw_file_write(void)
{
    FILE *f;
    if (!fm1core_fw_path[0] || !strcmp(fm1core_fw_path, FM1CORE_FLASH_STAGED) ||
        fm1core_fw_file_writes == web_flash_writes)
        return;
    f = fopen(fm1core_fw_path, "wb");
    if (f) {
        fwrite(nor, 1, sizeof nor, f);
        fclose(f);
    }
    fm1core_fw_file_writes = web_flash_writes;
}
static void fm1core_fw_flash_open(void)  /* the power-on */
{
    web_nor_erase();
    if (!strcmp(fm1core_fw_path, FM1CORE_FLASH_STAGED)) {
        uint32_t n = fm1nor_stage_len();
        if (n > sizeof nor)
            n = sizeof nor;
        if (n && fm1nor_stage_bytes())
            memcpy(nor, fm1nor_stage_bytes(), n);
    } else if (fm1core_fw_path[0]) {
        FILE *f = fopen(fm1core_fw_path, "rb");
        if (f) {
            size_t got = fread(nor, 1, sizeof nor, f);
            (void)got;                    /* (short: the rest stays erased) */
            fclose(f);
        }
    }
    fm1core_fw_file_writes = web_flash_writes;
}
static void fm1core_fw_cleanup(void)     /* exit / shutdown: the file current, later writes RAM only */
{
    fm1core_fw_file_write();
    fm1core_fw_path[0] = 0;
}

/* ---------------------------------------------------------- boot guard --- */
#if FM1CORE_FW_BOOTGUARD
/* main.c fm1_cstart: a run that died before its 30 s mark leaves bootguard.pending in .noinit; the next reset counts
 * it, and the second failed boot in a row enters UBOOT instead of the firmware (one failed boot: no LED sweep and no
 * autosave restore, main.c boot_leds' boot_clean). reason and stage are accepted and ignored (no reset reasons here) */
static int fm1core_fw_guard_failed = -1;  /* boot_options: the failed boots counted before this reset; -1 a clean record */
static int fm1core_fw_guard(void)        /* 1: UBOOT (the core halts) */
{
    if (fm1core_fw_guard_failed >= 0) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = (uint32_t)fm1core_fw_guard_failed;
        bootguard.pending = 1;            /* the last run died within 30 s */
    } else {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = bootguard.pending = 0;
        printf("boot: UBOOT (two failed boots in a row)\n");
        fm1core_exit(3);                  /* (the device: fm1_enter_uboot) */
        return 1;
    }
    return 0;
}
#endif

/* -------------------------------------------------------------- UBOOT --- */
/* main.c's loop: OCT- + OCT+ held 5 s (a countdown from 2 s, letting go cancels it), and the UBOOT SysEx */
static uint32_t fm1core_fw_uboot_t0;
static void fm1core_fw_uboot_screen(const char *s)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 110, 240, &AF_M, s, T_THEME, 1);
}
static int fm1core_fw_uboot(void)        /* 1: UBOOT (the core halts) */
{
    const uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    if (!web_booted || web_boot_ms) {     /* (the loop has not started: main.c's t0 is set by its first pass) */
        fm1core_fw_uboot_t0 = fm1_ms;
        return 0;
    }
#if FM1CORE_FW_BOOTGUARD
    if (fm1_ms > 30000u && bootguard.pending)
        bootguard.pending = bootguard.failed = 0;   /* a crash or hang in the first 30 s counts */
#endif
    if ((fm1_in.buttons & both) != both) {
        if (ui.uboot) {
            ui.uboot = 0;
            ui.force = 1;
            ui_message("UPDATE CANCELLED");
        }
        fm1core_fw_uboot_t0 = fm1_ms;
    } else if (fm1_ms - fm1core_fw_uboot_t0 > 2000u && fm1_ms - fm1core_fw_uboot_t0 <= 5000u) {
        uint32_t left = (5000u - (fm1_ms - fm1core_fw_uboot_t0) + 999u) / 1000u;
        if (left != ui.uboot) {
            ui.uboot = (uint8_t)left;
            ui.force = 1;
        }
    } else if (fm1_ms - fm1core_fw_uboot_t0 > 5000u) {
        fm1core_fw_uboot_screen("UBOOT");
#if FM1CORE_FW_BOOTGUARD
        bootguard.pending = 0;            /* intentional reset: not a failed boot */
#endif
        printf("%s: OCT- + OCT+ held 5 s: UBOOT\n", FM1CORE_ID);
        fm1core_exit(3);
        return 1;
    }
    if (usb.uboot_req) {                  /* SysEx F0 22 24 35 7D F7 from the host */
        usb.uboot_req = 0;
        fm1core_fw_uboot_screen("UBOOT (USB)");
#if FM1CORE_FW_BOOTGUARD
        bootguard.pending = 0;
#endif
        printf("%s: UBOOT requested over USB\n", FM1CORE_ID);
        fm1core_exit(3);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------- input --- */
/* the panel into the web unit's input, as its exports take it: held masks (a tap: pressed now, released at the
 * next ms) with the edges the UI takes (fm1_input_edges / fm1_input_note_edges), the detents by matrix encoder
 * (web_enc_steps: the panel's enc[] is already by matrix id, with each role's direction), MASTER as the ADC */
static void fm1core_fw_in(void)
{
    fm1panel_t *p = FM1CORE_PANEL;
    uint32_t b = (p->buttons | fm1p_take_button_taps(p)) & 0x3FFFu;
    uint32_t k = (p->keys | fm1p_take_key_taps(p)) & ((1u << 27) - 1u);
    const uint32_t ob = fm1_in.buttons, ok = fm1_in.notes;
    uint32_t i;
    web_pressed |= b & ~ob;
    web_released |= ob & ~b;
    fm1_in.buttons = b;
    web_btn_labels = fm1p_labels(p, b);
    web_notes_pressed |= k & ~ok;
    fm1_in.notes = k;
    web_key_mask = k;
    for (i = 0; i < FM1P_NENC; i++)
        web_enc_steps[i] += fm1p_take_enc(p, i);
    web_master_adc = (uint32_t)(p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master);
}

/* ------------------------------------------------------------- output --- */
/* the LEDs as levels: lit (fm1_led) 255; the mid level (fm1_led_mid, ~1 lit frame in 8 + the glow) MID; the glow
 * (fm1_led_dim) DIM, or FAINT with MENU > LEDS DIM LO (fm1_led_dim_level); a breath (fm1_led_breath, not over the
 * glow) its level now (dark .. ~60 % of lit and back in ~1.13 s, ~30 % with DIM LO: hal/fm1_input.h's curve, from
 * the simulated clock); the power-on sweep (and 1.2's idle animation) while it runs: hal/fm1_led_anim.h's level per
 * LED (0..64 / 64 of lit, the glow under it). The firmware's own extra planes through FM1CORE_FW_LED. */
#define FM1CORE_FW_DIM_LO_LEVEL 24u
static uint32_t fm1core_fw_br_t0;        /* fm1_ms when a breath started (at its peak) */
static uint8_t fm1core_fw_br_on;
static uint32_t fm1core_fw_breath_level(void)
{
    uint32_t any = 0, c, x, s, pk;
    for (c = 0; c < FM1_NCOL; c++)
        any |= fm1_led_breath[c];
    if (!any) {
        fm1core_fw_br_on = 0;
        return 0;
    }
    if (!fm1core_fw_br_on) {
        fm1core_fw_br_on = 1;
        fm1core_fw_br_t0 = fm1_ms;
    }
    x = (512u + (fm1_ms - fm1core_fw_br_t0) * 10u / 11u) & 1023u;   /* frames into the breath (a frame ~1.1 ms) */
    x = x * 512u / 1024u;
    x = x & 256u ? 511u - x : x;                  /* a triangle 0..255 */
    s = x * x * (765u - 2u * x) / 65025u;         /* smoothstep, 0..255 */
    s = s * s / 255u;                             /* squared */
    pk = web_dim_lo ? 77u : 154u;                 /* FM1_LED_BREATH_PK(_LO), /256 of lit */
    return s * pk / 256u;
}
static uint32_t fm1core_fw_led_level(uint32_t c, uint32_t r, uint32_t breath)
{
    const uint32_t bit = 1u << r;
    uint32_t lv = 0;
    if (fm1_led[c] & bit)
        return FM1P_LED_LIT;
    if (fm1_led_dim[c] & bit)
        lv = web_dim_lo ? FM1CORE_FW_DIM_LO_LEVEL : FM1P_LED_DIM;
    else if ((fm1_led_breath[c] & bit) && breath > lv)
        lv = breath;
    if ((fm1_led_mid[c] & bit) && lv < FM1P_LED_MID)
        lv = FM1P_LED_MID;
    FM1CORE_FW_LED(c, r, lv);
    return lv > 255u ? 255u : lv;
}
static void fm1core_fw_out(void)
{
    fm1panel_t *p = FM1CORE_PANEL;
    uint32_t c, l;
    const uint32_t br = fm1core_fw_breath_level();
    const int anim = web_anim_run != 0;            /* (read, not fm1_led_anim_on(): that one may end it) */
    const uint32_t f = anim ? web_anim_frame() : 0;
    for (c = 0; c < FM1_NCOL; c++)
        for (l = 0; l < FM1P_NLINE; l++) {
            uint32_t lv;
            if (anim && FM1_KEYMAP[l + 1u][c] >= 0) {
                uint32_t a;
#if FM1CORE_FW_ANIM_IDLE
                a = (web_anim_idle ? fm1_idle_level : fm1_anim_level)(f, (uint32_t)FM1_KEYMAP[l + 1u][c], web_anim_glow);
#else
                a = fm1_anim_level(f, (uint32_t)FM1_KEYMAP[l + 1u][c], web_anim_glow);
#endif
                lv = (a & 0x7Fu) * 255u / 64u;
                if ((a & 0x80u) && lv < FM1P_LED_DIM)
                    lv = FM1P_LED_DIM;
                if (lv > 255u)
                    lv = 255u;
            } else {
                lv = fm1core_fw_led_level(c, l + 1u, br);
            }
            p->led[c][l] = (uint8_t)lv;
        }
    p->led_play_green = p->led[LED_PLAY_GREEN >> 3][(LED_PLAY_GREEN & 7u) - 1u];
    fm1p_lcd_out(p, web_fb, web_draws);
    if (fm1core_fw_file_writes != web_flash_writes)
        fm1core_fw_file_write();
}

/* ------------------------------------------------------- entry points --- */
int FM1CORE_FW(halted)(void) { return fm1core_halted(); }
void FM1CORE_FW(shutdown)(void) { fm1core_run_exit(); }

void FM1CORE_FW(options)(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    const char *p = no_flash || (headless && !flash_path) ? "" : flash_path ? flash_path : FM1CORE_FW_DEFAULT_FLASH;
    (void)save_on_exit;                   /* (every write reaches the image at once; shutdown flushes the file) */
    snprintf(fm1core_fw_path, sizeof fm1core_fw_path, "%s", p);
    fm1core_exit_cleanup = fm1core_fw_cleanup;
}
int FM1CORE_FW(boot_options)(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
#if FM1CORE_FW_BOOTGUARD
    fm1core_fw_guard_failed = fail >= 0 ? fail : -1;
#else
    (void)fail;
#endif
    return 1;
}

/* at shutdown (the core's exit handlers): a settings change the firmware queued is written (settings_poll saves only
 * while the transport is stopped), and the file flushed (fm1core_fw_cleanup) */
static void fm1core_fw_at_exit(void)
{
    if (persist_pending)
        settings_poll();
}

void FM1CORE_FW(init)(int demo)
{
    fm1panel_t *p = FM1CORE_PANEL;
    uint32_t i;
    (void)demo;
    fm1core_exit_cleanup = fm1core_fw_cleanup;
#if FM1CORE_FW_BOOTGUARD
    if (fm1core_fw_guard())
        return;
#endif
    fm1core_fw_flash_open();
    if (p->master > 0)                    /* (0: the web unit's default, 700) */
        web_master_adc = (uint32_t)(p->master > 1023 ? 1023 : p->master);
    FM1CORE_FW_BEFORE_BOOT();
    web_boot();                           /* web_power_on: main.c fm1_main's boot */
    usb.up = 1;                           /* a host is there: MIDI out flows (usb.c midi_out_event) */
    usb.config = 1;
    fm1core_atexit(fm1core_fw_at_exit);   /* (common/exit.h: the descriptor's shutdown runs it) */
    for (i = 0; i < FM1P_NB; i++)
        p->btn_id[i] = panel.btn[i];
    for (i = 0; i < FM1P_NE - 1u; i++) {
        p->enc_id[i] = panel.enc[i];
        p->enc_dir[i] = (int8_t)(panel.dir[i] < 0 ? -1 : 1);
    }
    p->master = (int32_t)web_master_adc;
    fm1core_fw_in();                      /* (held at power-on: in the unit's input from the start) */
    fm1core_fw_ready = 1;
    p->ready = 1;
    fm1core_fw_out();
}
void FM1CORE_FW(tick)(uint32_t ms) { fm1core_fw_ms = ms; }
static void fm1core_fw_ms_run(void)      /* frame / idle: the input, the device's millisecond, the output */
{
    if (fm1core_halted() || !fm1core_fw_ready)
        return;
    fm1core_fw_in();
    if (fm1core_fw_uboot())
        return;
    web_ms();
    FM1CORE_FW_AFTER_MS();
    if (fm1core_halted())
        return;
    fm1core_fw_out();
}
void FM1CORE_FW(frame)(void) { fm1core_fw_ms_run(); }
void FM1CORE_FW(idle)(void) { fm1core_fw_ms_run(); }
/* the ring web_ms filled, 128 frames a block: abuf's 24-bit (Q15 << OUT_SHIFT) values, exact in its floats */
void FM1CORE_FW(audio)(int16_t *out, uint32_t frames)
{
    uint32_t k;
    if (fm1core_halted() || !fm1core_fw_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    for (k = 0; k < frames; k++) {
        if (ring_r == ring_w) {           /* (never in the ABI's order: web_ms runs ahead of the block due) */
            out[2u * k] = out[2u * k + 1u] = 0;
            continue;
        }
        {
            const uint32_t i = (ring_r++ % RING) * 2u;
            int32_t l = (int32_t)(ring[i] * 8388608.0f) >> OUT_SHIFT, r = (int32_t)(ring[i + 1] * 8388608.0f) >> OUT_SHIFT;
            out[2u * k] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            out[2u * k + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
    }
}
int FM1CORE_FW(midi_in)(uint32_t pkt)
{
    if (fm1core_halted() || !fm1core_fw_ready)
        return 1;                         /* (halted: taken and dropped, never "retry") */
    if (MQ - (mi_w - mi_r) < 8u)          /* (usb.c ep1_take: room kept for TRS MIDI) */
        return 0;
    return midi_enqueue(pkt, 1u) ? 1 : 0;
}
int FM1CORE_FW(midi_out_take)(uint32_t *pkt)
{
    if (fm1core_halted() || mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}
void FM1CORE_FW(dump)(void)
{
    uint32_t c, i;
    printf("%s: fm1_ms %u  buttons %04X notes %07X  master %u  booted %u (loop from %u)\n", FM1CORE_ID,
           (unsigned)fm1_ms, (unsigned)fm1_in.buttons, (unsigned)fm1_in.notes, (unsigned)web_master_adc,
           (unsigned)web_booted, (unsigned)web_boot_ms);
    printf("  leds lit/dim/breath/mid:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X/%02X/%02X", fm1_led[c], fm1_led_dim[c], fm1_led_breath[c], fm1_led_mid[c]);
    printf("%s\n", web_anim_run ? "  (the LED sweep runs)" : "");
    for (i = 0; i < NTRK; i++) {
        uint32_t v, n = 0;
        for (v = 0; v < NVOICE; v++)
            n += trk[i].v[v].active;
        printf("  part %u%s: %s preset %u, voices %u, level %d\n", (unsigned)i + 1u, i == song.sel ? "*" : "",
               ENGINES[trk[i].engine]->name, (unsigned)trk[i].preset, (unsigned)n, (int)trk[i].p[P_LEVEL]);
    }
    printf("  playing %u page %u home %u master_q12 %u cpu %u%%  midi in %u out %u\n", (unsigned)song.playing,
           (unsigned)ui.page, (unsigned)FM1CORE_FW_UI_HOME(), (unsigned)song.master_q12, (unsigned)(song.cpu_q8 * 100u / 256u),
           (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    printf("  flash: %s, %u writes%s\n", fm1core_fw_path[0] ? fm1core_fw_path : "RAM only", (unsigned)web_flash_writes,
           persist_pending ? ", a settings save pending" : "");
    FM1CORE_FW_DUMP();
}
void FM1CORE_FW(ui_info)(char *buf, uint32_t n)   /* the screen the last frame drew */
{
    snprintf(buf, n, "%s page %u '%s' part %u menu %u msg '%s' uboot %u", FM1CORE_FW_UI_HOME() ? "home" : "page",
             (unsigned)ui.page, FM1CORE_FW_UI_HOME() ? "HOME" : cur_page()->title, (unsigned)song.sel + 1u, (unsigned)ui.menu, ui.msg_t ? ui.msg : "",
             (unsigned)ui.uboot);
}
void FM1CORE_FW(stats)(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = shed_count;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}
const int8_t *FM1CORE_FW(keymap)(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2) ---- */
uint8_t *FM1CORE_FW(flash)(void) { return nor; }
uint32_t FM1CORE_FW(flash_dirty)(void) { return web_flash_writes; }
int FM1CORE_FW(flash_stage)(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
/* make the image current: the settings record (what settings_poll would save: palette, LEDS, prefs, favourites ..)
 * if it differs from the flash's, and the autosave (project.c autosave_poll: the music as a project holds it) if it
 * changed since the last one, as soon as nothing plays or sounds (its 10 s quiet time and 60 s gap waived).
 * Projects and user presets are written the moment they are SAVEd. 1: current; 0: the firmware defers a write (the
 * transport runs, a voice sounds, a key is down) */
int FM1CORE_FW(flash_sync)(void)
{
    uint32_t sig;
    int ok = 1;
    if (fm1core_halted() || !fm1core_fw_ready || !flash_ok)
        return 1;
    persist_pending = 1;
    settings_poll();
    if (persist_pending)
        ok = 0;
    sig = autosave_sig();
    if (sig != as.saved && FM1CORE_FW_RESTORE_ON()) {
        if (!autosave_quiet()) {
            ok = 0;                       /* (something plays or sounds: autosave_poll waits for quiet) */
        } else {
            as.seen = sig;                /* (seen, quiet for AS_IDLE_MS, the gap since the last one over) */
            as.poll = fm1_ms - AS_POLL_MS;
            as.t = fm1_ms - AS_IDLE_MS;
            as.last = fm1_ms - AS_GAP_MS - AS_RETRY_MS;
            autosave_poll();
            if (as.saved != sig)
                ok = 0;                   /* (a failed write: tried again at the next sync) */
        }
    }
    fm1core_fw_file_write();
    return ok;
}
