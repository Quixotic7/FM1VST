/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL for the SLOOP-family cores (SLOOP 2.7.1, sloopDX 4.0, ORBIT 0.7.1, No Man Sky FM 0.4): what the
 * device's firmware/hal/fm1_*.h headers give firmware/src/felucca.c, on the host. Included by sloop_firmware.h in
 * the place of those headers (first, before libc.c). None of these firmwares ships an emulator unit: this header is
 * modelled on their tests/ui_pages_test.c stubs (time, input, display) and No Man Sky's wasm/nms_wasm.c (the whole
 * firmware on a host HAL), but live, as core-glue/felucca/felucca_hal.h is for Felucca 1.0.1:
 *   - time: fm1_ms is the device ms the ABI's tick gave (core.h's), fm1_ticks the 24 MHz TIMER4 (FM1_TICKS_PER_US
 *     24, common/ticks.h); fm1_delay_ms and the watchdog are no-ops (simulated time never moves inside a call);
 *   - one thread runs the whole device (the ABI's threading contract): the "interrupt-off" sections only nest;
 *   - input: fm1_in holds the debounced state sloop_fw.c's tick delivers (a press at once, a release after the
 *     device's ~9 ms, the buttons held over MIDI OR'ed in as fm1__key does), the edges fm1_input_edges /
 *     fm1_input_note_edges take, the encoder detents fm1_enc_take takes;
 *   - LEDs: fm1_led (lit), fm1_led_dim (the glow), fm1_led_bg (the backlight, lit for fm1_led_bg_ns per frame,
 *     menu LIGHTS) and, in No Man Sky, fm1_led_lv[41] (sixteenths of full per key id): core_sloop.c turns them
 *     into fm1panel_t levels;
 *   - the LCD: lcd_fill / lcd_blit write fm1core_sl_lcd (the ST7789's RAM, big-endian RGB565 as sent: lcd.c swaps
 *     the fills, blits go as given), counted; lcd.c (the SPI driver) is not built;
 *   - audio: audio.c's ISR is called per 256-frame half by sloop_fw.c through common/fifo (the ABI's 128-frame
 *     blocks); fm1_audio_free_half names the half it renders;
 *   - the second core: cpu1.h's job API runs a job inline at cpu1_wait (tests/cpu1_host.c's CORES=1: one thread,
 *     deterministic); cpu1.c (the device's boot of core 1) is not built;
 *   - the flash: core-glue/common/nor.c's 1 MiB image; the XIP window (fm1_xip_ptr: user sample slots, the FM6
 *     bank, storage objects read in place) is the image itself.
 * Every name here is the firmware's own (the device HAL's); glue-only names carry fm1core_sl_ / FM1CORE_SL_. */

/* ------------------------------------------------------------------ time --- */
#define FM1_TICKS_PER_US 24u                       /* TIMER4 at 24 MHz (fm1_time.h) */
static uint32_t fm1_ticks(void) { return fm1t_ticks24(); }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }   /* (busy waits: simulated time does not move in a call) */
static void fm1_wdt_feed(void) {}
static void fm1_wdt_arm(uint8_t t) { (void)t; }
static void fm1_time_init(void) {}

/* ------------------------------------------------- interrupts (one thread) --- */
static uint32_t fm1core_sl_irq_depth;
static void fm1_irq_off(void) { fm1core_sl_irq_depth++; }
static void fm1_irq_on(void)
{
    if (fm1core_sl_irq_depth)
        fm1core_sl_irq_depth--;
}
static uint32_t irq_save(void)
{
    fm1_irq_off();
    return 0;
}
static void irq_restore(uint32_t f)
{
    (void)f;
    fm1_irq_on();
}
static void fm1_irq_init(void) {}
static void fm1_irq_enable_all(void) {}

/* ---------------------------------------------------------- the system --- */
static struct {
    uint8_t p3_rst;                                /* bit0 power-on (fm1_sys.h): every core power-on is one */
    uint32_t rst_src;
    uint8_t wdt_con;
} fm1_boot = {1u, 0u, 0u};
static void fm1_reset_reason(void) {}
static void fm1_supplies_stock(void) {}
static void fm1_mailbox_clear(void) {}
/* a reset or the update mode ends the core's "process": exit() is common/exit.h's (the core halts; the host
 * reloads it to power on again). 2: a reboot (factory reset, a crash screen), 3: UBOOT / the recovery updater */
