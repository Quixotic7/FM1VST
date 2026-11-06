/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Portions Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
 * The design (the device clock driven by the audio the worklet asks for, the exports below, the saved
 * flash handed back before boot) follows X0X's browser build, web/emu/x0x_web.c of charlesvestal/fm1-x0x
 * (GPL-3.0), a Felucca fork. */
/* RHYTHMER's web unit, for the felucca_web family's glue (fw_core.h). Rhythmer (JazzyRhythmer/Rhythmer) is a fork of
 * JIANT FM whose tree (jiant/) keeps JIANT's host tests (tests/hostsim.c, tests/ui_test.c) but not JIANT's browser
 * unit, web/emu/felucca_web.c (its own browser page runs the package in an instruction-set emulator, fm1-emu). This
 * file is that unit as JIANT had it when Rhythmer forked (JIANT 373125e4, "Emulator: stubs for fm1_clk_320 /
 * fm1_cnum", JIANT 0.9.12's line; the firmware/src it was written for matches Rhythmer's jiant/firmware/src but for
 * Rhythmer's own files), its paths pointed at cores/Rhythmer/jiant, and main.c fm1_main's Rhythmer additions where
 * main.c has them:
 *   - the boot: fl_plain_window_init (the XIP window for the break banks: nothing here, the NOR is RAM), then after
 *     felucca_init six_loop_scan (six_slots.c: the A / B break banks found in the NOR image, 0xA0000.., none on an
 *     erased flash), as fm1_main;
 *   - the frame: six_gen_poll (GEN) and mutate_poll (MUTATE on the bar) between autosave_poll and song_poll, as
 *     fm1_main's loop (JIANT's unit had left mutate_poll out);
 *   - six_slots.c before main.c (felucca.c's order), and hal/fm1_xip.h's fm1_xip_ptr on the NOR array (the banks are
 *     read in place, as through XIP on the device);
 * Unchanged otherwise: the device's sources are unmodified, the first comment below is JIANT's.
 *
 * JIANT's own description of this unit: */
/* Felucca in the browser: the firmware's sources, as the host tests build them (tests/hostsim.c: libc,
 * engines, voices, FX, sequencer; then the UI, main.c, storage and projects, in src/felucca.c's order),
 * compiled to WebAssembly and run in an AudioWorklet against a simulated FM-1:
 *   - the device clock: TIMER4 ticks (24 MHz) and fm1_ms advance with the audio the worklet asks for; the
 *     audio ISR (audio.c fm1_alnk0_irq, the real one) renders each I2S half (HALF_FRAMES) as the DMA would
 *     ask for it, and the main loop (main.c's, below in web_frame) runs every 15 ms, with ui_input on every
 *     millisecond in between as main.c's wait loop calls it;
 *   - the panel: buttons and encoders by their printed labels (panel.c's table maps them to the matrix),
 *     the keys as fm1_in.notes, the LEDs read back from fm1_led / fm1_led_dim through the key matrix;
 *   - the screen: lcd_fill / lcd_blit into a 240 x 240 RGB565 frame (the panel's byte order);
 *   - the flash: a 1 MiB NOR image behind storage.c's hooks (settings, projects, user presets, the user
 *     sample slots read through it as XIP); the page keeps its storage sectors and gives them back at boot.
 * Not here: USB (MIDI, audio, the editor, updates), TRS MIDI, the serial console, the battery (full), the
 * crash / UBOOT paths. Nothing of the firmware changes for this file. */
#include <stdint.h>

#define FELUCCA_WEB 1
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                    /* as src/felucca.c: MENU > USB SERIAL is there (nothing behind it) */
#endif
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "WEB"
#endif

static uint8_t nor[0x100000];            /* the SPI NOR (erased: 0xFF) */
#define main hostsim_main
#include "../../cores/Rhythmer/jiant/tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u             /* TIMER4: 24 MHz */
static uint32_t web_ticks;
static uint32_t fm1_ticks(void) { return web_ticks; }

/* ------------------------------------------------------ audio (ALNK0) --- */
static uint32_t web_half;                /* the half the next render fills */
#define FM1_AUDIO_HALF 1u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return web_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "../../cores/Rhythmer/jiant/firmware/src/audio.c"

