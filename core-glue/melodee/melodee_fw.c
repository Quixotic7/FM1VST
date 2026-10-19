/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the Melodee core: the firmware sources (melodee_firmware.h) and the hooks of ChoralRoot's
 * tools/emu/emu_hooks.h (the ABI's emu_fw_* entry points). Melodee has no emulator: this is the equivalent of
 * ChoralRoot's tools/emu/emu_fw.c, written against Melodee 1.4.3's own power-on and main loop (firmware/src/main.c
 * fm1_cstart / fm1_main), with nothing of the firmware changed.
 *
 * THE MAIN LOOP RUNS ON A COROUTINE (core-glue/common/coro.h). main.c's fm1_main never returns and waits in places:
 * the splash delays (fm1_delay_ms(30), (400)), the frame pacing (`while (fm1_ms - m < 15) ui_input();`) and
 * panel_setup (Calibration: OCT- + OCT+ held at power-on, or the menu's entry), whose loops poll the buttons and
 * knobs and feed the watchdog. me_main below is that loop: the boot's own part runs in emu_fw_init (no waits), the
 * rest on the coroutine, which yields where the device waits (me_delay; fm1_wdt_feed, melodee_hal.h) and is resumed
 * once per device millisecond by frame() and idle(). One resume: the loop's work for that millisecond (a UI frame
 * every 15 ms, else one ui_input pass), as the device does it between its timer ticks. An exit() on it (UBOOT) parks
 * it: it never runs again (common/exit.h fm1core_exit_park). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fm1core.h"                      /* the panel's constants (EMU_*); before hostsim's __attribute__ */
#include "panel.h"                        /* (core-glue/common) fm1p_emu_hal_t */

/* the firmware's own panel state, as ChoralRoot's emulator keeps it (core_melodee.c translates it to the ABI's
 * fm1panel_t) */
typedef fm1p_emu_hal_t emu_hal_t;
emu_hal_t emu_hal;
/* hal/fm1_input.h FM1_KEYMAP: key id at (physical column, packed row bit), -1 = none */
const int8_t emu_keymap[6][EMU_NCOL] = FM1P_KEYMAP_INIT;

#include "melodee_firmware.h"

/* ------------------------------------------------------------- flash --- */
static uint8_t emu_save_on_exit;
void emu_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    emu_save_on_exit = (uint8_t)(save_on_exit != 0);
    /* RAM only (a fresh flash each run) when no_flash or headless without a path; FM1CORE_FLASH_STAGED: the stage */
    fm1nor_options(flash_path, no_flash, headless, "build/melodee/flash.bin");
}

/* the boot-loop guard: the firmware's own bootguard record (core.h, .noinit on the device), as main.c fm1_cstart
 * counts it: a run that died before its 30 s mark left pending set; the next reset counts it, and the second failed
 * boot in a row enters UBOOT (the PC tool's mode) instead of the firmware. project_boot reads it too (a failed boot
 * skips the BOOT project). Melodee has no reset reasons or SAFE MODE: reason and stage are accepted and ignored.
 * fail: the failed boots counted before this reset (< 0: a clean record) */
int emu_fw_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    bootguard.magic = BOOTGUARD_MAGIC;
    if (fail >= 0) {
        bootguard.failed = (uint32_t)fail;
        bootguard.pending = 1;                    /* the last run died within 30 s */
    } else {
        bootguard.failed = bootguard.pending = 0;
    }
    return 1;
}
static int emu_boot_guard(void)                   /* main.c fm1_cstart; 1: UBOOT (halted) */
{
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        printf("boot: UBOOT (two failed boots in a row)\n");
        fflush(stdout);
        exit(3);                                  /* (the device: fm1_enter_uboot) */
        return 1;
    }
    return 0;
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

/* main.c melodee_init (static there; main.c itself is the device's boot and cannot be built on the host): the parts
 * with their default sounds (TRK_DEF), the sequencers empty. Verbatim (1.4.3). */
static void emu_melodee_init(void)
{
    uint32_t i;
    chain_defaults(&chain_config);
    pattern_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    cz_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        track_defaults_steps(t);              /* (a sound load never touches them) */
        pat_sig[i] = steps_sig(t);            /* (empty: not the user's) */
        pat_last[i] = 0;
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

#define EMU_RELEASE_MS 9u            /* hal/fm1_input.h: a release needs ~9 ms open (FM1_DEB_RELEASE) */
static uint32_t in_btn, in_key;      /* the debounced state the tick delivers */
static uint32_t btn_ms[32], key_ms[32];/* ms each went down */
static uint8_t emu_uboot_left;       /* main.c's OCT- + OCT+ countdown (ui.uboot), for ui_info */
static fm1coro_t *emu_co;            /* the main loop (me_main) */

static void me_delay(uint32_t ms)    /* fm1_delay_ms on the coroutine: until that many device ms have passed */
{
    uint32_t t0 = fm1_ms;
    while (fm1_ms - t0 < ms)
        fm1coro_yield();
}

static void me_uboot(const char *what, const char *log)   /* main.c: the UBOOT screen, then the reset */
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 100, 240, TY_VL, what, T_THEME, 1);
    reset_intended();                             /* (an intentional reset: not a failed boot) */
    printf("melodee: %s\n", log);
    exit(3);                                      /* (the device: usb_detach, fm1_enter_uboot); parks the loop */
}

