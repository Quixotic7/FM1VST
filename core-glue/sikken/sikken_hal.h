/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL for SikkenWorks' native cores (ETCH, RUPTURE): what the device's hal/fm1_*.h headers (Felucca's HAL,
 * as ETCH's firmware/fm1-native/hal and RUPTURE's vendor/etch-v1.88-source/firmware/fm1-native/hal ship it) give
 * bringup.c, on the host. The core's clang VFS
 * overlay (sikken.cmake) reads THIS file in the place of each of them except hal/fm1_flash.h (its own overlay,
 * hal/fm1_flash.h here), hal/fm1_cc.h, hal/fm1_gpio.h and hal/fm1_led_anim.h (pure: macros and tables, kept). Neither
 * firmware has an emulator; its HAL is Tonitor's (core-glue/tonitor/tonitor_hal.h), and so is this header:
 *   - time: the device ms the ABI's tick gave (fm1core_sk_ms), fm1_ticks TIMER4 at 24 MHz (common/ticks.h):
 *     simulated time, which never moves inside a call;
 *   - WAITS ARE YIELDS: fm1_cstart (the boot and the main loop, which never return) runs on a coroutine
 *     (sikken_glue.h), and where the device waits for its interrupts (fm1_wdt_feed at the top of the main loop and
 *     in the wave compiler, fm1_delay_ms in the splash) the clock goes back to the host: the audio ISR and the
 *     timer run, and the loop goes on at the next device ms. A feed right after a flash operation (the store's
 *     256-byte reads, an erase, a page) does not yield: on the device those take microseconds, here a yield each
 *     would stretch the boot's catalogue read over seconds. Outside the coroutine the waits return at once;
 *   - one thread runs the whole device (the ABI's threading contract): fm1_irq_off / on only count;
 *   - input: fm1_in holds the debounced state sikken_glue.h's tick delivers, the encoder detents fm1_enc_take takes;
 *   - LEDs: fm1_led (lit), fm1_led_mid (1 frame in 8), fm1_led_dim (the glow), as levels in sikken_glue.h;
 *   - the LCD: lcd.c (the ST7789 driver) is built unmodified over the SPI half below, which decodes what it sends
 *     (CASET / RASET / RAMWR and the pixels) into fm1core_sk_lcd, the panel's RAM (big-endian RGB565 as sent);
 *   - audio: audio_target.c's fm1_alnk0_irq is called once per 128-frame ABI block (its DMA half) by sikken_glue.h;
 *   - USB: usb.c is built, its SIE never answers (fm1_usb_sie_on 0): usb_start leaves usb.up set, the glue sets
 *     usb.config (a host is there) and takes the place of EP1 (midi_in / midi_out_take) on usb.c's own rings;
 *   - the TRS jack (UART1 + DMA): nothing arrives; the flash: hal/fm1_flash.h, core-glue/common/nor.c's 1 MiB image.
 * Every name here is the firmware's own (the device HAL's); glue-only names carry fm1core_sk_ / FM1CORE_SK_. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#define FM1_INLINE static inline __attribute__((always_inline))   /* (hal/fm1_cc.h's, the same) */
#include "coro.h"                                  /* (core-glue/common) the main loop's coroutine */
#include "ticks.h"
#include "nor.h"

/* ------------------------------------------------------------------ time --- */
static volatile uint32_t fm1core_sk_ms;            /* the device ms (the ABI's tick); etch_ms / fm1_ms follow it */
static uint8_t fm1core_sk_flash_op;                /* a flash operation just ran: the next feed does not yield */
#define FM1_TICKS_PER_US 24u                       /* TIMER4 at 24 MHz (fm1_time.h) */
#define FM1_T4_CNT (fm1t_ticks24())
static void fm1_time_init(void) {}
static uint32_t fm1_ticks(void) { return fm1t_ticks24(); }
static uint32_t fm1_micros(void) { return fm1t_us(); }
static void fm1_delay_us(uint32_t us) { (void)us; }
static void fm1_delay_ms(uint32_t ms)              /* on the main loop's coroutine: until that many device ms passed */
{
    const uint32_t t0 = fm1core_sk_ms;
    if (!fm1coro_inside())
        return;
    while ((uint32_t)(fm1core_sk_ms - t0) < ms)
        fm1coro_yield();
}

