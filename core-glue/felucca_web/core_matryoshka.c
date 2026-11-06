/* SPDX-License-Identifier: GPL-3.0-only */
/* The Matryoshka core (https://github.com/glvno/matryoshka, the pinned, unmodified submodule cores/matryoshka): michael
 * glaviano's patchable synth (a fork of morgulbrut's Modular-1) on Felucca's hardware layer. Matryoshka has no browser
 * emulator of the whole firmware (its web app's FM-1 tab, firmware/wasm/fm1_ui.c, is the panel and screens alone), so
 * this unit compiles the DEVICE's unit, firmware/src/modular.c, with the HAL read as matryoshka_hal.h (every
 * firmware/hal header, by clang's -ivfsoverlay: core_matryoshka.cmake), and runs it as the chip does:
 *
 *   the main loop  main.c fm1_main on a coroutine (common/coro.h), from the boot to the loop's for (;;). fm1_wdt_feed
 *                  (every loop and every wait of the firmware feeds it) ends a pass: each tick(ms) runs one. fm1_cstart's
 *                  part that is not the chip's (the boot guard record of a power-on) is done before it; exit() inside
 *                  parks it for good (common/exit.h fm1core_exit_park)
 *   the clock      TIMER4 (fm1core_mt_t4) 24000 ticks per device ms, TIMER5's interrupt (main.c fm1_timer5_irq: fm1_ms,
 *                  usb_poll at 2 kHz) ten times per ms, in tick, before the pass
 *   the audio      ALNK0's interrupt (audio.c fm1_alnk0_irq, the real one: the engine, MASTER, the overload guard) once
 *                  per 128-frame block audio() asks for, into the half the hardware would play next; the I2S words (24 bit
 *                  in the low bits) >> 8 to s16, as the device's USB audio takes them (UAC_IN_SHIFT). An erase's stall is
 *                  silence (common/nor.c). The engine runs at ENG_SR 44118 (the chip's I2S rate), served at 44100
 *   the panel      Matryoshka's ids are the FM-1's matrix ids (panel.c BTN_*: OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE
 *                  ARP SEQ PLAY REC = 0..13; knobs_poll: encoder k = matrix encoder k), so the panel table is the factory
 *                  one (fm1p_panel_default). Into fm1_in as the scan leaves it: held masks, button edges, the note keys'
 *                  queue in order, encoder steps; a tap is held for one pass. MASTER: the ADC's 0..1023
 *   the LEDs       the device's four planes (fm1_input.h): lit, mid, the glow (MENU > LEDS: DIM LOW / HIGH), the breath
 *                  (the PLAY green LED while it runs, OCT-/OCT+ while shifted: its curve, at fm1_br_step)
 *   the LCD        the ST7789's RAM (matryoshka_hal.h), big-endian as sent; dark while the panel is off
 *   the flash      common/nor.c's 1 MiB image (A), the device's offsets and formats: the patch bank and the settings
 *                  (slotstore.c), the preset store (presetstore.c), as the firmware writes them. flash_sync writes the
 *                  global tempo's settings record now (main.c tempo_poll's, due 5 s after the last change)
 *   USB MIDI       the firmware's emulator build (M1_EMU_MIDI=1, usb.c m1_emu_mb): midi_in puts the packets into the
 *                  mailbox, usb_poll moves them into the firmware's rings as EP1 would (its own flow control: SysEx held
 *                  while a frame waits), SysEx frames to the main loop's host_poll: the editor's protocol (proto.c,
 *                  F0 7D 4D 31 ..: HELLO -> INFO, the patch upload, STORE, the bank) and the M-UPGRADE handshake (ota.c:
 *                  the identity FM-1_7010901); midi_out_take reads the mailbox's out ring (SysEx first, never split)
 *   updates        the update key (F0 22 24 35 7F F7, usb.ota_req) and the UBOOT key (F0 22 24 35 7D F7, usb.uboot_req)
 *                  are cleared after each interrupt: no update and no chip boot inside a core. An installer gets the
 *                  identity, then no session
 * Not here: TRS MIDI (no bytes on UART1), USB audio (FELUCCA_UAC 0), the boot guard's second failed boot (a core boots
 * once per power-on), the crash and watchdog paths, the CPU's time (the overload guard sees no time pass in a render).
 *
 * Macros and statics here are fm1core_mt_-prefixed (the firmware is in this unit). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the NOR image */
