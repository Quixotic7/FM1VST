/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL for the Speak37 core: what the device's firmware/hal/fm1_*.h headers give firmware/src/speak.c
 * (BOARD_SMK37 0: the FM-1 build, build.py BOARD=fm1speak), on the host. Included by speak37_firmware.h in the place
 * of those headers, before libc.c. Speak37 ships no FM-1 emulator: this header is modelled on its own FM-1 host test
 * (tests/sp_screen.c -DT_FM1=1: the stubbed keyboard, a framebuffer) and the device headers, but live, as
 * core-glue/sloop/sloop_hal.h is for the SLOOP family:
 *   - time: fm1_ms is the device ms the ABI's tick gave, fm1_ticks the 24 MHz TIMER4 (common/ticks.h); the busy
 *     waits (fm1_delay_us / _ms) and the watchdog are no-ops (simulated time never moves inside a call);
 *   - one thread runs the whole device (the ABI's threading contract): the interrupt-off sections only nest;
 *   - input: fm1_in holds the debounced state core_speak37.c's tick delivers (a press at once, a release after the
 *     device's ~9 ms), the button edges fm1_input_edges takes, the encoder detents fm1_enc_take takes;
 *   - LEDs: fm1_led (lit, packed row bits per column) through fm1_led_key, the only LED call Speak37's FM-1 panel
 *     makes (sp_panel_fm1.c); the glue turns it into the panel's levels;
 *   - the LCD: lcd_fill / lcd_blit write fm1core_sp_lcd (the ST7789's RAM, big-endian RGB565 as sent: lcd.c swaps
 *     the fills, gfx.c's canvases go as given), counted; lcd.c (the SPI driver) is not built;
 *   - audio: sp_main.c's ALNK0 ISR (fm1_alnk0_irq, the device's own) renders one 128-frame half per ABI block;
 *     fm1_audio_free_half names the half it renders;
 *   - the flash: core-glue/common/nor.c's 1 MiB image under the device's storage_hw.c (fl_read_ram, fl_erase4k_ram,
 *     fl_write: hal/fm1_flash.h's RAM driver), the flash map's guards (hal/fm1_flash_map.h, macros only) kept.
 * Every name here is the firmware's own (the device HAL's); glue-only names carry fm1core_sp_ / FM1CORE_SP_. */

/* ------------------------------------------------------- fm1_time.h --- */
#define FM1_TICKS_PER_US 24u                       /* TIMER4 at 24 MHz */
static uint32_t fm1_ticks(void) { return fm1t_ticks24(); }
static uint32_t fm1_micros(void) { return fm1t_ticks24() / FM1_TICKS_PER_US; }
static void fm1_time_init(void) {}
static void fm1_delay_us(uint32_t us) { (void)us; }  /* (busy waits: simulated time does not move in a call) */
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
/* the CPU clock the boot screen shows ("AC791N .. MHZ"), measured on the device against TIMER4 with a NOP loop:
 * there is no CPU to measure in a core, so 0, as Speak37's own FM-1 host test (tests/sp_screen.c) draws it */
static uint32_t fm1_cpu_mhz(void) { return 0; }

/* -------------------------------------------------------- fm1_sys.h --- */
static struct {
    uint8_t p3_rst;                                /* bit0 power-on: every core power-on is one */
    uint32_t rst_src;
    uint8_t wdt_con;
} fm1_boot = {1u, 0u, 0u};
static void fm1_wdt_arm(uint8_t t) { (void)t; }
static void fm1_wdt_feed(void) {}
static void fm1_wdt_stop(void) {}
static void fm1_reset_reason(void) {}
static void fm1_mailbox_clear(void) {}
/* a reset or the update mode ends the core's "process": exit() is common/exit.h's (the core halts; the host reloads
 * it to power on again). 2: a reboot (the crash screen's), 3: UBOOT (OCT- + OCT+ held 5 s, the boot guard) */
static void fm1_reboot(void) { exit(2); }
static void fm1_enter_uboot(void) { exit(3); }

/* ---------------------------------------------- fm1_irq.h (one thread) --- */
static uint32_t fm1core_sp_irq_depth;
static void fm1_irq_off(void) { fm1core_sp_irq_depth++; }
static void fm1_irq_on(void)
{
    if (fm1core_sp_irq_depth)
        fm1core_sp_irq_depth--;
}
static void fm1_irq_init(void) {}
static void fm1_irq_enable_all(void) {}
#define FM1_CRASH_MAGIC 0x43525348u
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;
} fm1_crash_t;                                     /* (sp_main.c fm1_fault: compiled, never reached) */