static void fm1_reboot(void) { exit(2); }
static void fm1_enter_uboot(void) { exit(3); }
static uint32_t fm1_sp(void) { return 0; }
static uint32_t fm1_ssp(void) { return 0; }
static uint32_t fm1_usp(void) { return 0; }
#define FM1_GUARD_STACK 1u
#define FM1_GUARD_WRITE 2u
#define FM1_GUARD_BUS 4u
#define FM1_GUARD_PC 8u
static void fm1_guard_enable(uint32_t m) { (void)m; }
static void fm1_guard_lock_top(void) {}
typedef struct { uint32_t vec, pc, emu, dbg, rets, sp; } fm1_crash_t;   /* (fm1_guard.h: main.c fm1_fault) */
/* the linker's symbols main.c names (fm1_cstart, the stack paint; never run here), ABOUT's _etext */
static uint32_t fm1core_sl_linker[2];
#define _data_start fm1core_sl_linker
#define _data_end fm1core_sl_linker
#define _data_load fm1core_sl_linker
#define _bss_start fm1core_sl_linker
#define _bss_end fm1core_sl_linker
#define _pool_start fm1core_sl_linker
#define _pool_end fm1core_sl_linker
#define _rt_start fm1core_sl_linker
#define _rt_end fm1core_sl_linker
#define _rt_load fm1core_sl_linker
#define _c1_start fm1core_sl_linker
#define _c1_end fm1core_sl_linker
#define _c1_load fm1core_sl_linker
#define _lr_start fm1core_sl_linker
#define _lr_end fm1core_sl_linker
static char fm1core_sl_stack[16];
#define _ustack_lo fm1core_sl_stack
#define _ustack_top fm1core_sl_stack
#define _sstack_lo fm1core_sl_stack
#define _sstack_top fm1core_sl_stack
#define _etext fm1core_sl_stack

/* ------------------------------------------------------------ the timers --- */
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), int prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}

/* --------------------------------------------------- keys, knobs, LEDs --- */
#define FM1_NCOL 11u
#define FM1_NKEY 41u                               /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
/* key id at (physical column, packed row bit), -1 = none (fm1_input.h) */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
static volatile struct {
    uint32_t notes;                                /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;                              /* debounced: bit i = matrix button i (0..13) */
    uint32_t pressed, released;                    /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;                        /* note-key press edges since the last fm1_input_note_edges() */
    int16_t enc_steps[FM1_NENC];                   /* + = clockwise */
    uint32_t frames;
} fm1_in;
static volatile uint32_t fm1_remote_btn;           /* (2.7) the buttons held over MIDI: closed for the debounce */
static uint8_t fm1_led[FM1_NCOL];                  /* lit: packed row bits per column */
static uint8_t fm1_led_dim[FM1_NCOL];              /* the glow (~1/24 of lit) */
static uint8_t fm1_led_bg[FM1_NCOL];               /* the backlight (menu LIGHTS) */
static volatile uint16_t fm1_led_bg_ns;            /* the backlight's pulse a frame, 0 = off */
static uint8_t fm1_led_lv[FM1_NKEY];               /* (No Man Sky) soft brightness, sixteenths of full per key id */
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
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
static int32_t fm1core_sl_master = 724;            /* the MASTER pot 0..1023 (the panel's) */
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return fm1core_sl_master;
    if (ch == FM1_ADC_BATT)
        return 800;                                /* a healthy battery (as the web emulator) */
    return -1;
}
static void fm1_adc_init(void) {}

/* --------------------------------------------------------------- display --- */
/* the ST7789's RAM as sent: big-endian RGB565 (lcd.c swaps the fill colour; gfx.c's canvases are pre-swapped) */
static uint16_t fm1core_sl_lcd[240u * 240u];
static uint32_t fm1core_sl_lcd_writes;
static uint32_t fm1_lcd_timeouts;
static void lcd_sync(void) {}
static void lcd_init(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    const uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            fm1core_sl_lcd[(y + j) * 240u + x + i] = s;
    fm1core_sl_lcd_writes++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    if (!w || !h)
        return;
    for (j = 0; j < h; j++)                        /* (the panel's window: rows past the edge are dropped) */
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fm1core_sl_lcd[(y + j) * 240u + x + i] = p[j * w + i];
    fm1core_sl_lcd_writes++;
}