#include "coro.h"                         /* (core-glue/common) the main loop's coroutine */
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                          /* getenv -> NULL (common/env.h) */

/* the linker's symbols (app.ld) that main.c and fm1_guard.h name: the DIAG pages read them, the boot code that walks
 * them (fm1_cstart: lowram_prove, stack_paint) is not run. Renamed into the glue's own */
#define _data_start fm1core_mt_data_start
#define _data_end fm1core_mt_data_end
#define _data_load fm1core_mt_data_load
#define _bss_start fm1core_mt_bss_start
#define _bss_end fm1core_mt_bss_end
#define _pool_start fm1core_mt_pool_start
#define _pool_end fm1core_mt_pool_end
#define _rt_start fm1core_mt_rt_start
#define _rt_end fm1core_mt_rt_end
#define _rt_load fm1core_mt_rt_load
#define _lr_start fm1core_mt_lr_start
#define _lr_end fm1core_mt_lr_end
#define _lr_limit fm1core_mt_lr_limit
#define _ustack_top fm1core_mt_ustack_top
#define _ustack_lo fm1core_mt_ustack_lo
#define _sstack_lo fm1core_mt_sstack_lo
#define _sstack_top fm1core_mt_sstack_top
#define _guard0 fm1core_mt_guard0
#define _guard1 fm1core_mt_guard1
#define _etext fm1core_mt_etext
#define isr_alnk0 fm1core_mt_isr_alnk0
#define isr_timer5 fm1core_mt_isr_timer5

#define memset fm1core_mt_memset          /* libc.c's freestanding three (as firmware/test/ui_preview.c builds them) */
#define memcpy fm1core_mt_memcpy
#define memcmp fm1core_mt_memcmp
#include "modular.c"                      /* the device's unit (firmware/src), the HAL as matryoshka_hal.h */
#undef memset
#undef memcpy
#undef memcmp

#include "exit_end.h"

uint32_t fm1core_mt_data_start[1], fm1core_mt_data_end[1], fm1core_mt_data_load[1], fm1core_mt_bss_start[1],
    fm1core_mt_bss_end[1], fm1core_mt_pool_start[1], fm1core_mt_pool_end[1], fm1core_mt_rt_start[1],
    fm1core_mt_rt_end[1], fm1core_mt_rt_load[1], fm1core_mt_lr_start[1], fm1core_mt_lr_end[1], fm1core_mt_lr_limit[1];
char fm1core_mt_ustack_top[4], fm1core_mt_ustack_lo[4], fm1core_mt_sstack_lo[4], fm1core_mt_sstack_top[4],
    fm1core_mt_guard0[4], fm1core_mt_guard1[4], fm1core_mt_etext[4];
void fm1core_mt_isr_alnk0(void) {}        /* (the vectors' entry stubs, hal/fm1_isr.S: the glue calls the C handlers) */
void fm1core_mt_isr_timer5(void) {}

/* ---- the glue's state (hidden: reached only through the descriptor) ---- */
fm1panel_t fm1core_mt_panel;
#define FM1CORE_MT_PANEL (&fm1core_mt_panel)
static fm1coro_t *fm1core_mt_co;
static uint8_t fm1core_mt_booted;
static uint32_t fm1core_mt_isr_regs[8];   /* (fm1_timer5_irq's saved registers: DIAG's stall samples read two) */
static uint32_t fm1core_mt_devms;
static uint32_t fm1core_mt_lcd_seen = 0xFFFFFFFFu;