/* ------------------------------------------------------------ the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h: key id at (column, row bit) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL], fm1_led_hi[FM1_NCOL];
static uint32_t web_dim_lo;
static void fm1_led_dim_level(uint32_t lo) { web_dim_lo = lo; }
/* the power-on LED sweep (hal/fm1_input.h fm1_led_anim_start, the picture hal/fm1_led_anim.h): its frame from the
 * clock (a scan frame 1.1 ms), the page draws each LED's level (web_anim_levels); at its end, or on a key or a button
 * down, the last picture (the glow everywhere, or dark) and the UI's LEDs from the next frame, as the scan does */
#include "../../cores/Rhythmer/jiant/firmware/hal/fm1_led_anim.h"
static uint32_t web_anim_t0, web_anim_glow, web_anim_run;
static uint32_t web_anim_frame(void) { return (web_ticks - web_anim_t0) / (1100u * FM1_TICKS_PER_US); }
static void fm1_led_anim_start(uint32_t glow)
{
    memset(fm1_led, 0, sizeof fm1_led);
    memset(fm1_led_dim, 0, sizeof fm1_led_dim);
    memset(fm1_led_breath, 0, sizeof fm1_led_breath);
    memset(fm1_led_mid, 0, sizeof fm1_led_mid);
    memset(fm1_led_hi, 0, sizeof fm1_led_hi);
    web_anim_t0 = web_ticks;
    web_anim_glow = glow != 0u;
    web_anim_run = 1;
}
static int fm1_led_anim_on(void)
{
    if (web_anim_run && (web_anim_frame() >= FM1_ANIM_FRAMES || fm1_in.notes || fm1_in.buttons)) {
        uint32_t c, r;
        web_anim_run = 0;
        for (c = 0; c < FM1_NCOL; c++)
            for (r = 1; r < 5u; r++)
                if (web_anim_glow && FM1_KEYMAP[r][c] >= 0)
                    fm1_led_dim[c] |= (uint8_t)(1u << r);
    }
    return (int)web_anim_run;
}
static uint32_t web_pressed, web_released, web_notes_pressed;
static int32_t web_enc_steps[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released)
        *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = web_notes_pressed; web_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = web_enc_steps[e % 7u]; web_enc_steps[e % 7u] = 0; return s; }
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static int fm1_clk_320(void) { return 0; }                /* (0.9.12) the emulator: 360 MHz, one core */
static uint32_t fm1_cnum(void) { return 0; }

/* ----------------------------------------------------------- the screen --- */
static uint16_t web_fb[240 * 240];       /* RGB565 as the panel gets it (high byte first) */
static uint32_t web_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = sw;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = p[j * w + i];
    web_draws++;
}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s)              /* MENU > SCREEN OFF (lcd.c): off, the page dark; on: redrawn before */
{
    if (!s) {
        memset(web_fb, 0, sizeof web_fb);
        web_draws++;
    }
}
static void lcd_wake_now(void) {}
static void lcd_init(void) {}

#include "../../cores/Rhythmer/jiant/firmware/src/gfx.c"
#include "../../cores/Rhythmer/jiant/firmware/src/panel.c"
#include "../../cores/Rhythmer/jiant/firmware/src/ui.c"
#include "../../cores/Rhythmer/jiant/firmware/src/menu_items.c"
#include "../../cores/Rhythmer/jiant/firmware/src/icons.c"
#include "../../cores/Rhythmer/jiant/firmware/src/ui_graph.c"
#include "../../cores/Rhythmer/jiant/firmware/src/ui_draw.c"
#include "../../cores/Rhythmer/jiant/firmware/src/ui_menu.c"
#include "../../cores/Rhythmer/jiant/firmware/src/ui_input.c"
#include "../../cores/Rhythmer/jiant/firmware/src/ui_layer.c"