/* ----------------------------------------------------------- audio (audio.c) --- */
#define FM1_AUDIO_HALF 0x80u
static uint32_t fm1core_sl_half;                   /* the half the "DMA" hands the ISR */
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return fm1core_sl_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t prio)
{
    (void)b;
    (void)n;
    (void)isr;
    (void)prio;
}
static uint8_t fm1core_sl_audio_stopped;
static void fm1_audio_stop(void) { fm1core_sl_audio_stopped = 1; }
void isr_alnk0(void) {}

/* ------------------------------------------- the second core (fm1_cpu1.h) --- */
/* hal/fm1_cpu1.h's job hand-over for cpu1.c (built: its boot, self-test and state are the firmware's), on one
 * thread: a job handed over runs at fm1_cpu1_wait, as late as the device's (tests/cpu1_host.c's CORES=1: what the
 * caller does between run and wait must not depend on it, as on the device). No thread, no getenv. */
#define FM1_C1_STACK_WORDS 1u
#define FM1_C1_MARK 0x43314D4Bu
static uint32_t _c1_ustack[FM1_C1_STACK_WORDS];
static uint8_t fm1_c1_on;
static struct {
    int pending;
    void (*fn)(uint32_t);
    uint32_t arg, jobs;
} fm1core_sl_c1;
static int fm1_cpu1_start(void)
{
    fm1_c1_on = 1;
    return 0;
}
static void fm1_cpu1_hold(void) { fm1_c1_on = 0; }
static int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1_c1_on || fm1core_sl_c1.pending)
        return 0;                                  /* (one job at a time: the device's protocol) */
    fm1core_sl_c1.pending = 1;
    fm1core_sl_c1.fn = fn;
    fm1core_sl_c1.arg = arg;
    fm1core_sl_c1.jobs++;
    return 1;
}
static int fm1_cpu1_wait(void)
{
    if (fm1core_sl_c1.pending) {
        fm1core_sl_c1.pending = 0;
        fm1core_sl_c1.fn(fm1core_sl_c1.arg);       /* inline, deferred to here */
    }
    return 0;
}
static void fm1_guard_pc_open(void) {}
static uint32_t fm1_sysvdd(void) { return 15u; }   /* the stock supplies (fm1_supplies_stock): SYSVDD 1.38 V */
static uint32_t fm1_vdc14(void) { return 7u; }     /* VDC14 1.60 V */

/* ------------------------------------------------------------------ flash --- */
/* the FM-1's 1 MiB SPI NOR: common/nor.c's image (a file, the staged bytes or RAM only); storage.c, upreset.c,
 * project.c, fm6_bank.c and eng_sample.c's user slots on it. The XIP window is the image: what the device reads in
 * place at FM1_XIP(off) (user sample slots, the FM6 bank, storage objects) is read here at fm1nor_image + off. */
static uint8_t flash_ok;                           /* storage: the part answered (persist_boot sets it) */
#define FL_FAR(fn) (fn)
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part */
static void fl_plain_window_init(void) {}          /* (the image is plaintext) */
#define fm1_xip_ptr fm1core_sl_xip_device         /* (hal/fm1_xip.h, which eng_sample.c includes: the device's */
#include "../hal/fm1_xip.h"                        /*  window, renamed and unused; #pragma once keeps it so) */
#undef fm1_xip_ptr
#undef FM1_XIP
static const uint8_t *fm1_xip_ptr(uint32_t off) { return (const uint8_t *)fm1nor_image + (off & (FM1NOR_SIZE - 1u)); }
#define FM1_XIP(off) ((uintptr_t)fm1_xip_ptr(off))
#define ST_XIP_PTR(off) fm1_xip_ptr(off)
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return fm1nor_erase(off); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { return fm1nor_prog_span(off, src, n); }
