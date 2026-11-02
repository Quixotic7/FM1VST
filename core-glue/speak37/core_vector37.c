/* SPDX-License-Identifier: GPL-3.0-only */
/* The Vector37 core: Vector37's FM-1 build (https://github.com/genkigenki/Vector37, build.py BOARD=fm1, experimental
 * upstream; the pinned, unmodified submodule cores/Vector37 at v0.3.2) as one translation unit with this glue
 * (core-api/fm1core.h ABI 6). Speak37's sibling: the same author and board layer, so it shares Speak37's host HAL.
 *   vector37_firmware.h  the firmware: vector37.c's include list for the FM-1, the device HAL replaced
 *   speak37_hal.h        that HAL on the host (time, input, LEDs, LCD, audio, ADC, the NOR image): Speak37's
 *   this file            the device's power-on and main loop (main.c fm1_cstart / mk_main, which never return)
 *                        re-done on the ABI's clock, the managers' SysEx, and the ABI's entry points.
 * Upstream ships no FM-1 emulator (web/sim is the SMK-37's): the model is the firmware's own FM-1 host test,
 * tests/fm1_panel.c over tests/panel.h (BOARD_SMK37 0: the real sound, UI and drawing on a simulated FM-1), and the
 * device:
 *   init   fm1_cstart's boot guard (bootguard.h: a failed boot before this one counted; the second in a row is the
 *          rescue mode, recovery.c, which a core has not: UBOOT = exit(3); OCT- held at power-on: the same), then
 *          mk_main's power-on in its order: persist_boot (the JEDEC probe only: the FM-1 build has no store),
 *          ota_boot_cleanup, palette_set, splash, the input, MASTER, synth_init, ui_init (the power-on sound),
 *          audio_init; the boot animation (draw.c boot_anim) starts
 *   frame  the boot animation (boot_frame, BOOT_MS 3 s or a key after 300 ms; the sound already runs, as on the
 *          device), then the main loop's body every 15 ms: the 30 s healthy mark, OCT- + OCT+ held 5 s (UBOOT, its
 *          countdown from 2 s), the M-UPGRADE identity (ota.c ota_service), the USB keys (ignored), midi_poll,
 *          ui_input (panel_fm1.c: the knobs, buttons, keys, MASTER), ui_leds, draw_frame
 *   idle   the loop's wait for the next pass: ui_input, midi_poll
 *   tick   TIMER5's ms and the scan's debounce (a press at once, a release after ~9 ms open), the encoders
 *   audio  the ALNK0 ISR (audio.c fm1_alnk0_irq: synth_block in CTL blocks, the load guard) once per 128-frame
 *          block, which is one I2S half here; silent after fm1_audio_stop
 *   MIDI   in: usb.c midi_in_event (the ring midi_poll drains: notes, bend, CCs, program change, clock; SysEx bytes:
 *          the UBOOT key) and the glue's SysEx frame buffer (M-UPGRADE frames only); out: the reply ring first, then
 *          usb.c's MIDI out ring
 *   flash  none: the FM-1 build keeps no data of its own (scenes.c persist_boot returns after the probe,
 *          fm1_flash_map.h FL_STORE_OK 0), so another firmware's store in the image stays as it was; flash_sync has
 *          nothing to write
 *   exit() common/exit.h: UBOOT and a reboot halt the core (halted() = 0x100 | code), shutdown runs the handlers
 * Not done (hardware): USB enumeration and the driver (usb_poll), the USB audio input (FELUCCA_UAC), the serial
 * console (FELUCCA_CDC), the rescue updater (recovery.c), the update session (M-UPGRADE's update start is never acted
 * on: common/sysex_ota.h). Key velocity and lean: the FM-1's build plays its keys at velocity 100 (panel_fm1.c):
 * key_vel / key_lean are ignored. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */
#include "panel.h"                        /* (core-glue/common) */

#include "vector37_firmware.h"

/* ---- the managers' SysEx: ota.c's M-UPGRADE identity on the glue's transport (common/sysex.h, sysex_ota.h).
 * Vector37 has no editor: the M-UPGRADE frames are the only ones kept for the main loop ---- */
