/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the SLOOP-family cores: the firmware (sloop_firmware.h) and the device's power-on and main
 * loop (firmware/src/main.c fm1_main) re-done on the ABI's clock. None of these firmwares has an emulator, and
 * main.c is the device's boot (it never returns): this file is its host twin, as core-glue/felucca/felucca_fw.c is
 * for Felucca 1.0.1, with nothing of the firmware changed:
 *   init   fm1_main's power-on in its order: the boot guard (core.h's bootguard, bootguard.h's rules), the flash
 *          (persist_boot), settings_init, the splash, panel_init, main.c's felucca_init (verbatim, the firmware's
 *          own), cpu1_boot (inline), audio_init
 *   frame  the splash's time (main.c's fm1_delay_ms(30) + (900): sloopDX's 32 algorithm frames, ORBIT's logo
 *          guard), then the main loop's body every 15 ms: the 30 s healthy mark, the battery and MASTER pots,
 *          OCT- + OCT+ held 5 s (UBOOT), the USB UBOOT request, ui_input, ui_leds, ui_draw, autosave_tick,
 *          sections_flush (sloopDX: midi_pc_take)
 *   idle   the loop's wait for the next frame: ui_input
 *   tick   TIMER5's ms and the matrix scan's debounce (a press at once, a release after ~9 ms open)
 *   audio  the ALNK0 ISR, one 256-frame half per two ABI blocks (common/fifo), silent while a flash erase stalls
 * Not done (hardware): USB enumeration, the TRS UART, the web editor's SysEx and the update modes (FELUCCA_OTA),
 * the panel calibration at power-on (OCT- + OCT+ held while switching on: panel_setup waits for presses inside one
 * call; the panel table is the plugin's own wiring, and a stored table is still read from the settings). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) */
#include "ticks.h"
#include "fifo.h"

#include "sloop_firmware.h"

/* ------------------------------------------------------------- flash --- */
static uint8_t fm1core_sl_save_on_exit;
void fm1core_sl_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_sl_save_on_exit = (uint8_t)(save_on_exit != 0);
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

/* the boot guard (core.h bootguard, bootguard.h; main.c fm1_cstart): a run that died before its 30 s mark leaves
 * pending set in .noinit; the next reset counts it, and the second failed boot in a row enters the recovery
 * updater (an OTA build; UBOOT otherwise) instead of the firmware: exit(3) here. reason and stage are accepted and
 * ignored (no reset reasons): every power-on of a core counts, as the device's warm resets do. */
static uint8_t fm1core_sl_guard_set;
int fm1core_sl_fw_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    bootguard_clear(&bootguard);
    if (fail >= 0) {
        bootguard.failed = (uint32_t)fail;
        bootguard.pending = 1;                    /* the last run died within 30 s */
    }
    fm1core_sl_guard_set = 1;
    return 1;
}

/* at exit (the core's shutdown): save_on_exit: what the firmware keeps in RAM until quiet (the live sections, the
 * song, the working project, the settings) is written, as a UBOOT entry writes it (main.c persist_flush_now) */
static void fm1core_sl_fw_exit(void)
{
    if (fm1core_sl_save_on_exit && flash_ok && !song.playing) {
        persist_flush_now();
        settings_save();
    }
}

/* ----------------------------------------------------------- power-on --- */
#define FM1CORE_SL_RELEASE_MS 9u          /* fm1_input.h: a release needs FM1_DEB_RELEASE frames open (~9 ms) */
#if FM1CORE_SL_FW == FM1CORE_SL_DX
#define FM1CORE_SL_SPLASH_MS (30u + 32u * 70u + 800u)   /* splash.c sloopdx_splash_run: 32 algorithms, then 0.8 s */
#else
#define FM1CORE_SL_SPLASH_MS (30u + 900u) /* main.c: fm1_delay_ms(30), the calibration check, fm1_delay_ms(900) */
#endif
static uint32_t fm1core_sl_in_btn, fm1core_sl_in_key;         /* the debounced state the tick delivers */
static uint32_t fm1core_sl_btn_ms[32], fm1core_sl_key_ms[32];  /* ms each went down */
static int32_t fm1core_sl_knob = 512 * 16;                     /* main.c's MASTER IIR */
static uint32_t fm1core_sl_healthy_since;
static uint8_t fm1core_sl_looping;        /* the splash is over: the main loop runs */
static uint8_t fm1core_sl_uboot_shown;    /* main.c's OCT- + OCT+ countdown */
static uint32_t fm1core_sl_uboot_t0;
static uint32_t fm1core_sl_frames;
#if FM1CORE_SL_FW == FM1CORE_SL_DX
static int32_t fm1core_sl_alg = -1;       /* the splash's algorithm frame shown */
#endif
static fm1fifo_t fm1core_sl_fifo;

