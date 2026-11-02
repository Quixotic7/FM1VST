/* SPDX-License-Identifier: GPL-3.0-only */
/* RUPTURE (https://github.com/SikkenWorks/RUPTURE, the pinned submodule cores/RUPTURE): the core unit. RUPTURE is a
 * native FM-1 application (vendor/etch-v1.88-source/firmware/fm1-native: bringup.c and the units build.py links
 * with it) on Felucca's HAL, with no emulator of its own. This unit is bringup.c (its reset entry, its main loop,
 * the UI, usb.c, ota.c, the store's flash side, the audio ISR: one translation unit on the device too) over
 * rupture_hal.h and hal/fm1_flash.h (the core's VFS overlay reads them in the place of the device's hal/fm1_*.h,
 * core.cmake), and the glue that translates to and from the ABI's panel (core-api/fm1core.h). The DSP and the
 * codecs (audio_core.c, native_fx.c, .. 15 units) are their own units (core.cmake EXTRA_SOURCES), as build.py
 * builds them.
 *
 * THE FIRMWARE RUNS ON A COROUTINE (core-glue/common/coro.h): fm1_cstart, the device's reset entry, from its first
 * line (its boot: the splash, the store, the project; then the main loop, which never returns). It yields where the
 * device waits for its interrupts (rupture_hal.h: fm1_wdt_feed, fm1_delay_ms) and is resumed once per device ms by
 * frame() / idle(). What runs in its place:
 *   TIMER5 (fm1_timer5_irq: the input scan, etch_ms, usb_poll)   tick(ms): etch_ms / fm1_ms = the device ms, the
 *            panel's keys, buttons and encoder detents into fm1_in, debounced as below; USB is configured
 *   ALNK0 (fm1_alnk0_irq)    audio(): once per 128-frame block, its DMA half (Q15 << 7) out as s16
 *   EP1 (usb_poll's MIDI)    midi_in: usb.c midi_in_event (the MIDI ring, the SysEx frame ota.c answers);
 *                            midi_out_take: usb.c's SysEx reply ring first, then its MIDI out ring
 * Input: the main loop reads the buttons every 25 ms (bringup.c: their level, its own edges) and the audio ISR the
 * keys every block, so a press is held at least FM1CORE_RU_HOLD_MS here (a tap shorter than the main loop's poll
 * would be lost; a finger is never that short). MASTER: the pot, 0..1023.
 * Output: the LEDs as levels (fm1_led lit, fm1_led_mid MID, fm1_led_dim DIM; the first wins), the LCD (lcd.c's
 * window writes decoded, rupture_hal.h).
 * Not here (the hardware): USB audio (ETCH_NATIVE_UAC 0: the device's --usb-audio-48k feeds the computer, a core's
 * audio is its output), the TRS jack, the update session (usb.ota_req: M-UPGRADE's F0 22 24 35 7F F7) and the USB
 * UBOOT key (usb.uboot_req: F0 22 24 35 7D F7): both are dropped as they arrive, the core plays on
 * (core-glue/common/README.md, sysex). OCT- + OCT+ held 5 s (UBOOT), OCT- held at power-on, a second failed boot in
 * a row (the boot guard: boot_options): exit 3, the core halts. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) */
#include "version.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

/* ---- what the device's linker script gives fm1_cstart: its copy loops (.bss, .pool, .data, .ram_text) run over
 * nothing here (a core's RAM is fresh, its data initialised, at each power-on) ---- */
static uint32_t fm1core_ru_lnk[1];
#define _bss_start fm1core_ru_lnk
#define _bss_end fm1core_ru_lnk
#define _pool_start fm1core_ru_lnk
#define _pool_end fm1core_ru_lnk
#define _data_load fm1core_ru_lnk
#define _data_start fm1core_ru_lnk
#define _data_end fm1core_ru_lnk
#define _rt_load fm1core_ru_lnk
#define _rt_start fm1core_ru_lnk
#define _rt_end fm1core_ru_lnk