#include "sysex.h"
static fm1sx_in_t fm1core_v37_sxi;        /* the request waiting for the main loop */
static fm1sx_out_t fm1core_v37_sxo;       /* the replies, as CIN 4..7 packets */
static int fm1core_v37_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n); }
#define FM1SX_OTA_C "ota.c"
#define FM1SX_OTA_IN fm1core_v37_sxi
#define FM1SX_OTA_OUT fm1core_v37_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

#include "exit_end.h"

/* ---------------------------------------------------------------- state --- */
fm1panel_t fm1core_v37_panel;
#define FM1CORE_V37_PANEL (&fm1core_v37_panel)
#define FM1CORE_V37_RELEASE_MS 9u         /* fm1_input.h: a release needs FM1_DEB_RELEASE frames open (~9 ms) */
static fm1p_edges_t fm1core_v37_keys_e, fm1core_v37_btn_e;   /* (taps: a press this tick, the release the next) */
static uint32_t fm1core_v37_in_btn, fm1core_v37_in_key;      /* the debounced state the tick delivers */
static uint32_t fm1core_v37_btn_ms[32], fm1core_v37_key_ms[32];
static uint8_t fm1core_v37_guard_set;     /* boot_options gave the guard record */
static uint8_t fm1core_v37_looping;       /* the boot animation is over: the main loop runs */
static uint32_t fm1core_v37_boot_t0;      /* mk_main's boot_anim: its start */
static uint32_t fm1core_v37_frames;       /* main loop passes */
static uint32_t fm1core_v37_healthy;      /* mk_main's healthy_since */
static uint32_t fm1core_v37_oct_t0, fm1core_v37_oct_shown;   /* mk_main's OCT- + OCT+ hold (t_oct, shown) */

/* -------------------------------------------------------- the panel out --- */
static void fm1core_v37_out(void)
{
    static const uint8_t lvl[2] = {FM1P_LED_LIT, FM1P_LED_DIM};
    const uint8_t *const planes[2] = {fm1_led, fm1_led_dim};   /* (panel_fm1.c fp_led: lit, the glow) */
    fm1p_leds_planes(FM1CORE_V37_PANEL, planes, lvl, 2, FM1P_PLAY_GREEN_POS);   /* (no green PLAY) */
    fm1p_lcd_out(FM1CORE_V37_PANEL, fm1core_sp_lcd, fm1core_sp_lcd_writes);
}
static void fm1core_v37_in(void)                  /* MASTER (panel_fm1.c reads the pot every 20 ms) */
{
    const int32_t m = FM1CORE_V37_PANEL->master;
    fm1core_sp_master = m < 0 ? 0 : m > 1023 ? 1023 : m;
}

/* ------------------------------------------------------------- power-on --- */
static void fm1core_v37_uboot(const char *why)   /* main.c enter_uboot, without the USB detach (no driver) */
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, RGB(80, 120, 255), 1);
    bootguard.pending = 0;                        /* intentional reset: not a failed boot */
    printf("%s: %s\n", FM1CORE_ID, why);
    fm1_enter_uboot();
}

