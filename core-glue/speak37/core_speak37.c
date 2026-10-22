/* SPDX-License-Identifier: GPL-3.0-only */
/* The Speak37 core: Speak37's FM-1 build (https://github.com/genkigenki/Speak37, build.py BOARD=fm1speak; the pinned,
 * unmodified submodule cores/Speak37 at v1.0) as one translation unit with this glue (core-api/fm1core.h ABI 6):
 *   speak37_firmware.h  the firmware: speak.c's include list for the FM-1, the device HAL replaced
 *   speak37_hal.h       that HAL on the host (time, input, LEDs, LCD, audio, ADC, the NOR image)
 *   this file           the device's power-on and main loop (sp_main.c fm1_cstart / fm1_main, which never return)
 *                       re-done on the ABI's clock, the managers' SysEx, and the ABI's entry points.
 * Upstream ships no FM-1 emulator (its web/emu is plain Felucca, web/sim the SMK-37's): the model is the firmware's
 * own FM-1 host test, tests/sp_screen.c -DT_FM1=1 (the real sound and drawing on a stubbed keyboard), and the device:
 *   init   fm1_cstart's boot guard (two failed boots in a row: UBOOT = exit(3)), then fm1_main's power-on in its
 *          order: sp_init, sp_store_boot (the flash), palette_set, the LCD, the input, the audio; the boot screen
 *          (sp_boot_frame) starts
 *   frame  the boot screen for SP_BOOT_MS (1.2 s; the sound already runs, as on the device), then the main loop's
 *          body every 15 ms: the 30 s healthy mark, MASTER (the pot, smoothed), OCT- + OCT+ held 5 s (UBOOT, its
 *          countdown), the M-UPGRADE identity (ota.c ota_service), the USB UBOOT key (ignored), sp_ui_input,
 *          sp_ui_autosave, sp_ui_leds, sp_ui_draw
 *   idle   the loop's wait for the next pass: sp_ui_input
 *   tick   TIMER5's ms and the scan's debounce (a press at once, a release after ~9 ms open), the encoders
 *   audio  the ALNK0 ISR (sp_main.c fm1_alnk0_irq: USB-MIDI drained, sp_render in SP_BLK blocks, MASTER, the soft
 *          limiter, the load governor) once per 128-frame block, which is one I2S half; silent while a flash erase
 *          stalls (common/nor.c) or after fm1_audio_stop
 *   MIDI   in: usb.c midi_in_event (notes, bend, CC1, program change, clock; the UBOOT key) and the glue's SysEx
 *          frame buffer (M-UPGRADE frames only); out: the reply ring first, then usb.c's MIDI out ring
 *   flash  common/nor.c's image under the device's storage_hw.c / storage.c: the record (OBJ_SETTINGS 0xFC000) and the
 *          USER sentences (OBJ_PROJECT0); flash_sync runs the autosave now (sp_ui_autosave without its 4 s wait),
 *          deferred (0) while a voice speaks or rang within the last second, as the device never writes then
 *   exit() common/exit.h: UBOOT and a reboot halt the core (halted() = 0x100 | code), shutdown runs the handlers
 * Not done (hardware): USB enumeration and the driver (usb_poll), the USB audio input (FELUCCA_UAC), the serial
 * console (FELUCCA_CDC), the update session (M-UPGRADE's update start is never acted on: common/sysex_ota.h).
 * Key velocity and lean: the FM-1's build reads none (SP_VEL is 100, as speak.c): key_vel / key_lean are ignored. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */
#include "panel.h"                        /* (core-glue/common) */

#include "speak37_firmware.h"

/* ---- the managers' SysEx: ota.c's M-UPGRADE identity on the glue's transport (common/sysex.h, sysex_ota.h). Speak37
 * builds no editor (speak.c has no editor.c): the M-UPGRADE frames are the only ones kept for the main loop ---- */
#include "sysex.h"
static fm1sx_in_t fm1core_sp_sxi;         /* the request waiting for the main loop */
static fm1sx_out_t fm1core_sp_sxo;        /* the replies, as CIN 4..7 packets */
static int fm1core_sp_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n); }
#define FM1SX_OTA_C "ota.c"
#define FM1SX_OTA_IN fm1core_sp_sxi
#define FM1SX_OTA_OUT fm1core_sp_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

#include "exit_end.h"