static void fm1core_sl_render(void *ctx, void *buf, uint32_t frames)   /* the ALNK0 ISR: one half */
{
    (void)ctx;
    (void)frames;
    fm1core_sl_half ^= 1u;
    fm1_alnk0_irq();
    memcpy(buf, &abuf[fm1core_sl_half * HALF_WORDS], HALF_WORDS * sizeof abuf[0]);
}

void fm1core_sl_fw_init(int demo)
{
    uint32_t mode;
    (void)demo;
    fm1t_set_ms(0);
    fm1_ms = 0;
    /* main.c fm1_cstart: the guard (a power-on clears it: none staged by boot_options) */
    if (!fm1core_sl_guard_set)
        bootguard_clear(&bootguard);
    mode = bootguard_begin(&bootguard);
    if (mode != BOOT_NORMAL) {
        printf("%s: boot guard: %s\n", FM1CORE_ID, mode == BOOT_ROM ? "ROM (UBOOT)" : "two failed boots: recovery");
        exit(3);                                  /* (an OTA build runs recovery_main, the USB updater; UBOOT here) */
        return;
    }
    /* main.c fm1_main's power-on */
    fm1nor_open();                                /* (common/nor.c: erased, then the stage or the file) */
    persist_boot();
    settings_init();
    lcd_init();
#if FM1CORE_SL_FW == FM1CORE_SL_DX
    sloopdx_splash();
#elif FM1CORE_SL_FW == FM1CORE_SL_ORBIT
    orbit_splash();
    boot_logo_ms = fm1_ms;
#else
    sloop_splash();                               /* (No Man Sky: its boot screen, ui_nms.c) */
#endif
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();                               /* (main.c's, verbatim) */
#if FM1CORE_SL_HAS_CPU1
    cpu1_boot();
#endif
    audio_init();
    fm1core_sl_audio_stopped = 0;
    usb.up = 1;                                   /* a host is there: MIDI out flows (usb.c midi_out_event) */
    usb.config = 1;
    atexit(fm1core_sl_fw_exit);
    fm1fifo_init(&fm1core_sl_fifo, HALF_FRAMES, 2, FM1FIFO_S32, OUT_SHIFT, fm1core_sl_render, NULL);
    fm1core_sl_looping = 0;
    fm1core_sl_frames = 0;
}

/* the panel table (panel.c's, from the settings or PANEL_DEFAULT). The arrays' own sizes, not NB / NE: No Man Sky's
 * ui_nms.c redefines NB as a colour (#define NB RGB(0x00, 0x6E, 0xA0)), which read past panel.btn here */
void fm1core_sl_fw_panel_table(uint8_t *btn_id, uint8_t *enc_id, int8_t *enc_dir)
{
    uint32_t i;
    for (i = 0; i < sizeof panel.btn; i++)
        btn_id[i] = panel.btn[i];
    for (i = 0; i < sizeof panel.enc; i++) {
        enc_id[i] = panel.enc[i];
        enc_dir[i] = panel.dir[i];
    }
}

/* ------------------------------------------------------- the 1 ms timer --- */
/* what TIMER5 (fm1_input_tick, the debounce) delivers to the main loop: a press is an edge at once, a release only
 * after the key was held FM1CORE_SL_RELEASE_MS (the debounce's time), a tap shorter than a tick still arrives; the
 * buttons held over MIDI (fm1_remote_btn) are closed for the debounce, as fm1__key reads them */