static void fm1core_v37_power_on(void)
{
    uint32_t boot_mode;
    fm1t_set_ms(0);
    fm1_ms = 0;
    /* main.c fm1_cstart: a power-on clears the boot guard (.noinit), unless boot_options gave a record (a warm reset);
     * then bootguard_begin: a second failed boot in a row is the rescue mode (recovery.c), a reset inside that ROM */
    if (!fm1core_v37_guard_set)
        bootguard_clear(&bootguard);
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode != BOOT_NORMAL || bootguard_manual(fm1_in.buttons)) {
        printf("%s: %s: the rescue updater (recovery.c) is not in a core: UBOOT\n", FM1CORE_ID,
               boot_mode == BOOT_ROM ? "boot guard: the rescue itself reset" : boot_mode == BOOT_RECOVERY
               ? "boot guard: two failed boots in a row" : "OCT- held at power-on");
        fm1_enter_uboot();
        return;
    }
    /* main.c mk_main's power-on, in its order */
    fm1nor_open();                                /* (common/nor.c: erased, then the stage or the file) */
    persist_boot();
    if (flash_ok)
        ota_boot_cleanup();                       /* (sysex_ota.h's hooks: nothing is erased) */
    lcd_init();
    lcd_fill(0, 0, 240, 240, 0);                  /* (lcd.c lcd_init clears the panel) */
    palette_set(2);
    splash();
    if (mk_dbg.magic != DBG_MAGIC) {
        memset(&mk_dbg, 0, sizeof mk_dbg);
        mk_dbg.magic = DBG_MAGIC;
    }
    mk_dbg.boots++;
    mk_dbg.max_us = 0;
    mk_dbg.in_audio = 0;
    fm1_input_init();
    fm1_adc_init();                               /* MASTER */
    panel_master_init();                          /* (read before the audio starts: no jump) */
    synth_init();                                 /* the wavetables into RAM */
    ui_init();                                    /* the first sound */
    audio_init();
    fm1core_sp_audio_stopped = 0;
    usb.up = 1;                                   /* (usb_start) a host is there: MIDI out flows */
    usb.config = 1;
    fm1sx_in_init(&fm1core_v37_sxi, fm1core_v37_sx_accept);
    fm1sx_out_reset(&fm1core_v37_sxo);
    fm1core_v37_boot_t0 = fm1_ms;                 /* boot_anim (the sound and USB already run) */
    fm1core_v37_looping = 0;
    fm1core_v37_frames = 0;
    fm1core_v37_oct_t0 = 0;
    fm1core_v37_oct_shown = 0;
}

/* --------------------------------------------------------- the 1 ms timer --- */
/* what TIMER5 (fm1_input_tick, the debounce) delivers to the main loop: a press is an edge at once, a release only
 * after the key was held FM1CORE_V37_RELEASE_MS (the debounce's time); a tap shorter than a tick still arrives */
static void fm1core_v37_timer(uint32_t ms, uint32_t buttons, uint32_t keys, const int32_t *enc)
{
    uint32_t b = buttons, k = keys, i, down;
    fm1t_set_ms(ms);
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    down = b & ~fm1core_v37_in_btn;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_v37_btn_ms[i] = ms;
        else if ((fm1core_v37_in_btn & m) && !(b & m) && ms - fm1core_v37_btn_ms[i] < FM1CORE_V37_RELEASE_MS)
            b |= m;                               /* (held at least the debounce time) */
    }
    fm1_in.released |= fm1core_v37_in_btn & ~b;
    fm1core_v37_in_btn = b;
    fm1_in.pressed |= down;
    down = k & ~fm1core_v37_in_key;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_v37_key_ms[i] = ms;
        else if ((fm1core_v37_in_key & m) && !(k & m) && ms - fm1core_v37_key_ms[i] < FM1CORE_V37_RELEASE_MS)
            k |= m;
    }
    fm1core_v37_in_key = k;
    fm1_in.notes_pressed |= down;
    fm1_in.buttons = fm1core_v37_in_btn;
    fm1_in.notes = fm1core_v37_in_key;
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_steps[i] = (int16_t)(fm1_in.enc_steps[i] + enc[i]);
    fm1_in.frames++;
}

/* --------------------------------------------------------- the main loop --- */
static int fm1core_v37_boot_anim(void)            /* draw.c boot_anim, one frame per pass */
{
    uint32_t t;
    if (fm1core_v37_looping)
        return 0;
    t = fm1_ms - fm1core_v37_boot_t0;
    fm1_wdt_feed();
    if (!(t > BOOT_MS || (PANEL_KEYS() && t > 300u))) {
        boot_frame(t);
        return 1;
    }
    boot_frame(BOOT_MS);                          /* (the planets in their corners: the orbit screen takes over) */
    lcd_sync();
    boot_handover = 1;
    fm1core_v37_healthy = fm1_ms;
    fm1core_v37_oct_t0 = fm1_ms;
    fm1core_v37_looping = 1;
    return 1;
}