/* libc.c is the device's (freestanding, included by bringup.c): its memset / memcpy / memcmp kept off the host's */
#define memset rupture_memset
#define memcpy rupture_memcpy
#define memcmp rupture_memcmp
#include "bringup.c"                      /* (the patched copy: core.cmake) */
#undef memset
#undef memcpy
#undef memcmp

#include "exit_end.h"

/* ---------------------------------------------------------------- state --- */
#define FM1CORE_RU_HOLD_MS 40u            /* a press lasts at least this long (see the top) */
fm1panel_t fm1core_ru_panel;
#define FM1CORE_RU_PANEL (&fm1core_ru_panel)
static fm1p_edges_t fm1core_ru_keys_e, fm1core_ru_btn_e;   /* (taps: a press this tick, the release the next) */
static uint32_t fm1core_ru_in_btn, fm1core_ru_in_key;      /* the debounced state the tick delivers */
static uint32_t fm1core_ru_btn_ms[32], fm1core_ru_key_ms[32];
static fm1coro_t *fm1core_ru_co;          /* the firmware: fm1_cstart */
static uint8_t fm1core_ru_guard_set;
static uint32_t fm1core_ru_blocks, fm1core_ru_dropped;

/* ---------------------------------------------------------------- flash --- */
void fm1core_ru_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (nothing is kept in RAM to save at exit: SAVE writes the store) */
    fm1core_exit_cleanup = fm1nor_close;  /* (exit: the flash file closed, later writes RAM only) */
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}
/* the boot guard (boot_guard.h, .noinit on the device): a run that died before it was stable (30 s of healthy
 * audio) leaves pending set; the next boot counts it, and the second failed boot in a row starts in SAFE MODE.
 * reason and stage are accepted and ignored (no reset reasons) */
int fm1core_ru_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    etch_boot.magic = ETCH_BOOT_MAGIC;
    etch_boot.pending = fail >= 0 ? 1u : 0u;
    etch_boot.failed = fail >= 0 ? (uint32_t)fail : 0u;
    fm1core_ru_guard_set = 1;
    return 1;
}

/* ------------------------------------------------------------- power-on --- */
static void fm1core_ru_main(void *arg)    /* the coroutine: the device from its reset vector on */
{
    (void)arg;
    fm1_cstart();                         /* (never returns: the main loop, or exit() parks it) */
}

static void fm1core_ru_out(void)
{
    fm1panel_t *p = FM1CORE_RU_PANEL;
    static const uint8_t *const planes[3] = {fm1_led, fm1_led_mid, fm1_led_dim};
    static const uint8_t levels[3] = {FM1P_LED_LIT, FM1P_LED_MID, FM1P_LED_DIM};
    fm1p_leds_planes(p, planes, levels, 3, FM1P_PLAY_GREEN_POS);
    fm1p_lcd_out(p, fm1core_ru_lcd, fm1core_ru_lcd_writes);
}

void fm1core_ru_init(int demo)
{
    fm1panel_t *p = FM1CORE_RU_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1core_exit_park = fm1coro_park;     /* (an exit() on the firmware's coroutine: it is never resumed) */
    fm1p_panel_default(p);                /* the FM-1's factory wiring: RUPTURE reads Felucca's ids (OCT- 0, OCT+ 1,
                                           * SEL 3, GLO 7, HOME 8, SEQ 11, PLAY 12, REC 13; K1 encoder 2 ..) */
    if (p->master)
        fm1core_ru_master = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    fm1t_set_ms(0);
    fm1core_ru_ms = 0;
    if (!fm1core_ru_guard_set)            /* a power-on: no history (bringup.c's first boot_begin sets the magic) */
        memset(&etch_boot, 0, sizeof etch_boot);
    fm1nor_open();                        /* (common/nor.c: erased, then the stage or the file) */
    fm1core_ru_co = fm1coro_new(fm1core_ru_main, NULL, 0);
    if (!fm1core_ru_co) {
        printf("%s: no memory for the firmware's stack\n", FM1CORE_ID);
        exit(1);
        return;
    }
    fm1coro_resume(fm1core_ru_co);        /* (up to its first wait: fm1_cstart's 40 ms after TIMER5 starts) */
    if (fm1core_halted())
        return;
    if (!p->master)
        p->master = fm1core_ru_master;
    p->ready = 1;
    fm1core_ru_out();
}