/* ---------------------------------------------------------------- state --- */
fm1panel_t fm1core_sp_panel;
#define FM1CORE_SP_PANEL (&fm1core_sp_panel)
#define FM1CORE_SP_RELEASE_MS 9u          /* fm1_input.h: a release needs FM1_DEB_RELEASE frames open (~9 ms) */
static fm1p_edges_t fm1core_sp_keys_e, fm1core_sp_btn_e;   /* (taps: a press this tick, the release the next) */
static uint32_t fm1core_sp_in_btn, fm1core_sp_in_key;      /* the debounced state the tick delivers */
static uint32_t fm1core_sp_btn_ms[32], fm1core_sp_key_ms[32];
static uint8_t fm1core_sp_save_on_exit;
static uint8_t fm1core_sp_guard_set;      /* boot_options gave the guard record */
static uint8_t fm1core_sp_looping;        /* the boot screen is over: the main loop runs */
static uint32_t fm1core_sp_boot_t0;       /* fm1_main's boot screen: its start */
static uint32_t fm1core_sp_frames;        /* main loop passes */
static int32_t fm1core_sp_pot_last, fm1core_sp_pot_sm;   /* fm1_main's MASTER: last, smoothed (its statics) */
static uint32_t fm1core_sp_oct_t0;        /* fm1_main's OCT- + OCT+ hold */

/* ------------------------------------------------------------ the flash --- */
/* make the image current: the autosave (sp_ui_autosave: the sound in use, MASTER, the sentence and the mode, 4 s
 * after the last change) now, without its 4 s wait; deferred while a voice speaks or rang within the last second
 * (its own condition: a flash write halts the audio). The memories (SAVE held) and the USER sentences (the editor's
 * OK) are written by the firmware at once. */
static int fm1core_sp_sync(void)
{
    uint32_t t;
    if (!flash_ok)
        return 1;
    if (sp_pot != vst.master && !su.snd_dirty) {   /* (MASTER moved: sp_ui_autosave's own first step) */
        su.snd_dirty = 1;
        su.t_change = fm1_ms;
    }
    if (!su.snd_dirty && !su.glob_dirty)
        return 1;
    t = su.t_change;
    su.t_change = fm1_ms - 4000u;                 /* (its quiet time over) */
    sp_ui_autosave(sp_pot);
    if (su.snd_dirty || su.glob_dirty) {          /* a voice still speaks: the firmware keeps it for later */
        su.t_change = t;
        return 0;
    }
    return 1;
}
static void fm1core_sp_exit(void)                 /* at exit (the core's shutdown): save_on_exit */
{
    if (fm1core_sp_save_on_exit && fm1core_sp_looping)
        fm1core_sp_sync();
}

/* -------------------------------------------------------- the panel out --- */
static void fm1core_sp_out(void)
{
    static const uint8_t lvl[1] = {FM1P_LED_LIT};
    const uint8_t *const planes[1] = {fm1_led};
    fm1p_leds_planes(FM1CORE_SP_PANEL, planes, lvl, 1, FM1P_PLAY_GREEN_POS);   /* (Speak37 lights no green PLAY) */
    fm1p_lcd_out(FM1CORE_SP_PANEL, fm1core_sp_lcd, fm1core_sp_lcd_writes);
}
static void fm1core_sp_in(void)                   /* MASTER (the main loop reads it) */
{
    const int32_t m = FM1CORE_SP_PANEL->master;
    fm1core_sp_master = m < 0 ? 0 : m > 1023 ? 1023 : m;
}

/* ------------------------------------------------------------- power-on --- */
static void fm1core_sp_uboot(const char *why)    /* sp_main.c sp_uboot, without the USB detach (no driver) */
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, SV_BG);
    draw_text_line(0, 110, 240, &AF_M, why, SV_TEXT, SV_BG, 1);
    bootguard.pending = 0;                        /* intentional: not a failed boot */
    printf("%s: %s\n", FM1CORE_ID, why);
    fm1_enter_uboot();
}