static void fm1core_v37_loop(void)                /* mk_main's loop body (one pass per 15 ms) */
{
    if (fm1core_v37_boot_anim())
        return;
    fm1_wdt_feed();
    if (fm1_ms - fm1core_v37_healthy >= 30000u && bootguard.pending)
        bootguard_clear(&bootguard);              /* 30 s of this boot */
    /* OCT- + OCT+ held 5 s: UBOOT (debug / update); a countdown from 2 s, letting go cancels it */
    if ((fm1_in.buttons & FM1_OCT_BOTH) != FM1_OCT_BOTH) {
        if (fm1core_v37_oct_shown)
            ui_say("UPDATE MODE ", "CANCELLED");
        fm1core_v37_oct_shown = 0;
        fm1core_v37_oct_t0 = fm1_ms;
    } else if (fm1_ms - fm1core_v37_oct_t0 > 2000u && fm1_ms - fm1core_v37_oct_t0 <= 5000u) {
        uint32_t left = (5000u - (fm1_ms - fm1core_v37_oct_t0) + 999u) / 1000u;
        if (left != fm1core_v37_oct_shown) {
            char d[4] = {(char)('0' + left), '.', '.', 0};
            ui_say("UPDATE MODE IN ", d);
            fm1core_v37_oct_shown = left;
        }
    } else if (fm1_ms - fm1core_v37_oct_t0 > 5000u) {
        fm1core_v37_uboot("UBOOT");
        return;
    }
    ota_service();                                /* M-UPGRADE handshake (the installers): the identity */
    if (usb.ota_req) {                            /* F0 22 24 35 7F F7: the update session; a core takes no update */
        usb.ota_req = 0;
        printf("%s: M-UPGRADE update start ignored (a core takes no update)\n", FM1CORE_ID);
    }
    if (usb.uboot_req) {                          /* F0 22 24 35 7D F7: the device would enter its ROM loader; a core
                                                   * has none: ignored, the core plays on (OCT- + OCT+ still leave) */
        usb.uboot_req = 0;
        printf("%s: UBOOT key over USB ignored (no ROM loader in a core)\n", FM1CORE_ID);
    }
    midi_poll();
    mk_dbg.ui_frames++;
    ui_input();
    ui_leds();
    draw_frame();
    fm1core_v37_frames++;
}

/* ===================================================== the ABI's entry points --- */
/* (hidden: reached only through the descriptor, core_vector37_desc.c) */
int fm1core_v37_halted(void) { return fm1core_halted(); }
void fm1core_v37_shutdown(void) { fm1core_run_exit(); }

void fm1core_v37_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                           /* (nothing is saved: no store on the FM-1) */
    fm1core_exit_cleanup = fm1nor_close;          /* (exit: the flash file closed, later writes RAM only) */
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}
/* the boot guard (fm1_cstart, bootguard.h): fail < 0 a clean record (a power-on); else the failed boots counted so
 * far, and the last run died within its 30 s (pending): this reset counts it, and the second failed boot in a row
 * is the rescue mode (UBOOT here, exit(3)). reason and stage: accepted and ignored */