/* ------------------------------------------------------- the 1 ms timer --- */
void fm1core_ru_tick(uint32_t ms)
{
    fm1panel_t *p = FM1CORE_RU_PANEL;
    uint32_t i, b, k, down;
    if (fm1core_halted())
        return;
    fm1t_set_ms(ms);
    fm1core_ru_ms = ms;
    etch_ms = ms;                         /* (TIMER5's: native_irq_bridge.h counts it from TIMER4) */
    fm1_ms = ms;
    if (usb.up && !usb.detached) {        /* a host is there, configured: MIDI out flows, the USB bolt shows */
        usb.config = 1;
        usb.suspended = 0;
    }
    fm1core_ru_master = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    b = fm1p_edges(&fm1core_ru_btn_e, p->buttons, fm1p_take_button_taps(p), NULL, NULL);
    k = fm1p_edges(&fm1core_ru_keys_e, p->keys, fm1p_take_key_taps(p), NULL, NULL);
    down = b & ~fm1core_ru_in_btn;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_ru_btn_ms[i] = ms;
        else if ((fm1core_ru_in_btn & m) && !(b & m) && ms - fm1core_ru_btn_ms[i] < FM1CORE_RU_HOLD_MS)
            b |= m;
    }
    fm1_in.released |= fm1core_ru_in_btn & ~b;
    fm1_in.pressed |= down;
    fm1core_ru_in_btn = b;
    down = k & ~fm1core_ru_in_key;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_ru_key_ms[i] = ms;
        else if ((fm1core_ru_in_key & m) && !(k & m) && ms - fm1core_ru_key_ms[i] < FM1CORE_RU_HOLD_MS)
            k |= m;
    }
    fm1_in.notes_pressed |= down;
    fm1core_ru_in_key = k;
    fm1_in.buttons = fm1core_ru_in_btn;
    fm1_in.notes = fm1core_ru_in_key;
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_steps[i] = (int16_t)(fm1_in.enc_steps[i] + fm1p_take_enc(p, i));
    fm1_in.frames++;
}

/* ------------------------------------------------------- the main loop --- */
static void fm1core_ru_run(void)          /* frame and idle: this device ms of the firmware */
{
    if (fm1core_halted())
        return;
    if (fm1core_ru_co && !fm1coro_done(fm1core_ru_co))
        fm1coro_resume(fm1core_ru_co);
    if (!fm1core_halted())
        fm1core_ru_out();
    else
        fm1p_lcd_out(FM1CORE_RU_PANEL, fm1core_ru_lcd, fm1core_ru_lcd_writes);   /* (the last screen) */
}
void fm1core_ru_frame(void) { fm1core_ru_run(); }
void fm1core_ru_idle(void) { fm1core_ru_run(); }