static void fm1core_sp_power_on(void)
{
    uint32_t i;
    fm1t_set_ms(0);
    fm1_ms = 0;
    /* sp_main.c fm1_cstart: the boot-loop guard (.noinit: a power-on finds no magic, unless boot_options gave one) */
    if (!fm1core_sp_guard_set || bootguard.magic != BOOTGUARD_MAGIC) {
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
        printf("%s: boot guard: two failed boots in a row: UBOOT\n", FM1CORE_ID);
        fm1_enter_uboot();
        return;
    }
    /* sp_main.c fm1_main's power-on, in its order */
    fm1nor_open();                                /* (common/nor.c: erased, then the stage or the file) */
    sp_init();
    sp_store_boot();
    palette_set(SP_PALETTE);
    lcd_init();
    lcd_fill(0, 0, 240, 240, SV_BG);
    sp_dbg.boots++;
    sp_mhz = fm1_cpu_mhz();
    fm1_input_init();
    fm1_adc_init();
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
    fm1core_sp_audio_stopped = 0;
    usb.up = 1;                                   /* (usb_start) a host is there: MIDI out flows */
    usb.config = 1;
    fm1sx_in_init(&fm1core_sp_sxi, fm1core_sp_sx_accept);
    fm1sx_out_reset(&fm1core_sp_sxo);
    fm1core_atexit(fm1core_sp_exit);             /* (common/exit.h: the descriptor's shutdown runs it) */
    fm1core_sp_boot_t0 = fm1_ms;                  /* the boot screen (the sound and USB already run) */
    sp_boot_frame(0);
    fm1core_sp_looping = 0;
    fm1core_sp_frames = 0;
    fm1core_sp_pot_last = -1;
    fm1core_sp_pot_sm = -1;
    fm1core_sp_oct_t0 = 0;
}

/* --------------------------------------------------------- the 1 ms timer --- */
/* what TIMER5 (fm1_input_tick, the debounce) delivers to the main loop: a press is an edge at once, a release only
 * after the key was held FM1CORE_SP_RELEASE_MS (the debounce's time); a tap shorter than a tick still arrives */
static void fm1core_sp_timer(uint32_t ms, uint32_t buttons, uint32_t keys, const int32_t *enc)
{
    uint32_t b = buttons, k = keys, i, down;
    fm1t_set_ms(ms);
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    down = b & ~fm1core_sp_in_btn;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_sp_btn_ms[i] = ms;
        else if ((fm1core_sp_in_btn & m) && !(b & m) && ms - fm1core_sp_btn_ms[i] < FM1CORE_SP_RELEASE_MS)
            b |= m;                               /* (held at least the debounce time) */
    }
    fm1_in.released |= fm1core_sp_in_btn & ~b;
    fm1core_sp_in_btn = b;
    fm1_in.pressed |= down;
    down = k & ~fm1core_sp_in_key;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_sp_key_ms[i] = ms;
        else if ((fm1core_sp_in_key & m) && !(k & m) && ms - fm1core_sp_key_ms[i] < FM1CORE_SP_RELEASE_MS)
            k |= m;
    }
    fm1core_sp_in_key = k;
    fm1_in.notes_pressed |= down;
    fm1_in.buttons = fm1core_sp_in_btn;
    fm1_in.notes = fm1core_sp_in_key;
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_steps[i] = (int16_t)(fm1_in.enc_steps[i] + enc[i]);
    fm1_in.frames++;
}

/* --------------------------------------------------------- the main loop --- */
static void fm1core_sp_master_pot(void)           /* fm1_main: MASTER, the level pot^2 / 256, the FM-1's pot smoothed */
{
    int32_t pot = fm1_adc_read(FM1_ADC_MASTER);
    if (pot < 0)
        pot = (int32_t)sp_pot;
    fm1core_sp_pot_sm = fm1core_sp_pot_sm < 0 ? pot * 16 : fm1core_sp_pot_sm + (pot * 16 - fm1core_sp_pot_sm) / 8;
    pot = fm1core_sp_pot_sm / 16;
    pot = fm1core_sp_pot_last >= 0 && pot - fm1core_sp_pot_last < 6 && fm1core_sp_pot_last - pot < 6
              ? fm1core_sp_pot_last : pot;        /* (the ADC's last bits: no flicker) */
    sp_pot = (uint32_t)(pot < 0 ? 0 : pot > 1023 ? 1023 : pot);
    sp_master_q12 = (int32_t)(sp_pot * sp_pot / 256u);
    if (fm1core_sp_pot_last >= 0 && (int32_t)sp_pot != fm1core_sp_pot_last)
        sp_touch(0);                              /* (its cell) */
    fm1core_sp_pot_last = (int32_t)sp_pot;
}