/* ------------------------------------------------------------ the flash --- */
static uint32_t web_flash_writes;
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    web_flash_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    web_flash_writes++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }             /* the FM-1's part (project.c) */
static void fl_plain_window_init(void) {}
static const uint8_t *fm1_xip_ptr(uint32_t off) { return nor + off; }   /* (Rhythmer) hal/fm1_xip.h: the break banks read
                                                                        * in place (six_slots.c, six_upload.c) */
#define FL_FAR(fn) (fn)
#include "../../cores/Rhythmer/jiant/firmware/src/storage.c"
#include "../../cores/Rhythmer/jiant/firmware/src/upreset.c"
#include "../../cores/Rhythmer/jiant/firmware/src/project.c"

/* ------------------------------------------------- main.c and its HAL --- */
typedef struct { uint32_t magic, count, vec, pc, emu, dbg, rets; } fm1_crash_t;
#define FM1_CRASH_MAGIC 0x43525348u
static fm1_crash_t fm1_crash;                     /* (none: main.c boot_leds runs the sweep) */
static struct { uint8_t p3_rst, wdt_con; uint32_t rst_src; } fm1_boot;
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1];
/* (0.6.80) the RAM code and the low buffers (app.ld, JIANT 0.6.78): none here; each start the same array as its end,
 * so main.c's copy and clear loops do nothing */
#define _hot_start _hot_end
#define _low_start _low_end
uint32_t _hot_end[1], _hot_load[1], _low_end[1];
static uint32_t web_master_adc = 700;   /* MASTER, 0..1023 as the ADC reads it */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620; }
static void fm1_adc_init(void) {}
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}
static void fm1_reboot(void) {}
static void fm1_core1_stop(void) {}               /* (0.6.71: no second core here, FELUCCA_DUAL_CORE 0) */
static void fm1_enter_uboot(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_irq_enable_all(void) {}
static void fm1_irq_init(void) {}
static void fm1_time_init(void) {}
static void fm1_reset_reason(void) {}
static void fm1_wdt_arm(uint32_t v) { (void)v; }
static void fm1_mailbox_clear(void) {}
static void fm1_guard_enable(uint32_t m) { (void)m; }
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
#if FELUCCA_CDC
static void cdc_task(void) {}
#endif
/* main.c's calls into the USB driver (registers): nothing here (usb.c's own functions stay as they are) */
#define usb_poll() ((void)0)
#define usb_start() ((void)0)
#define usb_retry(ms) ((void)(ms))
#define usb_detach() ((void)0)
#include "../../cores/Rhythmer/jiant/firmware/src/master.c"
#include "../../cores/Rhythmer/jiant/firmware/src/six_slots.c"   /* (Rhythmer) felucca.c: before main.c */
#include "../../cores/Rhythmer/jiant/firmware/src/main.c"
#undef usb_poll
#undef usb_start
#undef usb_retry
#undef usb_detach

/* --------------------------------------------------------- the device --- */
static uint32_t web_booted, web_boot_ms, web_last_frame;

static uint32_t web_seed_v = 1u;          /* (JIANT 0.4) the page's random seed before web_boot (worklet.js), else 1 */
/* main.c fm1_main's boot, up to the main loop (no USB, no UART, no panel setup) */
static void web_power_on(void)
{
    fl_plain_window_init();               /* (Rhythmer) main.c: the XIP window for the break banks (none here) */
    persist_boot();
    settings_init();
    prefs_update();                       /* main.c (JIANT 0.6.44): FX LATCH ON by default (0.6.58: here too) */
    usb_serial_apply();
    lcd_init();
    spl_anim = 1;                         /* main.c (JIANT 0.5): the signature written */
    draw_splash();                        /* main.c: the splash (ui_draw.c) */
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    six_loop_scan();                      /* (Rhythmer) main.c: the break banks A / B in the flash */
    master_boot();                        /* main.c (#137): the pot's level from the first block */
    kb_boot_hold = 1;                     /* main.c (#137): the keys silent under the splash */
    audio_init();
    boot_leds();                          /* main.c: the power-on LED sweep, from the scan's start */
    autosave_boot(boot_clean);            /* main.c: the last session's music (1.2, project.c) */
    session_dice(web_seed_v);             /* main.c (JIANT 0.4): this session's macros (the page's seed: web_seed) */
    web_boot_ms = fm1_ms + 30u + SPL_WRITE + SPL_HOLD;   /* main.c: 30 ms + the splash before the first frame */
    web_booted = 1;
}

/* one pass of main.c fm1_main's loop (what the browser has: no USB, editor, UBOOT, console) */
static void web_frame(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    master_poll();
    felucca_dbg.ui_frames++;
    ui_input();
    settings_poll();
    autosave_poll();
    six_gen_poll();                       /* (Rhythmer) main.c: GEN */
    mutate_poll();                        /* (JIANT 0.6.43) main.c: MUTATE on the bar */
    song_poll();
    ui_leds();
    ui_draw();
}

/* output ring, stereo float, filled by web_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;

/* one millisecond of the device: the clock, the audio halves due by now, the main loop */
static void web_ms(void)
{
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {      /* an I2S half: the audio ISR renders it */
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (web_booted && (int32_t)(fm1_ms - web_boot_ms) < 0 && !(fm1_ms % 20u))   /* main.c: the signature written */
        splash_write(SPL_WRITE + SPL_HOLD - (web_boot_ms - fm1_ms));
    if (!web_booted || (int32_t)(fm1_ms - web_boot_ms) < 0)
        return;
    if (web_boot_ms) {                                    /* main.c: the splash goes, the loop starts */
        web_boot_ms = 0;
        kb_boot_hold = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {                /* main.c: a frame, then ui_input until 15 ms are over */
        web_last_frame = fm1_ms;
        web_frame();
    } else {
        ui_input();                                       /* main.c's wait loop */
    }
}

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* the flash: erased, then the page's saved sectors (web_nor), then boot */
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }
EXPORT void web_seed(uint32_t s) { web_seed_v = s; }   /* (JIANT 0.4) the session's macros: a seed of the page's */
EXPORT void web_boot(void)
{
    if (!web_booted)
        web_power_on();
}

/* input, by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023 */
static uint32_t web_btn_labels, web_key_mask;
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    web_btn_labels = labels;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    web_key_mask = mask;
    web_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < NE)
        web_enc_steps[panel.enc[role] % 7u] += n * panel.dir[role];
}
EXPORT void web_master(uint32_t v) { web_master_adc = v > 1023u ? 1023u : v; }
/* a channel message from Web MIDI, as a USB-MIDI packet arrives (usb.c midi_enqueue): 0 = the ring was full */
EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    return (uint32_t)midi_enqueue((status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, 1);
}

