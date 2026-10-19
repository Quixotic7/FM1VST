/* SPDX-License-Identifier: GPL-3.0-only */
/* The "plat" family's HAL half (core-glue/plat/README.md): what Charles Vestal's plat.h firmwares (X0X, FoMni and its
 * forks FiMba-1 and Fart Machine One) take from Felucca's HAL, on the host. Their own device files sit on top of it
 * unchanged: app/panel.c (the panel calibration, the label -> matrix map) and app/plat_fm1.c (plat.h on the HAL: the
 * buttons by label, the encoders by role, the LED picture with its glow, PLAY's green LED, the MIDI rings, the store),
 * and Felucca's storage.c (the A/B sector objects) on core-glue/common/nor.c's 1 MiB image.
 *
 * Included by a firmware unit (core_<id>.c) after fm1core.h, nor.h, ticks.h and panel.h, before the firmware's
 * sources. It is the device's HAL as the firmware sees it:
 *   time      fm1_ms (the device ms the ABI's tick gives), fm1_ticks (24 MHz TIMER4, from common/ticks.h)
 *   input     fm1_in.buttons / fm1_in.notes (the debounced matrix: a press at once, a release after 9 ms held, as
 *             Felucca's fm1_input_tick), fm1_enc_take (detents per matrix encoder), fm1_input_edges (presses)
 *   LEDs      fm1_led[] / fm1_led_dim[] (packed row bits per column, lit / dim), fm1_led_dim_div (the glow's duty)
 *   LCD       lcd_fill / lcd_blit / lcd_sync into the panel's RAM (RGB565 big-endian, as the ST7789 takes it)
 *   MIDI      usb.c's rings: midi_in_q / mi_r / mi_w (in), midi_out_event (out), 4-byte USB-MIDI packets
 *   flash     flash_ok and storage.c's st_read / st_erase / st_prog on fm1nor_* (an erase stalls the audio)
 * Every name here is the HAL's own (the firmware calls it); the glue's own state is prefixed fm1core_pl_. */
#pragma once

/* ------------------------------------------------------------- time --- */
static volatile uint32_t fm1_ms;          /* the device ms since power-on (fm1core_pl_tick) */
#define FM1_TICKS_PER_US 24u              /* TIMER4 at 24 MHz */
static uint32_t fm1_ticks(void) { return fm1t_ticks24(); }
#if defined FM1CORE_PL_SX_CORO && FM1CORE_PL_SX_CORO
#include "coro.h"
/* where the device feeds its watchdog while it waits (ota.c ota_idle, X0X's rs_ask loops): the SysEx service's
 * coroutine yields, one device ms passes (plat_sx.h); outside the coroutine nothing, as before */
static void fm1_wdt_feed(void) { fm1coro_yield(); }
#else
static void fm1_wdt_feed(void) {}
#endif
static void fm1_delay_ms(uint32_t ms) { (void)ms; }   /* (only panel_setup's, which the core never runs) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static uint32_t irq_save(void) { return 0; }      /* one thread runs the whole device (the ABI's contract) */
static void irq_restore(uint32_t f) { (void)f; }

/* ------------------------------------------------- keys, knobs, LEDs --- */
#define FM1_NCOL 11u
#define FM1_NKEY 41u                              /* key ids: 0..13 matrix buttons, 14..40 note keys */
static const int8_t FM1_KEYMAP[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;
static struct {
    volatile uint32_t buttons, notes;             /* matrix button bits, note key bits: held (debounced) */
} fm1_in;
static int32_t fm1core_pl_enc_acc[FM1P_NENC];     /* detents not yet taken, per matrix encoder */
static uint32_t fm1core_pl_pressed;               /* button press edges not yet taken (fm1_input_edges) */
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1core_pl_enc_acc[e % FM1P_NENC];
    fm1core_pl_enc_acc[e % FM1P_NENC] = 0;
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1core_pl_pressed;
    if (released)
        *released = 0;
    fm1core_pl_pressed = 0;
    return p;
}
static uint8_t fm1_led[FM1P_NCOL], fm1_led_dim[FM1P_NCOL];   /* lit / dim: row bit r of column c */
static volatile uint32_t fm1_led_dim_div;         /* the dim plane's duty, one scan frame in N (0: off) */

/* -------------------------------------------------------------- LCD --- */
static uint16_t fm1core_pl_lcd[FM1P_LCD_W * FM1P_LCD_H];   /* the ST7789's RAM as sent: RGB565 big-endian */
static uint32_t fm1core_pl_lcd_writes;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    const uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (j = y; j < y + h && j < FM1P_LCD_H; j++)
        for (i = x; i < x + w && i < FM1P_LCD_W; i++)
            fm1core_pl_lcd[j * FM1P_LCD_W + i] = s;
    fm1core_pl_lcd_writes++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < FM1P_LCD_W && y + j < FM1P_LCD_H)
                fm1core_pl_lcd[(y + j) * FM1P_LCD_W + x + i] = px[j * w + i];   /* (gfx.c's canvas: swapped) */
    fm1core_pl_lcd_writes++;
}

/* ------------------------------------------------- MIDI (usb.c's rings) --- */
#define MQ 256u
static uint32_t midi_in_q[MQ];
static volatile uint32_t mi_w, mi_r;
static uint32_t fm1core_pl_mo_q[MQ];
static volatile uint32_t fm1core_pl_mo_w, fm1core_pl_mo_r;
static void midi_out_event(uint32_t pkt)          /* a host is always there: every packet is queued (full: dropped) */
{
    if (fm1core_pl_mo_w - fm1core_pl_mo_r < MQ)
        fm1core_pl_mo_q[fm1core_pl_mo_w++ % MQ] = pkt;
}
static struct {
    uint8_t up, config, suspended;                /* (plat_battery: a USB host means power) */
} usb = {1, 1, 0};

/* ------------------------------------------------ flash (storage.c's hooks) --- */
static uint8_t flash_ok;                          /* the 1 MiB part answered (always, here) */
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return fm1nor_erase(off); }
static int st_prog(uint32_t off, const void *src, uint32_t n)   /* (fl_write splits at the 256-byte pages) */
{
    return fm1nor_prog_span(off, src, n);
}