void fm1core_sl_fw_tick(uint32_t ms, uint32_t buttons, uint32_t keys, const int32_t *enc)
{
    uint32_t b, k, i, down;
    fm1t_set_ms(ms);
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    b = buttons | fm1_remote_btn;
    k = keys;
    down = b & ~fm1core_sl_in_btn;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_sl_btn_ms[i] = ms;
        else if ((fm1core_sl_in_btn & m) && !(b & m) && ms - fm1core_sl_btn_ms[i] < FM1CORE_SL_RELEASE_MS)
            b |= m;                               /* (held at least the debounce time) */
    }
    fm1_in.released |= fm1core_sl_in_btn & ~b;
    fm1core_sl_in_btn = b;
    fm1_in.pressed |= down;
    down = k & ~fm1core_sl_in_key;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_sl_key_ms[i] = ms;
        else if ((fm1core_sl_in_key & m) && !(k & m) && ms - fm1core_sl_key_ms[i] < FM1CORE_SL_RELEASE_MS)
            k |= m;
    }
    fm1core_sl_in_key = k;
    fm1_in.notes_pressed |= down;
    fm1_in.buttons = fm1core_sl_in_btn;
    fm1_in.notes = fm1core_sl_in_key;
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_steps[i] = (int16_t)(fm1_in.enc_steps[i] + enc[i]);
    fm1_in.frames++;
}

/* ------------------------------------------------------- the main loop --- */
static void fm1core_sl_master_pot(void)           /* main.c: the battery and MASTER pots through their IIRs */
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT), a = fm1_adc_read(FM1_ADC_MASTER);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    if (a >= 0) {
        uint32_t k10;
        fm1core_sl_knob += (a * 16 - fm1core_sl_knob) / 8;
        k10 = (uint32_t)(fm1core_sl_knob / 16);
#if FM1CORE_SL_FW != FM1CORE_SL_ORBIT
        if (ed_master) {                          /* (2.7) the LIVE page's level, until the knob turns */
            if (ed_master_k == 0xFFFFu)
                ed_master_k = (uint16_t)k10;
            if (k10 + 48u < ed_master_k || k10 > ed_master_k + 48u)
                ed_master = 0;
            else
                k10 = (ed_master - 1u) * 1023u / 126u;
        }
#endif
        song.master_q12 = (k10 * k10) >> 8;
    }
}

/* the splash's time (main.c's delays; sloopDX: splash.c sloopdx_splash_run's frames, one per 70 ms) */
static int fm1core_sl_splash(void)
{
    if (fm1core_sl_looping)
        return 0;
#if FM1CORE_SL_FW == FM1CORE_SL_DX
    if (fm1_ms >= 30u) {
        uint32_t a = (fm1_ms - 30u) / 70u;
        int32_t want = a < 32u ? (int32_t)a : 0;
        if (want != fm1core_sl_alg) {
            sloopdx_alg_frame((uint32_t)want);
            fm1core_sl_alg = want;
        }
    }
#endif
    if (fm1_ms < FM1CORE_SL_SPLASH_MS)
        return 1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    fm1core_sl_healthy_since = fm1_ms;
    fm1core_sl_looping = 1;
    return 0;
}

void fm1core_sl_fw_frame(void)                    /* main.c fm1_main's loop body (one pass per 15 ms) */
{
    if (fm1core_sl_splash())
        return;
    fm1_wdt_feed();
    if ((uint32_t)(fm1_ms - fm1core_sl_healthy_since) >= 30000u && bootguard.pending)
        bootguard_clear(&bootguard);              /* 30 s of this boot */
    fm1core_sl_master_pot();
    {   /* OCT- + OCT+ held 5 s, stopped: UBOOT; a countdown from 2 s, letting go cancels it */
        const uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
        if ((fm1_in.buttons & both) != both || song.playing) {
            if (fm1core_sl_uboot_shown)
                ui_say("UPDATE MODE ", "CANCELLED");
            fm1core_sl_uboot_shown = 0;
            fm1core_sl_uboot_t0 = fm1_ms;
        } else if (fm1_ms - fm1core_sl_uboot_t0 > 2000u && fm1_ms - fm1core_sl_uboot_t0 <= 5000u) {
            uint32_t left = (5000u - (fm1_ms - fm1core_sl_uboot_t0) + 999u) / 1000u;
            if (left != fm1core_sl_uboot_shown) {
                char d[4] = {(char)('0' + left), '.', '.', 0};
                ui_say("UPDATE MODE IN ", d);
                fm1core_sl_uboot_shown = (uint8_t)left;
            }
        } else if (fm1_ms - fm1core_sl_uboot_t0 > 5000u) {
            fm1_audio_stop();
            persist_flush_now();                  /* (live sections, song, project: RAM-only so far) */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
            bootguard.pending = 0;                /* intentional reset: not a failed boot */
            printf("%s: OCT- + OCT+ held 5 s: UBOOT\n", FM1CORE_ID);
            fm1_enter_uboot();
            return;
        }
    }
    if (usb.uboot_req) {                          /* SysEx F0 22 24 35 7D F7 from the host */
        fm1_audio_stop();
        persist_flush_now();
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
        bootguard.pending = 0;
        printf("%s: UBOOT requested over USB\n", FM1CORE_ID);
        fm1_enter_uboot();
        return;
    }
#if FM1CORE_SL_FW == FM1CORE_SL_DX
    midi_pc_take();                               /* a MIDI Program Change: its preset */
#endif
    felucca_dbg.ui_frames++;
    felucca_dbg.page = ui.page;
    felucca_dbg.home = ui.home;
    felucca_dbg.stage = 1;
    ui_input();
    felucca_dbg.stage = 2;
    ui_leds();
#if FM1CORE_SL_FW == FM1CORE_SL_ORBIT
    if (fm1_ms - boot_logo_ms >= 750u)
#endif
        ui_draw();
    felucca_dbg.stage = 8;
    autosave_tick();                              /* the working project into flash, when quiet */
    sections_flush();                             /* live sections / the recorded song, when quiet */
    felucca_dbg.stage = 9;
    fm1core_sl_frames++;
}