int fm1core_v37_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    bootguard_clear(&bootguard);
    if (fail >= 0) {
        bootguard.failed = (uint32_t)(fail > 2 ? 2 : fail);
        bootguard.pending = 1;
    }
    fm1core_v37_guard_set = 1;
    return 1;
}
void fm1core_v37_init(int demo)
{
    fm1panel_t *p = FM1CORE_V37_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1core_sp_master = 1023;                     /* (tests/panel.h: the pot at the top, as the host's default) */
    if (p->master)
        fm1core_v37_in();
    fm1p_panel_default(p);                        /* the FM-1's fixed ids (panel_fm1.c reads the matrix ids) */
    fm1core_v37_power_on();
    if (fm1core_halted())
        return;
    if (!p->master)
        p->master = fm1core_sp_master;
    p->ready = 1;
    fm1core_v37_out();
}
void fm1core_v37_tick(uint32_t ms)
{
    fm1panel_t *p = FM1CORE_V37_PANEL;
    int32_t enc[FM1P_NENC];
    uint32_t i, keys, buttons;
    if (fm1core_halted())
        return;
    fm1core_v37_in();
    keys = fm1p_edges(&fm1core_v37_keys_e, p->keys, fm1p_take_key_taps(p), NULL, NULL);
    buttons = fm1p_edges(&fm1core_v37_btn_e, p->buttons, fm1p_take_button_taps(p), NULL, NULL);
    for (i = 0; i < FM1P_NENC; i++)
        enc[i] = fm1p_take_enc(p, i);
    fm1core_v37_timer(ms, buttons, keys, enc);
}
void fm1core_v37_frame(void)
{
    if (fm1core_halted())
        return;
    fm1core_v37_in();
    fm1core_v37_loop();
    if (!fm1core_halted())
        fm1core_v37_out();
}
void fm1core_v37_idle(void)                       /* mk_main's wait for the next pass: ui_input, midi_poll */
{
    if (fm1core_halted())
        return;
    fm1core_v37_in();
    if (fm1core_v37_looping) {
        ui_input();
        midi_poll();
    }
    fm1core_v37_out();
}
void fm1core_v37_audio(int16_t *stereo, uint32_t frames)
{
    uint32_t f, i;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        int16_t *o = stereo + 2u * f;
        const int32_t *a;
        if (fm1core_halted() || fm1nor_stall_take() || fm1core_sp_audio_stopped) {
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        fm1core_sp_half ^= 1u;                   /* the DMA hands the ISR the other half */
        fm1_alnk0_irq();
        a = &abuf[fm1core_sp_half * HALF_WORDS];
        for (i = 0; i < 2u * HALF_FRAMES; i++) {  /* Q15 << OUT_SHIFT (24 bit, -6 dBFS ceiling) -> Q15 */
            const int32_t v = a[i] >> OUT_SHIFT;
            o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}
/* MIDI in: refused (0, "retry") while a request waits and this packet starts another, or usb.c's ring is short of
 * room (ep1_take keeps the host NAKed then); else to usb.c midi_in_event and the frame buffer */
int fm1core_v37_midi_in(uint32_t pkt)
{
    if (fm1core_halted())
        return 1;
    if (fm1sx_in_would_refuse(&fm1core_v37_sxi, pkt))
        return 0;
    if (MQ - (mi_w - mi_r) < 8u)
        return 0;
    midi_in_event(pkt);
    fm1sx_in_pkt(&fm1core_v37_sxi, pkt);
    return 1;
}
int fm1core_v37_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    if (fm1sx_take(&fm1core_v37_sxo, pkt))
        return 1;                                 /* (usb.c ep1_tx: SysEx first, never split by notes) */
    if (mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}
void fm1core_v37_dump(void)
{
    uint32_t c;
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  loop passes %u%s\n", (unsigned)fm1_in.notes,
           (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)fm1core_v37_frames,
           fm1core_v37_looping ? "" : " (boot animation)");
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X", fm1_led[c], fm1_led_dim[c]);
    printf("\n  page %u layer %u bank %u sound %u sel %X  voices %u/%u  master q12 %d  midi in %u out %u\n",
           (unsigned)fp.page, (unsigned)ui.layer, (unsigned)ui.bank, (unsigned)ui.preset, (unsigned)ui.sel,
           (unsigned)vmax, (unsigned)NVOICE, (int)panel_master_q12, (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    printf("  flash: %s, %u writes (no store on the FM-1: %s)\n", fm1nor_path()[0] ? fm1nor_path() : "RAM only",
           (unsigned)fm1nor_dirty(), flash_ok ? "probe ok" : "probe failed");
}
void fm1core_v37_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = NVOICE - vmax;                        /* (voices the load guard took away) */
    *cpu_pct = cpu_q8 * 100u / 256u;
}
void fm1core_v37_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%spage %u layer %u bank %u sound %u%s", fm1core_v37_looping ? "" : "(boot) ",
             (unsigned)fp.page, (unsigned)ui.layer, (unsigned)ui.bank, (unsigned)ui.preset,
             ui.list ? " (sound list)" : "");
}
const int8_t *fm1core_v37_keymap(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2): common/nor.c (never written: no store) ---- */
uint8_t *fm1core_v37_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_v37_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_v37_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_v37_flash_sync(void) { return 1; }   /* (nothing deferred: the FM-1 build saves nothing) */