/* ------------------------------------------------- the HAL's hooks (matryoshka_hal.h) --- */
static void fm1core_mt_yield(void)
{
    if (fm1coro_inside())
        fm1coro_yield();
}
static void fm1core_mt_sleep_ms(uint32_t ms)  /* fm1_delay_ms: TIMER4's time, as the device's busy-wait */
{
    const uint32_t t0 = fm1core_mt_t4;
    if (!fm1coro_inside())
        return;
    do
        fm1coro_yield();
    while (fm1core_mt_t4 - t0 < ms * 1000u * FM1_TICKS_PER_US);
}
static void fm1core_mt_halt(int code, const char *why)
{
    printf("%s: %s: halted\n", FM1CORE_ID, why);
    fm1core_exit(code);                   /* (inside the main loop: parked there for good) */
}

/* ------------------------------------------------------------- the main loop --- */
static void fm1core_mt_main(void *arg)
{
    (void)arg;
    /* fm1_cstart's records of a power-on (the rest of it is the chip's: clocks, the vectors, the RAM's copies) */
    bootguard.magic = BOOTGUARD_MAGIC;
    bootguard.failed = 0;
    bootguard.pending = 1;
    diag.magic = DIAG_MAGIC;
    diag.n = 0;
    fm1_main(0);
}
static int fm1core_mt_running(void) { return fm1core_mt_booted && !fm1core_halted() && !fm1coro_done(fm1core_mt_co); }
static void fm1core_mt_pass(void)
{
    if (fm1coro_resume(fm1core_mt_co) <= 0 && !fm1core_halted()) {
        printf("%s: the main loop returned: halted\n", FM1CORE_ID);
        fm1core_exit(0);
    }
}

/* one device millisecond: TIMER4, TIMER5's interrupt ten times (the key scan is the glue's; fm1_ms; usb_poll and the
 * TRS poll every fifth) */
static void fm1core_mt_clock_ms(void)
{
    uint32_t i;
    for (i = 0; i < 10u; i++) {
        fm1core_mt_t4 += 100u * FM1_TICKS_PER_US;
        if (fm1core_mt_t5_on)
            fm1_timer5_irq(fm1core_mt_isr_regs);
    }
    if (usb.ota_req) {                    /* the update key: no update inside a core */
        usb.ota_req = 0;
        printf("%s: the update command (F0 22 24 35 7F F7): ignored (no update inside a core)\n", FM1CORE_ID);
    }
    if (usb.uboot_req) {                  /* the UBOOT key: the chip's USB boot, not a core's */
        usb.uboot_req = 0;
        printf("%s: UBOOT requested over USB: ignored (no update inside a core)\n", FM1CORE_ID);
    }
    fm1core_mt_devms++;
}

/* ------------------------------------------------------------------ input --- */
/* the panel into fm1_in as the scan leaves it: a tap (down and up within the ms) held for this ms, so the next pass
 * sees its release */
static void fm1core_mt_in(void)
{
    fm1panel_t *p = FM1CORE_MT_PANEL;
    const uint32_t b = (p->buttons | fm1p_take_button_taps(p)) & 0x3FFFu;
    const uint32_t k = (p->keys | fm1p_take_key_taps(p)) & ((1u << 27) - 1u);
    const uint32_t kd = k & ~fm1_in.notes, ku = fm1_in.notes & ~k;
    uint32_t n;
    fm1_in.pressed |= b & ~fm1_in.buttons;
    fm1_in.released |= fm1_in.buttons & ~b;
    fm1_in.buttons = b;
    for (n = 0; n < 27u; n++) {           /* (the note keys' queue: key n | 0x80 pressed, as fm1__key writes it) */
        const uint32_t bit = 1u << n;
        if (!((kd | ku) & bit))
            continue;
        if ((uint8_t)(fm1_in.kq_w - fm1_in.kq_r) < FM1_KQ) {
            fm1_in.kq[fm1_in.kq_w % FM1_KQ] = (uint8_t)(n | ((kd & bit) ? 0x80u : 0u));
            fm1_in.kq_w++;
        }
    }
    fm1_in.notes_pressed |= kd;
    fm1_in.notes = k;
    for (n = 0; n < FM1_NENC; n++)
        fm1_in.enc_steps[n] = (int16_t)(fm1_in.enc_steps[n] + fm1p_take_enc(p, n));
    if (p->master > 0)
        fm1core_mt_master = p->master > 1023 ? 1023 : p->master;
}