void fm1core_sl_fw_idle(void)                     /* main.c: ui_input while it waits for the next frame */
{
    if (fm1core_sl_looping)
        ui_input();
}

void fm1core_sl_fw_master(int32_t m) { fm1core_sl_master = m < 0 ? 0 : m > 1023 ? 1023 : m; }

/* ---------------------------------------------------------------- audio --- */
void fm1core_sl_fw_audio(int16_t *out, uint32_t frames)
{
    uint32_t f;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        if (fm1nor_stall_take() || fm1core_sl_audio_stopped) {   /* a flash erase: IRQs off, the buffer silent */
            memset(out + 2u * f, 0, FM1P_BLOCK * 2u * sizeof *out);
            continue;
        }
        fm1fifo_pull(&fm1core_sl_fifo, out + 2u * f, FM1P_BLOCK);
    }
}

/* ----------------------------------------------------------------- MIDI --- */
int fm1core_sl_fw_midi_in(uint32_t pkt)
{
    if (MQ - (mi_w - mi_r) < 8u)                  /* (usb.c's EP1 room) */
        return 0;
    midi_in_event(pkt);
    return 1;
}
int fm1core_sl_fw_midi_out_take(uint32_t *pkt)
{
    if (mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}

/* ------------------------------------------------------------- the flash --- */
/* make the image current: what the firmware writes when quiet (the live sections and the song, the working
 * project's autosave, the settings record) is written now (no erase when unchanged); 0 while the transport runs
 * (the firmware never writes then: a song playing holds a section) */
int fm1core_sl_fw_flash_sync(void)
{
    if (!flash_ok)
        return 1;
    if (song.playing || transport_req)
        return 0;
    persist_flush_now();
    settings_save();
    return 1;
}

/* ------------------------------------------------------------ diagnostics --- */
void fm1core_sl_fw_dump(void)
{
    uint32_t c, i;
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  loop frames %u\n", (unsigned)fm1_in.notes,
           (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)fm1core_sl_frames);
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X/%02X", fm1_led[c], fm1_led_dim[c], fm1_led_bg[c]);
    printf("  (lit/glow/backlight per column, backlight %u ns)\n", (unsigned)fm1_led_bg_ns);
    for (i = 0; i < NTRK; i++)
        printf("  track %u%s: engine %u preset %u level %d\n", (unsigned)i + 1u, i == song.sel ? "*" : "",
               (unsigned)trk[i].engine, (unsigned)trk[i].preset, (int)trk[i].p[P_LEVEL]);
    printf("  playing %u bpm %d page %u home %u master_q12 %u cpu %u%%  midi in %u out %u\n", (unsigned)song.playing,
           (int)song.g[G_BPM], (unsigned)ui.page, (unsigned)ui.home, (unsigned)song.master_q12,
           (unsigned)(song.cpu_q8 * 100u / 256u), (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks)\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks());
}

void fm1core_sl_fw_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%s %u track %u menu %u layer %u%s", ui.home ? "home, page" : "page", (unsigned)ui.page,
             (unsigned)song.sel + 1u, (unsigned)ui.menu, (unsigned)ui.layer, fm1core_sl_looping ? "" : " (splash)");
}

void fm1core_sl_fw_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}