/* main.c fm1_main from the timer's start: the waits, Calibration if OCT- + OCT+ are held, the GLO values as last
 * used and the BOOT project, the splash's end, then the loop. Its body in main.c's order; built out with the
 * hardware: usb_retry (usb.up is always 1 here: usb_start is the SIE), ua_off_apply (USB audio), ota_session (the
 * update; ed_service and ota_service are in), cdc_task; the battery ADC (song.batt_raw stays 0: usb.config shows
 * external power, ui_draw.c batt_shown) */
static void me_main(void *arg)
{
    int32_t knob = 512 * 16;
    (void)arg;
    me_delay(30);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();                            /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    glo_restore();                                /* CLK TUNE MIDI ROUT as last used, */
    project_boot();                               /* then the BOOT project or the template (theirs win) */
    me_delay(400);
    if (boot_work.state != 2u)
        lcd_fill(0, 0, 240, 240, T_BG);

    for (;;) {
        uint32_t m = fm1_ms;
        pattern_poll();
        if (fm1_ms > 30000u && bootguard.pending) {   /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {   /* the MASTER pot (ADC) through its IIR */
            int32_t a = emu_hal.master;
            uint32_t k10;
            a = a < 0 ? 0 : a > 1023 ? 1023 : a;
            knob += (a * 16 - knob) / 8;
            k10 = (uint32_t)(knob / 16);
            song.master_q12 = (k10 * k10) >> 8;   /* 0 .. ~4096 */
        }
        {   /* OCT- + OCT+ held 5 s: UBOOT; the countdown shows from 2 s, letting go cancels it */
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
                scr_wake_now();
                me_uboot("UBOOT", "OCT- + OCT+ held 5 s: UBOOT");
                return;
            }
            emu_uboot_left = ui.uboot;
        }
        cz_service();
        fm6_service();                            /* DX7 SysEx for FM6, a pending FM6 settings save */
        ed_service();                             /* web editor SysEx (melodee_firmware.h: the managers) */
        ota_service();                            /* M-UPGRADE handshake (its update, usb.ota_req, never starts here) */
        usb.ota_req = 0;
        if (usb.uboot_req) {                      /* SysEx F0 22 24 35 7D F7 from the host: the device resets into
                                                   * the ROM's USB boot for an installer; a core is not updated that
                                                   * way, so the key is ignored: the firmware runs on, nothing written */
            usb.uboot_req = 0;
            printf("melodee: UBOOT requested over USB: ignored (no update inside a core)\n");
        }
        melodee_dbg.ui_frames++;
        melodee_dbg.page = ui.page;
        melodee_dbg.home = ui.home;
        melodee_dbg.stage = 1;
        ui_input();
        glo_poll();                               /* a kept GLO value changed: the settings, later */
        settings_poll();                          /* queued settings save: only while stopped */
        melodee_dbg.stage = 2;
        ui_leds();
        ui_draw();
        melodee_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                /* ~60 UI frames/s at most */
            fm1coro_yield();                      /* (the next device ms) */
            ui_input();
            ed_service();                         /* editor replies without waiting for the next frame */
        }
    }
}