/* the LEDs: bit b = the button labelled b lit (bit 14: PLAY's green), keys bit k; the dim glow and the breath alike */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void)
{
    return led_buttons(fm1_led) | ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) << 14;
}
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void) { return led_buttons(fm1_led_dim); }
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim); }
EXPORT uint32_t web_dim_level(void) { return web_dim_lo; }
EXPORT uint32_t web_breath_buttons(void) { return led_buttons(fm1_led_breath); }   /* #119: dark .. ~60 % of lit */
EXPORT uint32_t web_breath_keys(void) { return led_keys(fm1_led_breath); }
EXPORT uint32_t web_mid_keys(void) { return led_keys(fm1_led_mid); }   /* the DRUM grid's beats: a steady mid level */
/* the power-on sweep running: each LED's level now (hal/fm1_led_anim.h: 0..64 /64 of lit, | 0x80 the glow under
 * it), 0..13 the buttons by label, 14..40 the keys F3..G5; 0 when it is not */
EXPORT uint8_t *web_anim_levels(void)
{
    static uint8_t lv[FM1_ANIM_NBTN + FM1_ANIM_NKEY];
    uint32_t f = web_anim_frame(), i;
    if (!fm1_led_anim_on())
        return 0;
    for (i = 0; i < FM1_ANIM_NBTN + FM1_ANIM_NKEY; i++)
        lv[i] = (uint8_t)fm1_anim_level(f, i < FM1_ANIM_NBTN ? (i < NB ? panel.btn[i] : i) : i, web_anim_glow);
    return lv;
}