/* ------------------------------------------------- the system (fm1_sys.h) --- */
static uint32_t fm1_p33_timeouts;
static uint8_t fm1_p33_read(uint32_t a)
{
    (void)a;
    return 0;
}
static void fm1_p33_write(uint32_t a, uint8_t d) { (void)a; (void)d; }
static void fm1_p33_or(uint32_t a, uint8_t d) { (void)a; (void)d; }
static void fm1_p33_and(uint32_t a, uint8_t d) { (void)a; (void)d; }
static void fm1_wdt_arm(uint8_t t) { (void)t; }
static void fm1_wdt_feed(void)                     /* where the firmware waits for its interrupts: a yield */
{
    if (fm1core_sk_flash_op) {                     /* (a feed after a flash operation: on at once, see the top) */
        fm1core_sk_flash_op = 0;
        return;
    }
    if (fm1coro_inside())
        fm1coro_yield();
}
static void fm1_wdt_stop(void) {}
static struct {
    uint8_t p3_rst;                                /* bit0 power-on: every power-on of a core is one */
    uint32_t rst_src;
    uint8_t wdt_con;
} fm1_boot = {1u, 0u, 0u};
static void fm1_reset_reason(void) {}
/* a reset or the update mode ends the core's "process": exit() is common/exit.h's (the core halts; the host reloads
 * it to power on again). 2: a reboot, 3: UBOOT, the update (an installer's) */
static void fm1_reboot(void) { exit(2); }
static void fm1_enter_uboot(void) { exit(3); }
static void fm1_core_reset(void) { exit(2); }
static void fm1_enter_update(const uint8_t *parm)
{
    (void)parm;
    exit(3);
}
static void fm1_updata_parm_clear(void) {}
static void fm1_mailbox_clear(void) {}
static int fm1_mem_readable(uint32_t a, uint32_t n)
{
    (void)a;
    (void)n;
    return 0;
}
static uint8_t fm1_peek8(uint32_t a)
{
    (void)a;
    return 0;
}

/* ------------------------------------------------- interrupts (one thread) --- */
#define FM1_IRQ_TARGET 1
enum { FM1_IRQ_EXCEPTION = 1, FM1_IRQ_ALNK0 = 11, FM1_IRQ_SPI1 = 16, FM1_IRQ_UART1 = 20,
       FM1_IRQ_SARADC = 24, FM1_IRQ_SPI2 = 37, FM1_IRQ_TIMER5 = 63, FM1_IRQ_SOFT0 = 120 };
#define FM1_CRASH_MAGIC 0x43525348u
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;
} fm1_crash_t;
static fm1_crash_t fm1_crash;                      /* (bringup.c fm1_fault: never called, no CPU exceptions here) */
static uint32_t fm1core_sk_irq_depth;
static uint32_t fm1_icfg(void) { return 0; }
static void fm1_icfg_set(uint32_t v) { (void)v; }
static void fm1_irq_off(void) { fm1core_sk_irq_depth++; }
static void fm1_irq_on(void) { fm1core_sk_irq_depth = 0; }   /* (sti: on, however deep) */
static void fm1_irq_init(void) {}
static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)
{
    (void)n;
    (void)h;
    (void)prio;
}
static void fm1_irq_mask(uint32_t n) { (void)n; }
static void fm1_irq_enable_all(void) { fm1_irq_on(); }

/* --------------------------------------------------------- the guards --- */
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
static void fm1_guard_enable(uint32_t which) { (void)which; }
static void fm1_guard_lock_top(void) {}
static void fm1_guard_unlock_top(void) {}

/* ----------------------------------------------------------- the timers --- */
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio)
{
    (void)isr;
    (void)prio;
}
void isr_timer5(void) {}                           /* (fm1_isr.S's wrappers: never entered) */
void isr_alnk0(void) {}