/* ---------------------------------------------------------------- audio --- */
void fm1core_ru_audio(int16_t *out, uint32_t frames)
{
    uint32_t f, i;
    if (fm1core_halted()) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        int16_t *o = out + 2u * f;
        if (fm1nor_stall_take() || !fm1core_ru_audio_on || !fm1core_ru_dma) {   /* an erase: IRQs off */
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        fm1core_ru_half ^= 1u;            /* the half the DMA has just finished playing: the free one */
        fm1_alnk0_irq();
        fm1core_ru_blocks++;
        if (!fm1core_ru_pc6) {            /* the analog gate (PC6) still low: nothing reaches the jack */
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        {
            const int32_t *d = &fm1core_ru_dma[fm1core_ru_half * ETCH_NATIVE_FRAMES * 2u];
            for (i = 0; i < FM1P_BLOCK * 2u; i++) {
                int32_t v = d[i] / 128;   /* (Q15 << 7, audio_target.c) */
                o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
    }
    if (f < frames)
        memset(out + 2u * f, 0, (size_t)(frames - f) * 2u * sizeof *out);
}

/* ----------------------------------------------------------------- MIDI --- */
/* usb.c's EP1 OUT (ep1_take, midi_in_event): refused (0, "retry") while the ring is short of room (EP1_ROOM, as
 * ep1_take), or while a SysEx frame waits for the main loop and this packet starts another (usb.c would drop it);
 * the update request and the UBOOT key are dropped as they complete (see the top) */
int fm1core_ru_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 15u;
    if (fm1core_halted())
        return 1;
    if (MQ - (mi_w - mi_r) < EP1_ROOM)
        return 0;
    if (cin >= 4u && cin <= 7u && ((pkt >> 8) & 0xFFu) == 0xF0u && sx_ready)
        return 0;
    midi_in_event(pkt);
    if (usb.ota_req) {
        usb.ota_req = 0;
        fm1core_ru_dropped++;
        printf("%s: M-UPGRADE update request ignored (no update inside a core)\n", FM1CORE_ID);
    }
    if (usb.uboot_req) {
        usb.uboot_req = 0;
        fm1core_ru_dropped++;
        printf("%s: UBOOT key over USB ignored (no ROM loader in a core)\n", FM1CORE_ID);
    }
    return 1;
}
/* usb.c's EP1 IN (ep1_tx): the SysEx replies first, never split by notes; no notes while a reply is being queued */
int fm1core_ru_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    if (so_r != so_w) {
        *pkt = sx_out_q[so_r % SXQ];
        so_r++;
        return 1;
    }
    if (sx_busy || mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}

/* ---------------------------------------------------------- diagnostics --- */
void fm1core_ru_dump(void)
{
    uint32_t c;
    printf("fm1_in: notes %07X buttons %04X  etch_ms %u  page %s%s%s%s\n", (unsigned)fm1_in.notes,
           (unsigned)fm1_in.buttons, (unsigned)etch_ms, ru_page < RU_PAGE_COUNT ? ru_page_names[ru_page] : "?",
           native_safe ? ", SAFE MODE" : "", native_tripped ? ", audio tripped" : "",
           native_updating ? ", updating" : "");
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X/%02X", fm1_led[c], fm1_led_mid[c], fm1_led_dim[c]);
    printf("  (lit/mid/glow per column)\n");
    printf("  audio: %s, PC6 %s, %u blocks, %u halves, %u late; store status %d; project %u\n",
           fm1core_ru_audio_on ? "on" : "off", fm1core_ru_pc6 ? "on" : "off", (unsigned)fm1core_ru_blocks,
           (unsigned)native_halves, (unsigned)native_late, native_store_status, (unsigned)ru_project_slot);
    printf("  midi in %u out %u  sysex out %u%s  dropped requests %u\n", (unsigned)(mi_w - mi_r),
           (unsigned)(mo_w - mo_r), (unsigned)(so_w - so_r), sx_ready ? ", a frame waiting" : "",
           (unsigned)fm1core_ru_dropped);
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks)\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks());
    printf("  the firmware's coroutine: %u resumes, its stack's peak %u of %u bytes\n",
           (unsigned)fm1coro_resumes(fm1core_ru_co), (unsigned)fm1coro_stack_peak(fm1core_ru_co),
           (unsigned)FM1CORO_STACK);
}
void fm1core_ru_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%s%s", ru_page < RU_PAGE_COUNT ? ru_page_names[ru_page] : "?",
             native_safe ? " (SAFE MODE)" : "");
}
void fm1core_ru_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = native_late;
    *cpu_pct = 0;
}
const int8_t *fm1core_ru_keymap(void) { return &FM1_KEYMAP[0][0]; }
int fm1core_ru_halted(void) { return fm1core_halted(); }
void fm1core_ru_shutdown(void)
{
    fm1core_run_exit();
    if (fm1core_ru_co)
        fm1coro_free(fm1core_ru_co);
    fm1core_ru_co = NULL;
}

/* ---- the flash (fm1core.h ABI 2): common/nor.c ---- */
uint8_t *fm1core_ru_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_ru_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_ru_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_ru_flash_sync(void) { return 1; }   /* (SAVE writes the store at once; AUTO SAVE is the firmware's own) */