static int fm1core_sp_boot_screen(void)           /* fm1_main: the boot screen's 1.2 s, then the loop */
{
    if (fm1core_sp_looping)
        return 0;
    if (fm1_ms - fm1core_sp_boot_t0 < SP_BOOT_MS) {
        sp_boot_frame(fm1_ms - fm1core_sp_boot_t0);
        return 1;
    }
    lcd_fill(0, 0, 240, 240, SV_BG);
    su.redraw = 1;
    fm1core_sp_oct_t0 = fm1_ms;
    fm1core_sp_looping = 1;
    return 0;
}

static void fm1core_sp_loop(void)                 /* fm1_main's loop body (one pass per 15 ms) */
{
    if (fm1core_sp_boot_screen())
        return;
    fm1_wdt_feed();
    if (fm1_ms > 30000u && bootguard.pending) {   /* a crash or hang in the first 30 s counts */
        bootguard.pending = 0;
        bootguard.failed = 0;
    }
    fm1core_sp_master_pot();
    /* OCT- + OCT+ held 5 s: UBOOT (the update path), the countdown from 2 s */
    if ((fm1_in.buttons & FM1_OCT_BOTH) != FM1_OCT_BOTH)
        fm1core_sp_oct_t0 = fm1_ms;
    else if (fm1_ms - fm1core_sp_oct_t0 > 5000u) {
        fm1core_sp_uboot("UBOOT");
        return;
    } else if (fm1_ms - fm1core_sp_oct_t0 > 2000u)
        sp_popup("UPDATE MODE IN", fm1_ms - fm1core_sp_oct_t0 > 4000u ? "1" : fm1_ms - fm1core_sp_oct_t0 > 3000u ? "2"
                                                                                                          : "3");
    ota_service();                                /* M-UPGRADE handshake (the installers): the identity */
    if (usb.uboot_req) {                          /* SysEx F0 22 24 35 7D F7 from the host: the device would save,
                                                   * detach and enter its ROM loader; a core has none: the key is
                                                   * ignored, the core plays on (OCT- + OCT+ held 5 s still leaves) */
        usb.uboot_req = 0;
        printf("%s: UBOOT key over USB ignored (no ROM loader in a core)\n", FM1CORE_ID);
    }
    sp_ui_input();
    sp_ui_autosave(sp_pot);
    sp_ui_leds();
    sp_ui_draw();
    fm1core_sp_frames++;
}

/* ===================================================== the ABI's entry points --- */
/* (hidden: reached only through the descriptor, core_speak37_desc.c) */
int fm1core_sp_halted(void) { return fm1core_halted(); }
void fm1core_sp_shutdown(void) { fm1core_run_exit(); }

void fm1core_sp_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_exit_cleanup = fm1nor_close;          /* (exit: the flash file closed, later writes RAM only) */
    fm1core_sp_save_on_exit = (uint8_t)(save_on_exit != 0);
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}
/* the boot guard (fm1_cstart): fail < 0 a clean record; else the failed boots counted so far, and the last run died
 * within its 30 s (pending): this reset counts it, and the second failed boot in a row enters UBOOT (exit(3)).
 * reason and stage: accepted and ignored (no reset reasons here; every power-on counts as the device's resets) */