/* --------------------------------------------------- keys, knobs, LEDs --- */
#define FM1_INPUT_IDLE() ((void)0)
#define FM1_INPUT_NOW() fm1_ticks()
#define FM1_DEB_PRESS 2u
#define FM1_DEB_RELEASE 8u                         /* frames open in a row: a release (~9 ms) */
#define FM1_NCOL 11u
#define FM1_NKEY 41u                               /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
#define FM1_LED_MID_N 8u
/* key id at (physical column, packed row bit), -1 = none (fm1_input.h) */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
static volatile struct {
    uint32_t notes;                                /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;                              /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;                    /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;                        /* note-key press edges since the last fm1_input_note_edges() */
    uint8_t raw[FM1_NCOL];
    uint8_t cnt[FM1_NKEY];
    uint32_t note_t0[27];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];
    uint16_t enc_still[FM1_NENC];
    int8_t enc_sub[FM1_NENC];
    int16_t enc_steps[FM1_NENC];                   /* + = clockwise */
    int8_t enc_dir[FM1_NENC];
    uint32_t enc_dfr[FM1_NENC];
    uint32_t frames;
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];                  /* lit: packed row bits per column */
static uint8_t fm1_led_dim[FM1_NCOL];              /* the glow */
static uint8_t fm1_led_breath[FM1_NCOL];           /* (unused by ETCH and RUPTURE) */
static uint8_t fm1_led_mid[FM1_NCOL];              /* the steady mid level: lit 1 frame in FM1_LED_MID_N */
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_input_scan(void) {}
static void fm1_led_dim_level(uint32_t lo) { (void)lo; }
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }
static int fm1_led_anim_on(void) { return 0; }
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s;
    if (e >= FM1_NENC)
        return 0;
    s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    return p;
}
static void fm1_led_key(uint32_t id, int on)       /* the LED of key id (button 0..13 or note key 14..40) */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

/* ------------------------------------------------------------------- ADC --- */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1core_sk_master = 1023;           /* the MASTER pot 0..1023 (the panel's) */
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return fm1core_sk_master;
    if (ch == FM1_ADC_BATT)
        return 600;                                /* a full battery (bringup.c: >= 578 is the 100 % icon) */
    return -1;
}
static void fm1_adc_init(void) {}

/* ------------------------------------------------- the display's SPI half --- */
/* lcd.c over this: lcd_cmd = fm1_lcd_send_cmd + wait + deselect, lcd_data = fm1_lcd_send_data (a DMA, finished by
 * lcd_sync's wait). The panel decoded: CASET 0x2A / RASET 0x2B (4 bytes each: start, end, big-endian), RAMWR 0x2C
 * (then pixels, two bytes each, into the window from its top left, row by row, as the ST7789 does); the rest
 * (SWRESET, SLPOUT, COLMOD, MADCTL 0, INVON, DISPON ..) changes nothing that is shown here */
static uint16_t fm1core_sk_lcd[240u * 240u];       /* the ST7789's RAM: big-endian RGB565 as sent */
static uint32_t fm1core_sk_lcd_writes;
static uint32_t fm1_lcd_timeouts;
static struct {
    uint8_t cmd, nparam, param[4], hi, have_hi;
    uint16_t x0, x1, y0, y1, x, y;
} fm1core_sk_st;
static void fm1_lcd_hw_init(void) {}
static void fm1_lcd_backlight(uint32_t on) { (void)on; }
static void fm1_lcd_baud(uint32_t b) { (void)b; }
static void fm1_lcd_wait(void) {}
static void fm1_lcd_deselect(void) {}
static void fm1_lcd_send_cmd(uint8_t c)
{
    fm1core_sk_st.cmd = c;
    fm1core_sk_st.nparam = 0;
    fm1core_sk_st.have_hi = 0;
    if (c == 0x2Cu) {                              /* RAMWR: from the window's top left */
        fm1core_sk_st.x = fm1core_sk_st.x0;
        fm1core_sk_st.y = fm1core_sk_st.y0;
    }
}
static void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i;
    if (fm1core_sk_st.cmd == 0x2Au || fm1core_sk_st.cmd == 0x2Bu) {
        for (i = 0; i < n && fm1core_sk_st.nparam < 4u; i++)
            fm1core_sk_st.param[fm1core_sk_st.nparam++] = b[i];
        if (fm1core_sk_st.nparam == 4u) {
            const uint16_t s = (uint16_t)(fm1core_sk_st.param[0] << 8 | fm1core_sk_st.param[1]);
            const uint16_t e = (uint16_t)(fm1core_sk_st.param[2] << 8 | fm1core_sk_st.param[3]);
            if (fm1core_sk_st.cmd == 0x2Au) {
                fm1core_sk_st.x0 = s;
                fm1core_sk_st.x1 = e;
            } else {
                fm1core_sk_st.y0 = s;
                fm1core_sk_st.y1 = e;
            }
        }
        return;
    }
    if (fm1core_sk_st.cmd != 0x2Cu)
        return;
    for (i = 0; i < n; i++) {
        if (!fm1core_sk_st.have_hi) {
            fm1core_sk_st.hi = b[i];
            fm1core_sk_st.have_hi = 1;
            continue;
        }
        fm1core_sk_st.have_hi = 0;
        if (fm1core_sk_st.y > fm1core_sk_st.y1)
            continue;                              /* (past the window's end: the panel ignores it) */
        if (fm1core_sk_st.x < 240u && fm1core_sk_st.y < 240u) {
            uint8_t *px = (uint8_t *)&fm1core_sk_lcd[fm1core_sk_st.y * 240u + fm1core_sk_st.x];
            px[0] = fm1core_sk_st.hi;              /* (the bytes as sent: big-endian in memory) */
            px[1] = b[i];
        }
        if (fm1core_sk_st.x >= fm1core_sk_st.x1) {
            fm1core_sk_st.x = fm1core_sk_st.x0;
            fm1core_sk_st.y++;
        } else {
            fm1core_sk_st.x++;
        }
    }
    fm1core_sk_lcd_writes++;
}

