/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the Felucca core: the firmware sources (felucca_firmware.h) and the hooks of ChoralRoot's
 * tools/emu/emu_hooks.h (the ABI's emu_fw_* entry points). Felucca has no emulator: this is the equivalent of
 * ChoralRoot's tools/emu/emu_fw.c, written against Felucca's own power-on and main loop (firmware/src/main.c), with
 * nothing of the firmware changed. Everything Felucca-specific the core relies on is in this file's hook bodies. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fm1core.h"                      /* emu_hooks.h: emu_hal_t, the hooks; before hostsim's __attribute__ */

emu_hal_t emu_hal;
/* hal/fm1_input.h FM1_KEYMAP: key id at (physical column, packed row bit), -1 = none */
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};

#include "felucca_firmware.h"

/* ------------------------------------------------------------- flash --- */
static uint8_t emu_save_on_exit;
void emu_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    emu_save_on_exit = (uint8_t)(save_on_exit != 0);
    if (no_flash || (headless && !flash_path))
        emu_flash_path[0] = 0;                    /* RAM only: a fresh flash each run */
    else
        snprintf(emu_flash_path, sizeof emu_flash_path, "%s", flash_path ? flash_path : "build/felucca/flash.bin");
}

/* the boot-loop guard (main.c fm1_cstart): a run that died before its 30 s mark leaves bootguard.pending in .noinit;
 * the next reset counts it, and the second failed boot in a row enters UBOOT (the PC tool's mode) instead of the
 * firmware. Felucca has no reset reasons or SAFE MODE (ChoralRoot's cr_bootguard.h): reason and stage are
 * accepted and ignored. fail: the failed boots counted before this reset (< 0: a clean record) */
static struct { uint32_t failed, pending; } emu_guard;
int emu_fw_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    if (fail >= 0) {
        emu_guard.failed = (uint32_t)fail;
        emu_guard.pending = 1;                    /* the last run died within 30 s */
    } else {
        emu_guard.failed = emu_guard.pending = 0;
    }
    return 1;
}
static void emu_boot_guard(void)                  /* main.c fm1_cstart */
{
    if (emu_guard.pending)
        emu_guard.failed++;
    emu_guard.pending = 1;
    if (emu_guard.failed >= 2u) {
        emu_guard.failed = emu_guard.pending = 0;
        printf("boot: UBOOT (two failed boots in a row)\n");
        fflush(stdout);
        exit(3);                                  /* (the device: fm1_enter_uboot) */
    }
}

/* at exit (the core's shutdown): a settings change the firmware has queued (settings_poll saves only while the
 * transport is stopped) is written, as it would be once the device stops; save_on_exit: whatever differs */
static void emu_fw_exit(void)
{
    if (emu_save_on_exit)
        persist_pending = 1;
    if (persist_pending)
        settings_poll();
}

/* main.c felucca_init (static there; main.c itself is the device's boot and cannot be built on the host): the parts
 * with their default sounds (TRK_DEF), the sequencers empty. Verbatim. */
static void emu_felucca_init(void)
{
    uint32_t i;
    chain_defaults(&chain_config);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        track_defaults_steps(t);              /* (a sound load never touches them) */
        if (TRK_DEF[i][2])
            load_pat16(t, PATTERNS[TRK_DEF[i][2] - 1u].note, PATTERNS[TRK_DEF[i][2] - 1u].flags);
        pat_sig[i] = steps_sig(t);            /* a default pattern, not the user's */
        pat_last[i] = TRK_DEF[i][2];
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

#define EMU_RELEASE_MS 9u            /* hal/fm1_input.h: a release needs ~9 ms open (FM1_DEB_RELEASE) */
#define EMU_SPLASH_MS 430u           /* main.c: the splash, fm1_delay_ms(30) + fm1_delay_ms(400), then the loop */
static uint32_t in_btn, in_key;      /* the debounced state the tick delivers */
static uint32_t btn_ms[32], key_ms[32];/* ms each went down */
static int32_t master_knob = 512 * 16;
static uint8_t emu_looping;          /* the splash is over: the main loop runs */

void emu_fw_init(int demo)
{
    struct timespec ts;
    uint32_t i;
    (void)demo;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    emu_t0_ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    if (emu_hal.ready == 2u)                      /* simulated time (set before init) */
        emu_sim = 1;

    /* power-on, main.c fm1_cstart + fm1_main's order: the guard, the flash (persist_boot: user sample slots,
     * settings, projects, user presets), the settings applied, the splash, the panel table, the parts, the audio,
     * USB */
    emu_boot_guard();
    emu_flash_open();
    persist_boot();
    settings_init();
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 94, 240, &AF_L, "FELUCCA", T_THEME, 1);
    draw_text_box(0, 134, 240, &AF_S, "MULTI-ENGINE SYNTH", T_MID, 1);
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    panel_init();
    emu_felucca_init();
    audio_init();
    atexit(emu_fw_exit);
    usb.up = 1;                                   /* a host is there: MIDI out flows (usb.c midi_out_event) */
    usb.config = 1;
    for (i = 0; i < EMU_NB; i++)
        emu_hal.btn_id[i] = panel.btn[i];
    for (i = 0; i < EMU_NE - 1u; i++) {
        emu_hal.enc_id[i] = panel.enc[i];
        emu_hal.enc_dir[i] = panel.dir[i];
    }
    emu_hal.led_play_green = LED_PLAY_GREEN;
    if (!emu_hal.master)
        emu_hal.master = 724;                     /* -> master_q12 2047, as felucca_init's 2048 */
    master_knob = emu_hal.master * 16;
    emu_looping = 0;
    emu_hal.ready = 1;
}