int fm1core_sp_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    bootguard.magic = BOOTGUARD_MAGIC;
    bootguard.failed = fail >= 0 ? (uint32_t)fail : 0u;
    bootguard.pending = fail >= 0;
    fm1core_sp_guard_set = 1;
    return 1;
}
void fm1core_sp_init(int demo)
{
    fm1panel_t *p = FM1CORE_SP_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    if (p->master)
        fm1core_sp_in();
    fm1p_panel_default(p);                        /* Felucca's PANEL_DEFAULT: sp_panel_fm1.c's fixed ids */
    fm1core_sp_power_on();
    if (fm1core_halted())
        return;
    if (!p->master)
        p->master = fm1core_sp_master;            /* the firmware's default MASTER */
    p->ready = 1;
    fm1core_sp_out();
}
void fm1core_sp_tick(uint32_t ms)
{
    fm1panel_t *p = FM1CORE_SP_PANEL;
    int32_t enc[FM1P_NENC];
    uint32_t i, keys, buttons;
    if (fm1core_halted())
        return;
    fm1core_sp_in();
    keys = fm1p_edges(&fm1core_sp_keys_e, p->keys, fm1p_take_key_taps(p), NULL, NULL);
    buttons = fm1p_edges(&fm1core_sp_btn_e, p->buttons, fm1p_take_button_taps(p), NULL, NULL);
    for (i = 0; i < FM1P_NENC; i++)
        enc[i] = fm1p_take_enc(p, i);
    fm1core_sp_timer(ms, buttons, keys, enc);
}
void fm1core_sp_frame(void)
{
    if (fm1core_halted())
        return;
    fm1core_sp_in();
    fm1core_sp_loop();
    if (!fm1core_halted())
        fm1core_sp_out();
}
void fm1core_sp_idle(void)                        /* fm1_main's wait for the next pass: sp_ui_input */
{
    if (fm1core_halted())
        return;
    fm1core_sp_in();
    if (fm1core_sp_looping)
        sp_ui_input();
    fm1core_sp_out();
}
void fm1core_sp_audio(int16_t *stereo, uint32_t frames)
{
    uint32_t f, i;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        int16_t *o = stereo + 2u * f;
        const int32_t *a;
        if (fm1core_halted() || fm1nor_stall_take() || fm1core_sp_audio_stopped) {
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);   /* (a flash erase: IRQs off, the buffer silent) */
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
 * room (ep1_take keeps the host NAKed then); else to usb.c midi_in_event (the ring the audio ISR drains: notes, bend,
 * CC1, program change, clock; SysEx bytes: the UBOOT key) and the frame buffer */
int fm1core_sp_midi_in(uint32_t pkt)
{
    if (fm1core_halted())
        return 1;
    if (fm1sx_in_would_refuse(&fm1core_sp_sxi, pkt))
        return 0;
    if (MQ - (mi_w - mi_r) < 8u)
        return 0;
    midi_in_event(pkt);
    fm1sx_in_pkt(&fm1core_sp_sxi, pkt);
    return 1;
}
int fm1core_sp_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    if (fm1sx_take(&fm1core_sp_sxo, pkt))
        return 1;                                 /* (usb.c ep1_tx: SysEx first, never split by notes) */
    if (mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}
void fm1core_sp_dump(void)
{
    uint32_t c;
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  loop passes %u%s\n", (unsigned)fm1_in.notes,
           (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)fm1core_sp_frames,
           fm1core_sp_looping ? "" : " (boot screen)");
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X", fm1_led[c]);
    printf("\n  page %u setup %u mode %u sentence %u (bank %u) voice %u octave %d  speaking %03X voices %u/%u  "
           "auto %u groove %u  master %u (q12 %d)  cpu %u%%  midi in %u out %u\n",
           (unsigned)su.page, (unsigned)su.setup, (unsigned)sp.mode, (unsigned)vst.sent, (unsigned)sp_bank_of(vst.sent),
           (unsigned)sp.snd.voice, (int)sp.snd.oct, (unsigned)sp.speaking, (unsigned)sp.vmax, (unsigned)SP_NV,
           (unsigned)sp.auto_on, (unsigned)sp.groove, (unsigned)sp_pot, (int)sp_master_q12,
           (unsigned)(sp_cpu_q8 * 100u / 256u), (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks); store %s%s\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks(), flash_ok ? "ok" : "none", sp_store_fail ? ", a save FAILED" : "");
}
void fm1core_sp_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = SP_NV - sp.vmax;                      /* (voices the load governor took away) */
    *cpu_pct = sp_cpu_q8 * 100u / 256u;
}
void fm1core_sp_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%spage %u%s mode %u sentence %u voice %u%s", fm1core_sp_looping ? "" : "(boot) ",
             (unsigned)su.page, su.setup ? " SETUP" : "", (unsigned)sp.mode, (unsigned)vst.sent,
             (unsigned)sp.snd.voice, ue.open ? " (editor)" : "");
}
const int8_t *fm1core_sp_keymap(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2): common/nor.c ---- */
uint8_t *fm1core_sp_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_sp_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_sp_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_sp_flash_sync(void)
{
    if (fm1core_halted() || !FM1CORE_SP_PANEL->ready || !fm1core_sp_looping)
        return 1;
    return fm1core_sp_sync();
}