/* ------------------------------------------------------ fm1_guard.h --- */
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
static void fm1_guard_enable(uint32_t which) { (void)which; }
static void fm1_guard_lock_top(void) {}
static void fm1_guard_unlock_top(void) {}
/* the linker's symbols speak.c declares and fm1_cstart reads (never run here) */
static uint32_t fm1core_sp_linker[2];
#define _data_start fm1core_sp_linker
#define _data_end fm1core_sp_linker
#define _data_load fm1core_sp_linker
#define _bss_start fm1core_sp_linker
#define _bss_end fm1core_sp_linker
#define _pool_start fm1core_sp_linker
#define _pool_end fm1core_sp_linker
#define _rt_start fm1core_sp_linker
#define _rt_end fm1core_sp_linker
#define _rt_load fm1core_sp_linker

/* ------------------------------------------------------ fm1_input.h --- */
#define FM1_NCOL 11u
#define FM1_NKEY 41u                               /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
#define FM1_NBTN 14u
/* key id at (physical column, packed row bit), -1 = none */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
#define FM1_OCT_BOTH (1u << FM1_BTN_OCT_DOWN | 1u << FM1_BTN_OCT_UP)
static volatile struct {
    uint32_t notes;                                /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;                              /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;                    /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;                        /* note-key press edges since the last fm1_input_note_edges() */
    int16_t enc_steps[FM1_NENC];                   /* + = clockwise */
    uint32_t frames;                               /* scan frames (one a device ms here) */
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];                  /* lit: packed row bits per column */
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static uint32_t fm1__lock(void)
{
    fm1_irq_off();
    return 0;
}
static void fm1__unlock(uint32_t v)
{
    (void)v;
    fm1_irq_on();
}
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

/* ------------------------------------------------------ fm1_timer.h --- */
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), int prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}

/* ------------------------------------------------------ fm1_audio.h --- */
#define FM1_AUDIO_HALF 0x80u
static uint32_t fm1core_sp_half;                   /* the half the "DMA" hands the ISR */
static uint8_t fm1core_sp_audio_stopped;           /* fm1_audio_stop: the crash / UBOOT screens */
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return fm1core_sp_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t prio)
{
    (void)b;
    (void)n;
    (void)isr;
    (void)prio;
}
static void fm1_audio_stop(void) { fm1core_sp_audio_stopped = 1; }
void isr_alnk0(void) {}

/* -------------------------------------------------------- fm1_adc.h --- */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1core_sp_master = 724;            /* the MASTER pot 0..1023 (the panel's; 724: sp_main.c's level
                                                    * before the first reading, sp_master_q12 2047) */
static void fm1_adc_init(void) {}
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return fm1core_sp_master;
    if (ch == FM1_ADC_BATT)
        return 800;                                /* a healthy battery */
    return -1;
}

/* ------------------------------------------------------ fm1_lcd_hw.h / lcd.c --- */
/* the ST7789's RAM as sent: big-endian RGB565 (lcd.c swaps the fill colour; gfx.c's canvases are pre-swapped) */
static uint16_t fm1core_sp_lcd[240u * 240u];
static uint32_t fm1core_sp_lcd_writes;
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
            fm1core_sp_lcd[(y + j) * 240u + x + i] = s;
    fm1core_sp_lcd_writes++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    if (!w || !h)
        return;
    for (j = 0; j < h; j++)                        /* (the panel's window: what lies past the edge is dropped) */
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fm1core_sp_lcd[(y + j) * 240u + x + i] = p[j * w + i];
    fm1core_sp_lcd_writes++;
}

/* -------------------------------------------- fm1_xip.h, fm1_flash.h --- */
/* the FM-1's 1 MiB SPI NOR: common/nor.c's image (a file, the staged bytes or RAM only), under the device's own
 * storage_hw.c and storage.c. The XIP window is the image itself. */
#include "../hal/fm1_flash_map.h"                  /* FL_JEDEC_OK, FL_STORE_OK (macros only) */
static const uint8_t *fm1_xip_ptr(uint32_t off) { return (const uint8_t *)fm1nor_image + (off & (FM1NOR_SIZE - 1u)); }
#define FM1_XIP(off) ((uintptr_t)fm1_xip_ptr(off))
#define FL_FAR(fn) (fn)
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
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's part (FL_JEDEC_OK) */
static void fl_plain_window_init(void) {}                  /* (the image is plaintext) */
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
/* a 4 KiB erase: the image's sector (nor.c queues the erase's silent audio blocks, as storage_hw.c's IRQ-off
 * window silences the device) */
static int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    if (took_us)
        *took_us = 0;
    return fm1nor_erase(off);
}
static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n) { return fm1nor_prog_span(off, src_ram, n); }