/* the 1 ms timer: what fm1_timer5_irq / fm1_input_tick give the main loop (ChoralRoot's emu_fw.c logic): a press
 * is an edge at once, a release only after the key was held EMU_RELEASE_MS (the debounce's time), a tap shorter
 * than a tick still arrives */
void emu_fw_tick(uint32_t ms)
{
    uint32_t b, k, i, down;
    if (emu_sim)
        emu_sim_us = ms * 1000u;
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    b = emu_hal.buttons | emu_hal.buttons_tap;
    emu_hal.buttons_tap = 0;
    k = emu_hal.keys | emu_hal.keys_tap;
    emu_hal.keys_tap = 0;
    down = b & ~in_btn;
    for (i = 0; i < EMU_NBTN; i++) {
        uint32_t m = 1u << i;
        if (down & m)
            btn_ms[i] = ms;
        else if ((in_btn & m) && !(b & m) && ms - btn_ms[i] < EMU_RELEASE_MS)
            b |= m;                               /* (held at least the debounce time) */
    }
    in_btn = b;
    host_pressed |= down;
    down = k & ~in_key;
    for (i = 0; i < EMU_NKEY; i++) {
        uint32_t m = 1u << i;
        if (down & m)
            key_ms[i] = ms;
        else if ((in_key & m) && !(k & m) && ms - key_ms[i] < EMU_RELEASE_MS)
            k |= m;
    }
    in_key = k;
    host_notes |= down;
    fm1_in.buttons = in_btn;
    fm1_in.notes = in_key;
    for (i = 0; i < EMU_NENC; i++) {
        int32_t s = emu_hal.enc[i];
        emu_hal.enc[i] = 0;
        host_enc[i] += s;
    }
}

static void master_pot(void)                      /* main.c: the MASTER pot (ADC) through its IIR */
{
    int32_t a = emu_hal.master;
    uint32_t k10;
    a = a < 0 ? 0 : a > 1023 ? 1023 : a;
    master_knob += (a * 16 - master_knob) / 8;
    k10 = (uint32_t)(master_knob / 16);
    song.master_q12 = (k10 * k10) >> 8;
}

static uint8_t emu_uboot_left;                    /* main.c's OCT- + OCT+ countdown (ui.uboot) */
void emu_fw_frame(void)                           /* main.c fm1_main's loop body */
{
    if (!emu_looping) {                           /* the splash (main.c: the delays before the loop) */
        if (fm1_ms < EMU_SPLASH_MS)
            return;
        lcd_fill(0, 0, 240, 240, T_BG);
        emu_looping = 1;
    }
    fm1_wdt_feed();
    /* (usb_retry: usb.up is always 1 here, and its usb_start is the SIE: not built) */
    if (fm1_ms > 30000u && emu_guard.pending) {   /* a crash or hang in the first 30 s counts */
        emu_guard.pending = 0;
        emu_guard.failed = 0;
    }
    master_pot();
    {   /* OCT- + OCT+ held 5 s: UBOOT (main.c); the countdown shows from 2 s, letting go cancels it */
        static uint32_t t0;
        uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
        if ((fm1_in.buttons & both) != both) {
            if (ui.uboot) {
                ui.uboot = 0;
                ui.force = 1;
                ui_message("UPDATE CANCELLED");
            }
            t0 = fm1_ms;
        } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
            uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
            if (left != ui.uboot) {
                ui.uboot = (uint8_t)left;
                ui.force = 1;
            }
        } else if (fm1_ms - t0 > 5000u) {
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
            emu_guard.pending = 0;                /* intentional reset: not a failed boot */
            printf("felucca: OCT- + OCT+ held 5 s: UBOOT\n");
            exit(3);
            return;
        }
        emu_uboot_left = ui.uboot;
    }
    if (usb.uboot_req) {                          /* SysEx F0 22 24 35 7D F7 from the host */
        usb.uboot_req = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 110, 240, &AF_M, "UBOOT (USB)", T_THEME, 1);
        emu_guard.pending = 0;
        printf("felucca: UBOOT requested over USB\n");
        exit(3);
        return;
    }
    felucca_dbg.ui_frames++;
    felucca_dbg.page = ui.page;
    felucca_dbg.home = ui.home;
    felucca_dbg.stage = 1;
    ui_input();
    settings_poll();                              /* queued settings save: only while stopped */
    felucca_dbg.stage = 2;
    ui_leds();
    ui_draw();
    felucca_dbg.stage = 9;
}