/* (0.6.91) the song generator from the page or a bench (web/emu/gen_bench.mjs): a genre (0 RANDOM, 1 + the genre's
 * index, 0.8.6: web_gen_count() + 1 EXPLORE), a seed 0..9999, an energy 0..100, a form (0 LOOP, 1 PARTS, 2 SONG); the
 * piece's name after */
EXPORT void web_gen(uint32_t genre, uint32_t seed, uint32_t energy, uint32_t form)
{
    gp.genre = (uint8_t)(genre % (NGENRE + 2u));
    gp.seed = (uint16_t)(seed % 10000u);
    gp.energy = (uint8_t)(energy > 100u ? 100u : energy);
    gp.form = (uint8_t)(form % 3u);
    gen_go();
    gp.sig = gen_sig();
}
EXPORT const char *web_gen_last(void) { return gp.last; }
EXPORT uint32_t web_gen_count(void) { return NGENRE; }
EXPORT const char *web_gen_name(uint32_t i) { return GENRE[i % NGENRE].name; }

/* for the test and the bench (web/emu/emu_test.mjs), a heavy song straight into the tracks (as hostsim's
 * renders set them): T1 FM6 PAD, T2 TRIO (its first preset; PHYS, retired in TONIC, was here), T3 VOICE CHOIR AAH (GRAIN, retired in JIANT, was here), 4-note chords on all 16
 * steps, held; T4 DRUM, kick snare and hats. PLAY is the page's (or the test's) to press */
EXPORT void web_test_heavy(void)
{
    static const uint8_t ENG[3][2] = {{12, 4}, {6, 0}, {5, 0}};   /* (JIANT: VOICE CHOIR where GRAIN was) */
    static const uint8_t CH[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    uint32_t p, i;
    for (p = 0; p < 3u; p++) {
        track_t *t = &trk[p];
        host_preset(t, ENG[p][0], ENG[p][1]);
        t->p[P_VOICE] = V_POLY;
        t->p[P_SUS] = 127;
        for (i = 0; i < 16u; i++) {
            uint8_t n[4], k;
            for (k = 0; k < 4u; k++)
                n[k] = (uint8_t)(CH[i / 4u][k] + 12u * p);
            put_step(t, i, 4, n, ST_NOTE, 0);
        }
    }
    host_drums(&trk[3]);
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        n[k++] = i % 2u ? 42 : 46;
        put_step(&trk[3], i, k, n, ST_NOTE, 0);
    }
    ui.force = 1;
}

EXPORT uint32_t web_test_user_preset(uint32_t i) { return (uint32_t)up_used(i); }

EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return song.playing; }
EXPORT uint32_t web_sample_rate(void) { return FS; }

/* (0.7.1) stems from the browser (web/emu/stems.js): a backup file's objects into this device, as the editor's RESTORE
 * writes them (editor_backup.c ed_bk_commit, not built here): the songs' sections and index into the flash, the user
 * presets, the live music last into RAM; then a song or the live section rendered with parts left out of the mix (fx.c
 * mix_skip: the part still runs, only its sound and sends are left out), each part alone in turn */