void emu_fw_init(int demo)
{
    struct timespec ts;
    uint32_t i;
    (void)demo;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    emu_t0_ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    if (emu_hal.ready == 2u)                      /* simulated time (set before init) */
        emu_sim = 1;
    fm1core_exit_park = fm1coro_park;             /* (an exit() on the loop's coroutine: it is never resumed) */
    fm1sx_in_init(&fm1core_me_sxi, fm1core_me_sx_accept);
    fm1sx_out_reset(&fm1core_me_sxo);

    /* power-on, main.c fm1_cstart + fm1_main's order up to the timer's start: the guard, the default palette, the
     * splash, the flash (persist_boot: settings, projects, pattern banks, user presets, the FM6 / CZ / Prophet
     * banks; a migration draws its progress, boot_work), the settings applied, the voice budget, the panel table,
     * the parts, CPU1, the audio, USB. (dsp_tables_init: hostsim.c's constructor ran it when the module loaded.) */
    if (emu_boot_guard())
        return;
    fm1nor_open();                                /* (common/nor.c: erased, then the stage or the file) */
    settings_init();                              /* a readable default palette before loading flash */
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 70, 240, TY_HR, "MELODEE", T_THEME, 1);
    draw_text_box(0, 140, 240, TY_TX, "Starting up", T_MID, 1);
    lcd_sync();
    persist_boot();
    settings_init();
    vbudget_cpu1 = VBUDGET;                       /* clock_boot: the SPL's 360 MHz (VBUDGET * 360 / 360) */
    if (boot_work.state != 2u) {
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 70, 240, TY_HR, "MELODEE", T_THEME, 1);
        draw_text_box(0, 140, 240, TY_TX, "Multi-engine synth", T_MID, 1);
    }
    if (melodee_dbg.magic != DBG_MAGIC) {
        memset(&melodee_dbg, 0, sizeof melodee_dbg);
        melodee_dbg.magic = DBG_MAGIC;
    }
    melodee_dbg.boots++;
    melodee_dbg.max_us = 0;
    melodee_dbg.in_audio = 0;
    panel_init();
    emu_melodee_init();
    audio_worker_online = 1;                      /* fm1_multicore_start: CPU1 up (its jobs run inline) */
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
    emu_hal.led_play_green = 0;                   /* (Melodee lights no green PLAY LED: column 0, row bit 0 is the
                                                   * encoders' row, never lit) */
    if (!emu_hal.master)
        emu_hal.master = 724;                     /* -> master_q12 2047, as melodee_init's 2048 */
    emu_hal.ready = 1;
    emu_co = fm1coro_new(me_main, NULL, 0);
    if (!emu_co) {
        printf("melodee: no memory for the main loop's stack\n");
        exit(1);
        return;
    }
    fm1coro_resume(emu_co);                       /* (up to its first wait: the 30 ms after the timer's start) */
}

/* the 1 ms timer: what fm1_timer5_irq / fm1_input_tick give the main loop (ChoralRoot's emu_fw.c logic): a press
 * is an edge at once, a release only after the key was held EMU_RELEASE_MS (the debounce's time), a tap shorter
 * than a tick still arrives */
void emu_fw_tick(uint32_t ms)
{
    uint32_t b, k, i, down;
    if (emu_sim)
        emu_sim_ms = ms;
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

static void emu_fw_run(void)                      /* this device ms of the main loop */
{
    if (emu_co && !fm1core_halted())
        fm1coro_resume(emu_co);
}
void emu_fw_frame(void) { emu_fw_run(); }
void emu_fw_idle(void) { emu_fw_run(); }
static void emu_fw_free(void)                     /* (shutdown: the loop's stack) */
{
    if (emu_co)
        fm1coro_free(emu_co);
    emu_co = 0;
}

void emu_fw_audio(int16_t *out, uint32_t frames)  /* the ALNK0 ISR, one half buffer per EMU_BLOCK */
{
    uint32_t f, i;
    for (f = 0; f + HALF_FRAMES <= frames; f += HALF_FRAMES) {
        const int32_t *h;
        if (fm1nor_stall_take()) {                /* a flash erase on the device: IRQs off, the buffer zeroed */
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
    if (MQ - (mi_w - mi_r) < EP1_ROOM - 16u)      /* (usb.c ep1_take: room kept for TRS MIDI) */
        return 0;
    if (fm1sx_in_would_refuse(&fm1core_me_sxi, pkt))
        return 0;                                 /* (a request waits for the main loop: this one after it) */
    midi_in_event(pkt);
    fm1sx_in_pkt(&fm1core_me_sxi, pkt);           /* (and the managers' frame buffer: melodee_firmware.h) */
    return 1;
}

int emu_fw_midi_out_take(uint32_t *pkt)
{
    if (fm1sx_take(&fm1core_me_sxo, pkt))
        return 1;                                 /* (usb.c ep1_tx: SysEx first, never split by notes) */
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
        printf(" %02X/%02X/%02X/%02X", fm1_led[c], fm1_led_dim[0][c], fm1_led_dim[1][c], fm1_led_dim[2][c]);
    printf("  (lit/keys/buttons/breath per column; masks %u %u %u)\n", fm1_led_dim_mask[0], fm1_led_dim_mask[1],
           fm1_led_dim_mask[2]);
    for (i = 0; i < NTRK; i++) {
        uint32_t v, n = 0;
        for (v = 0; v < NVOICE; v++)
            n += trk[i].v[v].active;
        printf("  part %u%s: %s preset %u, voices %u, level %d\n", (unsigned)i + 1u, i == song.sel ? "*" : "",
               ENGINES[eng_idx(trk[i].engine)]->name, (unsigned)trk[i].preset, (unsigned)n, (int)trk[i].p[P_LEVEL]);
    }
    printf("  playing %u bpm %d page %u home %u master_q12 %u  midi in %u out %u  cpu1 %s, %u jobs\n",
           (unsigned)song.playing, (int)song.g[G_BPM], (unsigned)ui.page, (unsigned)ui.home,
           (unsigned)song.master_q12, (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r),
           audio_worker_online ? "online (inline)" : "retired", (unsigned)audio_worker.jobs);
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks)%s\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks(), persist_pending ? ", a settings save pending" : "");
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