/* ----------------------------------------------------- audio (fm1_audio.h) --- */
#define FM1_AUDIO_HALF 0x80u
static uint32_t fm1core_sk_half;                   /* the half the "DMA" hands the ISR (sikken_glue.h) */
static uint8_t fm1core_sk_audio_on;                /* fm1_audio_init ran, and no fm1_audio_stop since */
static uint8_t fm1core_sk_pc6;                     /* the analog output gate (PC6) */
static int32_t *fm1core_sk_dma;                    /* the DMA buffer audio_init handed over */
static void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    (void)half_words;
    (void)isr;
    (void)prio;
    fm1core_sk_dma = buf;
    fm1core_sk_audio_on = 1;
}
static void fm1_audio_output_enable(uint8_t enabled) { fm1core_sk_pc6 = enabled != 0u; }
static uint8_t fm1_audio_pending(void) { return fm1core_sk_audio_on ? FM1_AUDIO_HALF : 0u; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return fm1core_sk_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_stop(void) { fm1core_sk_audio_on = 0; }

/* ------------------------------------------- the TRS MIDI jack (fm1_uart.h) --- */
static void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len)
{
    (void)ring;
    (void)len;
}
static uint32_t fm1_uart1_rx_take(void) { return 0; }   /* (nothing arrives: MIDI comes in over "USB") */

/* ------------------------------------------------------ USB (fm1_usb.h) --- */
/* the SIE never answers: usb.c's sie_wr / sie_rd return at once, usb_poll is never called (no TIMER5) */
static void fm1_usb_reset(void) {}
static void fm1_usb_attach(void *ep0) { (void)ep0; }
static void fm1_usb_off(void) {}
static uint32_t fm1_usb_sie_on(void) { return 0; }
static void fm1_usb_sie_wr_start(uint32_t r, uint32_t v) { (void)r; (void)v; }
static void fm1_usb_sie_rd_start(uint32_t r) { (void)r; }
static uint32_t fm1_usb_sie_done(void) { return 1; }
static uint32_t fm1_usb_sie_data(void) { return 0; }
static void fm1_usb_ep0_buf(void *p) { (void)p; }
static void fm1_usb_ep_txbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static void fm1_usb_ep0_send(void *p, uint32_t n) { (void)p; (void)n; }
static void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n) { (void)ep; (void)p; (void)n; }
static void fm1_usb_ep4_txbuf(void *p) { (void)p; }
static void fm1_usb_ep4_send(void *p, uint32_t n) { (void)p; (void)n; }
static void fm1_usb_rx_sync(void) {}
static void fm1_usb_ep_enable(uint32_t eps) { (void)eps; }
static uint32_t fm1_usb_sof_take(void) { return 0; }

/* -------------------------------------------------- the XIP window (fm1_xip.h) --- */
#define FM1_XIP(off) ((uintptr_t)(fm1nor_image + (uint32_t)(off)))
static const uint8_t *fm1_xip_ptr(uint32_t off) { return fm1nor_image + off; }