EXPORT uint8_t *web_bk_buf(void) { return proj_wire_u.raw; }
EXPORT uint32_t web_bk_max(void) { return (uint32_t)sizeof proj_wire_u.raw; }
EXPORT uint32_t web_bk_put(uint32_t id, uint32_t len)   /* the bytes in web_bk_buf(): 0 = done, 1 unknown, 2 bad, 4 flash */
{
    uint8_t *raw = proj_wire_u.raw;
    if (len > sizeof proj_wire_u.raw) return 2;
    if (id == 0u || (id >= 40u && id < 40u + NSONG * 4u)) {   /* the live music; song s's section k (song 1: the slots) */
        uint32_t s = (id - 40u) / 4u, k = (id - 40u) % 4u;
        if (!len) return 0;
        if (!proj_import(&proj_scratch, raw, (int)len)) return 2;
        proj_bound(&proj_scratch);
        if (id == 0u) return project_restore_runtime(&proj_scratch) ? 2u : 0u;
        if (!proj_pack((project_store_t *)raw, &proj_scratch)) return 2;
        if (!s)                                        /* song 1's sections live in RAM too (the slots) */
            memcpy(&proj_slot[k], raw, sizeof proj_slot[0]);
        return st_save(s ? OBJ_SONG1 + (s - 1u) * 4u + k : OBJ_PROJECT0 + k, raw, sizeof(project_store_t)) ? 4u : 0u;
    }
    if (id == 10u) {                                   /* the song index: the rows and scenes, into RAM too */
        if (len != sizeof song_idx || ((const song_index_t *)raw)->magic != SONG_MAGIC) return 2;
        memcpy(&song_idx, raw, sizeof song_idx);
        song_idx.cur = 0;
        chain_config = song_idx.rows[song_cur];        /* (the rows now, as editor_backup.c) */
        return st_save(OBJ_SONGIDX, raw, len) ? 4u : 0u;
    }
    if (id == 6u || id == 7u) return !len ? 0u : st_save(OBJ_UPRESET0 + id - 6u, raw, len) ? 4u : 0u;   /* user presets */
    if (id == 9u) {                                    /* their FM6 patches */
        if (!len) return 0;
        if (len != sizeof upf || !upf_valid((const upf_t *)raw)) return 2;
        memcpy(&upf, raw, sizeof upf);
        return upf_save() == 2 ? 4u : 0u;
    }
    return id == 1u || id == 8u || (id >= 2u && id <= 5u) ? 0u : 1u;   /* (settings, the retired bank, the current song's
                                                         * sections: the songs' ids carry them) */
}
EXPORT uint32_t web_bk_len(uint32_t id)   /* the object as it is now, into web_bk_buf(): its length, 0 none */
{
    uint8_t *raw = proj_wire_u.raw;
    if (id == 0u) {                                    /* the live music, packed as a section is stored */
        project_capture(&proj_scratch);
        return proj_pack((project_store_t *)raw, &proj_scratch) ? (uint32_t)sizeof(project_store_t) : 0u;
    }
    if (id == 10u) {                                   /* the index with this song's rows as they are now (song_flush) */
        song_idx.rows[song_cur] = chain_config;
        memcpy(raw, &song_idx, sizeof song_idx);
        return (uint32_t)sizeof song_idx;
    }
    if (id >= 40u && id < 40u + NSONG * 4u) {
        uint32_t s = (id - 40u) / 4u, k = (id - 40u) % 4u;
        int n;
        if (!s) {                                      /* song 1's sections: the RAM slots (as editor_backup.c ids 2..5) */
            if (!project_used(k)) return 0;
            memcpy(raw, &proj_slot[k], sizeof proj_slot[0]);
            return (uint32_t)sizeof proj_slot[0];
        }
        n = st_load(OBJ_SONG1 + (s - 1u) * 4u + k, raw, (uint32_t)sizeof proj_wire_u.raw);
        return n > 0 ? (uint32_t)n : 0u;
    }
    return 0;
}
EXPORT void web_mix_skip(uint32_t mask) { mix_skip = (uint8_t)mask; }
EXPORT uint32_t web_song_select(uint32_t n) { return (uint32_t)song_switch(n, 1); }   /* 0 = done (stopped, n < 8) */
EXPORT uint32_t web_song_cur(void) { return song_cur; }
EXPORT uint32_t web_song_rows(void) { return chain_config.count; }
EXPORT uint32_t web_song_bars(void)                    /* the rows' bars, once round (stems.js: a LOOP song's length) */
{
    uint32_t i, n = 0;
    for (i = 0; i < chain_config.count && i < CHAIN_ROWS; i++) n += chain_config.row[i].bars ? chain_config.row[i].bars : 1u;
    return n;
}
EXPORT uint32_t web_song_end(void) { return SONG_END(&chain_config); }   /* 0 once, 2..8 times, 255 round and round */
EXPORT uint32_t web_song_play(void) { return chain_prepare(); }   /* 0 = the song starts (song_main.c's codes) */
EXPORT void web_play(uint32_t on) { transport_req = (uint8_t)(on ? 1u : 2u); }
EXPORT uint32_t web_beat_samples(void) { return beat_samples(); }
EXPORT uint32_t web_trk_live(void) { return trk_live; }