void emu_fw_idle(void)                            /* main.c: the input scan while it waits for the next frame */
{
    if (emu_looping)
        ui_input();
}

void emu_fw_audio(int16_t *out, uint32_t frames)  /* the ALNK0 ISR, one half buffer per EMU_BLOCK */
{
    uint32_t f, i;
    for (f = 0; f + HALF_FRAMES <= frames; f += HALF_FRAMES) {
        const int32_t *h;
        if (emu_stall_blocks) {                   /* a flash erase on the device: IRQs off, the buffer zeroed */
            emu_stall_blocks--;
            emu_stall_blocks_all++;
            for (i = 0; i < HALF_WORDS; i++)
                out[2u * f + i] = 0;
            continue;
        }
        emu_half ^= 1u;
        fm1_alnk0_irq();
        h = &abuf[emu_half * HALF_WORDS];
        for (i = 0; i < HALF_WORDS; i++) {        /* 24-bit (Q15 << OUT_SHIFT) -> Q15 = 0 dBFS */
            int32_t v = h[i] >> OUT_SHIFT;
            out[2u * f + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

int emu_fw_midi_in(uint32_t pkt)
{
    if (MQ - (mi_w - mi_r) < 8u)                  /* (usb.c ep1_take: room kept for TRS MIDI) */
        return 0;
    midi_in_event(pkt);
    return 1;
}

int emu_fw_midi_out_take(uint32_t *pkt)
{
    if (mo_r == mo_w)                             /* usb.c ep1_tx */
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}

void emu_fw_dump(void)
{
    uint32_t c, i;
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  pending edges %04X / notes %07X\n",
           (unsigned)fm1_in.notes, (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)host_pressed,
           (unsigned)host_notes);
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X", fm1_led[c], fm1_led_dim[c]);
    printf("  (lit/dim per column)\n");
    for (i = 0; i < NTRK; i++) {
        uint32_t v, n = 0;
        for (v = 0; v < NVOICE; v++)
            n += trk[i].v[v].active;
        printf("  part %u%s: %s preset %u, voices %u, level %d\n", (unsigned)i + 1u, i == song.sel ? "*" : "",
               ENGINES[trk[i].engine]->name, (unsigned)trk[i].preset, (unsigned)n, (int)trk[i].p[P_LEVEL]);
    }
    printf("  playing %u bpm %d page %u home %u master_q12 %u cpu %u%%  midi in %u out %u\n", (unsigned)song.playing,
           (int)song.g[G_BPM], (unsigned)ui.page, (unsigned)ui.home, (unsigned)song.master_q12,
           (unsigned)(song.cpu_q8 * 100u / 256u), (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks)%s\n",
           emu_flash_path[0] ? emu_flash_path : "RAM only", (unsigned)emu_flash_writes, (unsigned)emu_stalls,
           (unsigned)emu_stall_blocks_all, persist_pending ? ", a settings save pending" : "");
}

void emu_fw_ui_info(char *buf, uint32_t n)        /* the screen the last frame drew */
{
    snprintf(buf, n, "%s page %u '%s' part %u menu %u msg '%s' uboot %u", ui.home ? "home" : "page", (unsigned)ui.page,
             ui.home ? "HOME" : cur_page()->title, (unsigned)song.sel + 1u, (unsigned)ui.menu, ui.msg_t ? ui.msg : "",
             (unsigned)emu_uboot_left);
}

void emu_fw_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = shed_count;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}