/* ----------------------------------------------------------------- output --- */
/* the breath (fm1_input.h): frames of ~1.1 ms, fm1_br_step a frame, 0 and 1023 dark, 512 the peak; the level a
 * smoothstep squared x the peak (~60 % of lit, ~30 % with DIM LO) */
static uint8_t fm1core_mt_breath(void)
{
    static uint32_t ph, last_ms, acc;
    uint32_t x, s;
    acc += (fm1_ms - last_ms) * 10u;
    last_ms = fm1_ms;
    while (acc >= 11u) {
        acc -= 11u;
        ph = (ph + fm1_br_step) & 1023u;
    }
    x = ph < 512u ? ph : 1023u - ph;      /* 0..511 */
    s = x * x * (3u * 512u - 2u * x) / 512u / 512u;                  /* smoothstep, 0..511 */
    return (uint8_t)(s * s / 511u * (fm1core_mt_dim_level == 1u ? 77u : 153u) / 511u);
}
static void fm1core_mt_out(void)
{
    fm1panel_t *p = FM1CORE_MT_PANEL;
    const uint8_t *planes[4] = {fm1_led, fm1_led_mid, fm1_led_dim, fm1_led_breath};
    uint8_t levels[4] = {FM1P_LED_LIT, FM1P_LED_MID, FM1P_LED_DIM, 0};
    levels[2] = fm1core_mt_dim_level == 2u ? 64u : fm1core_mt_dim_level == 1u ? FM1P_LED_FAINT : FM1P_LED_DIM;
    levels[3] = fm1core_mt_breath();
    fm1p_leds_planes(p, planes, levels, 4, FM1P_PLAY_GREEN_POS);
    if (fm1core_mt_lcd_writes != fm1core_mt_lcd_seen) {
        if (fm1core_mt_lcd_on && fm1core_mt_bl)
            memcpy(fm1core_mt_lcd_shown, fm1core_mt_lcd, sizeof fm1core_mt_lcd_shown);
        else
            memset(fm1core_mt_lcd_shown, 0, sizeof fm1core_mt_lcd_shown);
        fm1core_mt_lcd_seen = fm1core_mt_lcd_writes;
        fm1p_lcd_out(p, fm1core_mt_lcd_shown, fm1core_mt_lcd_writes);
    }
}

