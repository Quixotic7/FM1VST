/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot family's shared glue (core-glue/choralroot/README.md): ChoralRoot's emulator unit and its forks'
 * (PurpleMonkey, CoralFlower) behind the core ABI. Each fork keeps its OWN emu_hooks.h / emu_hal_t (they differ: the
 * 44453d0 base, the 0.22 line, 0.25.1 on), private to its firmware unit; this file translates that emu_hal to and from
 * the ABI's fm1panel_t and defines the entry points the descriptor (crf_desc.h) points at.
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: a firmware unit (core_<id>.c) includes its fork's emu_fw.c (so `emu_hal`,
 * `emu_keymap` and the emu_fw_* hooks are in scope, with the fork's own emu_hal_t), then this file, after defining
 *
 *   FM1CORE_CRF_P          the entry points' prefix: fm1core_<P>_init, fm1core_<P>_panel, .. (cr, pm, cf)
 *   FM1CORE_CRF_VEL        1: the fork's emu_hal_t has key_vel[32] / key_lean[32] (ChoralRoot 0.25.1 on)
 *   FM1CORE_CRF_LEVELS     4: led / led_mid / led_dim / led_faint (0.22 on); 2: led / led_dim (the 44453d0 base)
 *   FM1CORE_CRF_PA2        1: the fork's emu_hal_t has pa2 (the backlight pin: the panel black, no LED lit)
 *   FM1CORE_CRF_FLASH_SYNC a function `int f(void)` for the descriptor's flash_sync (the fork's settings save now)
 *   FM1CORE_CRF_SX         the managers' SysEx service (crf_sx_pre.h included before the fork's emu_fw.c):
 *                          1 the fork's backup handler, cr_backup.c / cf_backup.c crb_handle (and its crb.reboot);
 *                          2 PurpleMonkey's pm_info.c (INFO only: pmi_reply, FM1CORE_CRF_PMI_C its path); 0 none
 *   FM1CORE_CRF_OTA_C      with FM1CORE_CRF_SX: the fork's ota.c (the M-UPGRADE identity, common/sysex_ota.h), with
 *                          FELUCCA_ID its device unit's package identity
 *   FM1CORE_CRF_EMU_SX     1: the fork's emu_fw_frame answers short 7D frames from usb.sysex itself (ChoralRoot
 *                          0.25.1 on): the glue takes them instead (the frame's bytes cleared once usb.c has seen them)
 *
 * and, before its emu_fw.c, including stdlib.h, fm1core.h (FM1CORE_NO_EMU_NAMES), version.h, exit.h, nor_stdio.h
 * and common/env.h, and after it nor_stdio_end.h, exit_end.h and panel.h (core_choralroot.c is the template).
 *
 * THE TRANSLATION (fm1core.h THREADING CONTRACT; the reference emulator's emu.c is the model)
 *   in    at the top of tick / frame / idle: FM1P_EMU_IN (held keys and buttons, taps OR'ed in, detents added,
 *         MASTER) and, with FM1CORE_CRF_VEL, each key's velocity and lean (emu.c hold_key_v: key_vel is the press's,
 *         0 = none given, the firmware's 100; key_lean 0..100 %). Keys 27..31 of the fork's [32] stay 0.
 *   boot  FM1P_EMU_BOOT before emu_fw_init (ready = 2, the held input, MASTER or 0: the firmware's default),
 *         FM1P_EMU_BOOTED after it (the fork's panel table, its MASTER default, ready).
 *   out   after init / frame / idle: the LED planes in emu.c led_state's priority, lit > mid > dim > faint, as
 *         FM1P_LED_LIT / MID / DIM / FAINT; PLAY green from its position (emu_hal.led_play_green); the LCD when its
 *         write count moved. PA2 high: every LED off and the LCD black (emu.c lcd_shown), the firmware's own RAM
 *         kept, so PA2 low shows it again.
 *   the rest of the fork's emu_hal_t stays as the headless emulator leaves it: strip_done / pump_mark NULL, pump_off 0
 *   (emu.c bench: strip_done is bench_strip, a no-op at EMU_STRIP_US 0), knob_quick 0 ("knobs real"), keys_classic 0
 *   (a fresh unit: Options > Chord Keys at the fork's default; the flash's settings record says otherwise).
 *
 * THE MANAGERS (FM1CORE_CRF_SX; research/managers-2026-10-19/MANAGERS.md): the device's main loop runs ed_service
 * (its editor / backup frames, 7D 46 4C) then ota_service (the M-UPGRADE handshake) every pass and in its wait loop.
 * Here midi_in gives each packet to the firmware as before (usb.c: DX7 / CZ-1 receive, the UBOOT key's flag, which no
 * emulator unit acts on) and to the glue's frame buffer (common/sysex.h: only FL and M-UPGRADE frames are kept, one at
 * a time; a new one waits, midi_in 0 = retry); after each frame and idle (the device's two places) the service hands
 * a waiting frame to crb_handle (or pmi_reply), else to ota_service. The replies leave through midi_out_take, ahead of
 * the firmware's MIDI out. 72 RESTART: as cr_backup.c crb_leave (FELUCCA_OTA, not in the emulator units), once the
 * reply has been taken (the ring empty) + 5 ms, 150 ms from the request at the earliest, 500 ms at the latest: the
 * firmware's exit(0) (CR_REBOOT: the boot guard told, the settings handler run), which halts the core with 0x100;
 * the host powers it on again from its flash image (the plugin: FM1Processor::handleAsyncUpdate). 77 STUB answers the
 * firmware's own "unsupported" (no boot stub: CRB_STUB_VER 0). The update key F0 22 24 35 7F F7 and UBOOT's
 * F0 22 24 35 7D F7 reach usb.c, which only sets flags here: nothing halts or writes.
 *
 * exit() and atexit(): see core_choralroot.c (common/exit.h; cleanup closes the flash file). */
#pragma once

#define FM1CORE_CRF_CAT_(a, b, c) a##b##c
#define FM1CORE_CRF_CAT(a, b, c) FM1CORE_CRF_CAT_(a, b, c)
#define FM1CORE_CRF(name) FM1CORE_CRF_CAT(fm1core_, FM1CORE_CRF_P, _##name)

#ifndef FM1CORE_CRF_VEL
#define FM1CORE_CRF_VEL 0
#endif
#ifndef FM1CORE_CRF_LEVELS
#define FM1CORE_CRF_LEVELS 2
#endif
#ifndef FM1CORE_CRF_PA2
#define FM1CORE_CRF_PA2 0
#endif
#ifndef FM1CORE_CRF_SX
#define FM1CORE_CRF_SX 0
#endif
#ifndef FM1CORE_CRF_EMU_SX
#define FM1CORE_CRF_EMU_SX 0
#endif

fm1panel_t FM1CORE_CRF(panel);

static void fm1core_crf_cleanup(void)     /* exit()'s last step: the flash file closed, later writes RAM only */
{
    if (emu_flash_f) {
        fm1nor_fflush(emu_flash_f);
        fm1nor_fclose(emu_flash_f);
        emu_flash_f = NULL;
    }
    emu_flash_path[0] = 0;                /* (a later emu_flash_open: RAM only) */
}

static void fm1core_crf_in(void)
{
    fm1panel_t *const p = &FM1CORE_CRF(panel);
    FM1P_EMU_IN(p, &emu_hal);
#if FM1CORE_CRF_VEL
    {
        uint32_t k;
        for (k = 0; k < FM1P_NKEY; k++) {
            emu_hal.key_vel[k] = p->key_vel[k] > 127u ? 127u : p->key_vel[k];
            emu_hal.key_lean[k] = p->key_lean[k] > 100u ? 100u : p->key_lean[k];
        }
    }
#endif
}

static int fm1core_crf_dark(void)         /* PA2 high: the panel shows black and no LED lights (emu.c) */
{
#if FM1CORE_CRF_PA2
    return emu_hal.pa2 != 0;
#else
    return 0;
#endif
}

static void fm1core_crf_out(void)
{
    fm1panel_t *const p = &FM1CORE_CRF(panel);
    static const uint8_t OFF[FM1P_NCOL];
#if FM1CORE_CRF_LEVELS >= 4
    static const uint8_t LV[4] = {FM1P_LED_LIT, FM1P_LED_MID, FM1P_LED_DIM, FM1P_LED_FAINT};
    const uint8_t *const pl[4] = {emu_hal.led, emu_hal.led_mid, emu_hal.led_dim, emu_hal.led_faint};
    const int n = 4;
#else
    static const uint8_t LV[2] = {FM1P_LED_LIT, FM1P_LED_DIM};
    const uint8_t *const pl[2] = {emu_hal.led, emu_hal.led_dim};
    const int n = 2;
#endif
    static int dark_was;
    const int dark = fm1core_crf_dark();
    if (dark) {                           /* every LED off; the LCD black once, its count off the firmware's */
        const uint8_t *const off[1] = {OFF};
        fm1p_leds_planes(p, off, LV, 1, emu_hal.led_play_green);
        if (!dark_was) {
            memset(p->lcd, 0, sizeof p->lcd);
            p->lcd_writes = emu_hal.lcd_writes ^ 0x80000000u;
        }
    } else {
        fm1p_leds_planes(p, pl, LV, n, emu_hal.led_play_green);
        if (dark_was)                     /* (back: the firmware's RAM again, whatever its count) */
            p->lcd_writes = ~emu_hal.lcd_writes;
        fm1p_lcd_out(p, emu_hal.lcd, emu_hal.lcd_writes);
    }
    dark_was = dark;
}

/* ---- the managers' SysEx service (FM1CORE_CRF_SX) ---- */
#if FM1CORE_CRF_SX
#ifdef FM1CORE_CRF_OTA_C
#define FM1SX_OTA_C FM1CORE_CRF_OTA_C
#define FM1SX_OTA_IN fm1core_crf_sxi
#define FM1SX_OTA_OUT fm1core_crf_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
#endif
#if FM1CORE_CRF_SX == 2
#include FM1CORE_CRF_PMI_C
#endif
static int fm1core_crf_sx_accept(const uint8_t *f, uint32_t n)
{
#ifdef FM1CORE_CRF_OTA_C
    if (fm1sx_is_mupg(f, n))
        return 1;
#endif
    return fm1sx_is_fl(f, n);
}
#if FM1CORE_CRF_SX == 1
/* cr_backup.c crb_leave's timing, on the glue's ring: the reply taken by the host (the ring empty), then 5 ms more,
 * not before 150 ms from the request, at 500 ms whatever happened; then CR_REBOOT's exit(0) */
static void fm1core_crf_sx_leave(void)
{
    const uint32_t now = fm1_ms, age = now - crb.reboot_ms;
    if (crb.reboot == 1u)                 /* (crb_handle has returned: the reply is in the ring) */
        crb.reboot = 2;
    if (crb.reboot == 2u && !fm1sx_out_queued(&fm1core_crf_sxo)) {
        crb.gone_ms = now;
        crb.reboot = 3;
    }
    if (age < 500u && (crb.reboot != 3u || age < 150u || now - crb.gone_ms < 5u))
        return;
    crb.reboot = 0;
    bootguard.pending = 0;                /* (CR_REBOOT: an intentional restart, not a failed boot) */
    printf("restart: 72 RESTART answered: the device restarts (exit 0)\n");
    fm1core_exit(0);                      /* (common/exit.h: exit() itself; this file is after exit_end.h) */
}
#endif
static void fm1core_crf_sx_service(void)  /* main.c's ed_service, then ota_service */
{
    const uint8_t *p;
    uint32_t n;
#if FM1CORE_CRF_SX == 1
    if (crb.reboot) {
        fm1core_crf_sx_leave();
        if (fm1core_halted())
            return;
    }
#endif
    if (!fm1sx_frame(&fm1core_crf_sxi, &p, &n))
        return;
    if (fm1sx_is_fl(p, n)) {
#if FM1CORE_CRF_SX == 1
        crb_handle(p, n);                 /* (in place: the frame is released after the reply) */
#else
        static uint8_t out[48];
        const uint32_t k = pmi_reply(p, n, out);
        if (k)
            fm1core_crf_sx_send(out, k);
#endif
        fm1sx_done(&fm1core_crf_sxi);
        return;
    }
#ifdef FM1CORE_CRF_OTA_C
    ota_service();                        /* (takes the frame and frees it) */
#else
    fm1sx_done(&fm1core_crf_sxi);
#endif
}
#endif

/* ---- the entry points (hidden; reached only through the descriptor) ---- */
int FM1CORE_CRF(halted)(void) { return fm1core_halted(); }
void FM1CORE_CRF(shutdown)(void) { fm1core_run_exit(); }

void FM1CORE_CRF(options)(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_exit_cleanup = fm1core_crf_cleanup;
    emu_fw_options(flash_path, no_flash, save_on_exit, headless);
}
int FM1CORE_CRF(boot_options)(int fail, const char *reason, int stage)
{
    return emu_fw_boot_options(fail, reason, stage);
}
void FM1CORE_CRF(init)(int demo)
{
    fm1panel_t *const p = &FM1CORE_CRF(panel);
    fm1core_exit_cleanup = fm1core_crf_cleanup;
#if FM1CORE_CRF_SX
    fm1sx_in_init(&fm1core_crf_sxi, fm1core_crf_sx_accept);
    fm1sx_out_reset(&fm1core_crf_sxo);
#endif
    FM1P_EMU_BOOT(p, &emu_hal);
    fm1core_crf_in();                     /* (the velocities of keys held at power-on) */
    emu_fw_init(demo);
    FM1P_EMU_BOOTED(p, &emu_hal);
    fm1core_crf_out();
}
void FM1CORE_CRF(tick)(uint32_t ms)
{
    if (fm1core_halted())
        return;
    fm1core_crf_in();
    emu_fw_tick(ms);
}
void FM1CORE_CRF(frame)(void)
{
    if (fm1core_halted())
        return;
    fm1core_crf_in();
    emu_fw_frame();
#if FM1CORE_CRF_SX
    if (!fm1core_halted())
        fm1core_crf_sx_service();
#endif
    fm1core_crf_out();
}
void FM1CORE_CRF(idle)(void)
{
    if (fm1core_halted())
        return;
    fm1core_crf_in();
    emu_fw_idle();
#if FM1CORE_CRF_SX
    if (!fm1core_halted())
        fm1core_crf_sx_service();
#endif
    fm1core_crf_out();
}
void FM1CORE_CRF(audio)(int16_t *stereo, uint32_t frames)
{
    if (fm1core_halted())
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
    else
        emu_fw_audio(stereo, frames);
}
int FM1CORE_CRF(midi_in)(uint32_t pkt)
{
    if (fm1core_halted())
        return 1;                         /* (halted: taken and dropped, never "retry") */
#if FM1CORE_CRF_SX
    if (fm1sx_in_would_refuse(&fm1core_crf_sxi, pkt))
        return 0;                         /* (a request waits for the main loop: this one after it) */
    if (!emu_fw_midi_in(pkt))
        return 0;
    fm1sx_in_pkt(&fm1core_crf_sxi, pkt);
#if FM1CORE_CRF_EMU_SX
    if (!usb.sx_on && usb.sx_len && usb.sysex[1] == 0x7Du)
        usb.sx_len = 0;                   /* (emu_fw_frame's own path: the service has it) */
#endif
    return 1;
#else
    return emu_fw_midi_in(pkt);
#endif
}
int FM1CORE_CRF(midi_out_take)(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
#if FM1CORE_CRF_SX
    if (fm1sx_take(&fm1core_crf_sxo, pkt))
        return 1;                         /* (usb.c ep1_tx: SysEx first, never split by notes) */
#endif
    return emu_fw_midi_out_take(pkt);
}
void FM1CORE_CRF(dump)(void) { emu_fw_dump(); }
void FM1CORE_CRF(stats)(uint32_t *shed, uint32_t *cpu_pct) { emu_fw_stats(shed, cpu_pct); }
void FM1CORE_CRF(ui_info)(char *buf, uint32_t n) { emu_fw_ui_info(buf, n); }
const int8_t *FM1CORE_CRF(keymap)(void) { return &emu_keymap[0][0]; }

/* ---- the flash (fm1core.h ABI 2): emu_hal_fw.h's image, staged through nor_stdio.h ---- */
uint8_t *FM1CORE_CRF(flash)(void) { return emu_flash; }
uint32_t FM1CORE_CRF(flash_dirty)(void) { return emu_flash_writes; }
int FM1CORE_CRF(flash_stage)(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int FM1CORE_CRF(flash_sync)(void) { return FM1CORE_CRF_FLASH_SYNC(); }