/* ------------------------------------------------------------ the entry points --- */
int fm1core_mt_halted(void) { return fm1core_halted(); }
void fm1core_mt_shutdown(void) { fm1core_run_exit(); }
void fm1core_mt_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (every write reaches the image at once) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, NULL);
}
void fm1core_mt_init(int demo)
{
    fm1panel_t *p = FM1CORE_MT_PANEL;
    (void)demo;
    if (fm1core_mt_booted)
        return;
    fm1core_exit_cleanup = fm1nor_close;
    fm1core_exit_park = fm1coro_park;
    fm1nor_open();
    fm1p_panel_default(p);
    if (p->master <= 0)
        p->master = 512;                  /* (none given: main.c's master_knob, 512) */
    fm1core_mt_co = fm1coro_new(fm1core_mt_main, NULL, 0);
    if (!fm1core_mt_co) {
        printf("%s: no memory for the main loop's stack: halted\n", FM1CORE_ID);
        fm1core_exit(1);
        return;
    }
    fm1core_mt_booted = 1;
    fm1core_mt_in();                      /* (held at power-on: OCT- + OCT+ is update mode, SAVE safe mode) */
    fm1core_mt_pass();                    /* the boot, up to its first wait */
    fm1core_mt_out();
    p->ready = 1;
}
void fm1core_mt_tick(uint32_t ms)
{
    (void)ms;                             /* (one device ms per tick: the clock is TIMER4's) */
    if (!fm1core_mt_running())
        return;
    fm1core_mt_in();
    fm1core_mt_clock_ms();
    fm1core_mt_pass();
}
void fm1core_mt_frame(void)
{
    if (fm1core_mt_running())
        fm1core_mt_out();
}
void fm1core_mt_idle(void)
{
    if (fm1core_mt_running())
        fm1core_mt_out();
}
void fm1core_mt_audio(int16_t *out, uint32_t frames)
{
    while (frames) {
        const uint32_t k = frames < HALF_FRAMES ? frames : HALF_FRAMES;
        uint32_t i;
        if (!fm1core_mt_running() || !fm1core_mt_audio_on || fm1nor_stall_take()) {
            memset(out, 0, (size_t)k * 2u * sizeof *out);
        } else {
            const int32_t *o;
            fm1core_mt_half ^= 1u;        /* (the half the hardware plays next) */
            fm1_alnk0_irq();
            o = &abuf[fm1core_mt_half * HALF_WORDS];
            for (i = 0; i < 2u * k; i++) {
                const int32_t v = o[i] >> UAC_IN_SHIFT;
                out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
        out += 2u * k;
        frames -= k;
    }
}
int fm1core_mt_midi_in(uint32_t pkt)
{
    if (!fm1core_mt_running())
        return 1;                         /* (halted: taken and dropped, never "retry") */
    if (m1_emu_mb.in_w - m1_emu_mb.in_r >= EMB_N)
        return 0;                         /* (the mailbox is full: usb_poll empties it at its pace) */
    m1_emu_mb.in[m1_emu_mb.in_w % EMB_N] = pkt;
    m1_emu_mb.in_w++;
    return 1;
}
int fm1core_mt_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted() || m1_emu_mb.out_r == m1_emu_mb.out_w)
        return 0;
    *pkt = m1_emu_mb.out[m1_emu_mb.out_r % EMB_N];
    m1_emu_mb.out_r++;
    return 1;
}
void fm1core_mt_dump(void)
{
    printf("%s %s: ms %u, page %u, patch %u '%s' (%u bytes), transport %u, octave %d, master %d, engine %s; flash "
           "writes %u, erases %u; main loop stack %u B, %u passes; midi in %u out %u\n",
           FM1CORE_ID, FM1CORE_VERSION, (unsigned)fm1_ms, (unsigned)page, (unsigned)cur_preset,
           patch_label ? patch_label : "", (unsigned)m1_cur_len, (unsigned)eng_transport_running(), (int)octave,
           (int)fm1core_mt_master, eng_run ? "running" : "stopped", (unsigned)fm1nor_dirty(),
           (unsigned)fm1nor_stalls(), (unsigned)fm1coro_stack_peak(fm1core_mt_co),
           (unsigned)fm1coro_resumes(fm1core_mt_co), (unsigned)(m1_emu_mb.in_w - m1_emu_mb.in_r),
           (unsigned)(m1_emu_mb.out_w - m1_emu_mb.out_r));
}
void fm1core_mt_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    if (shed)
        *shed = eng_shed_count;
    if (cpu_pct)
        *cpu_pct = 0;                     /* (no CPU time here) */
}
void fm1core_mt_ui_info(char *buf, uint32_t n)
{
    if (!buf || !n)
        return;
    if (!fm1core_mt_booted) {
        snprintf(buf, n, "off");
        return;
    }
    snprintf(buf, n, "page %u, patch %u '%s', transport %u%s", (unsigned)page, (unsigned)cur_preset,
             patch_label ? patch_label : "", (unsigned)eng_transport_running(), booting ? ", booting" : "");
}
const int8_t *fm1core_mt_keymap(void) { return &FM1_KEYMAP[0][0]; }
uint8_t *fm1core_mt_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_mt_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_mt_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
/* make the image current: every write reaches it at once but the global tempo's settings record, which main.c writes 5 s
 * after the last change with the transport stopped (tempo_poll). 0: still due (the transport runs) */
int fm1core_mt_flash_sync(void)
{
    if (!fm1core_mt_running() || !gt_due)
        return 1;
    if (eng_transport_running())
        return 0;
    gt_due = 0;
    if (flash_ok && !boot_safe)
        settings_store();
    return 1;
}

/* the module's unload: the main loop's stack */
__attribute__((destructor)) static void fm1core_mt_fini(void)
{
    if (fm1core_mt_co && !fm1coro_inside()) {
        fm1coro_free(fm1core_mt_co);
        fm1core_mt_co = NULL;
    }
}
