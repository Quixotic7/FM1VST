/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL for the Tonitor core: what the device's firmware/hal/fm1_*.h headers give firmware/src/tonitor.c, on
 * the host. Included by tonitor_firmware.h in the place of tonitor.c's HAL headers (fm1_time.h .. fm1_lcd_hw.h), and
 * main.c's own `#include "fm1_flash.h"` finds hal/fm1_flash.h here (this folder's hal/ is on the unit's include
 * path; the device's is not). Tonitor ships no emulator: its tests/host/host.c (the render / screen / flow harness)
 * stubs time, the panel, the screen and CPU1's mailbox for the parts it compiles; this header does the same for the
 * whole firmware, live:
 *   - time: fm1_ms is the device ms the ABI's tick gave (tonitor_firmware.h declares it, as tonitor.c does),
 *     fm1_ticks TIMER4 at 24 MHz (common/ticks.h): simulated time, which never moves inside a call;
 *   - WAITS ARE YIELDS: the main loop runs on a coroutine (tonitor_fw.c), and the places where the device waits for
 *     its interrupts to move on (fm1_wdt_feed in sound_slot_free, ses_events_drain, ota_wire_send's full ring and the
 *     engines' measuring loops; fm1_delay_ms) give the clock back to the host there: the audio ISR, the timer and
 *     USB run, and the loop goes on at the next device ms, as the device's main loop does under its interrupts.
 *     Outside the coroutine (the power-on in init) they return at once;
 *   - one thread runs the whole device (the ABI's threading contract): irq_save / irq_restore only nest;
 *   - input: fm1_in holds the debounced state tonitor_fw.c's tick delivers (a press at once, a release after the
 *     device's ~9 ms), the button edges fm1_input_edges takes, the encoder detents fm1_enc_take takes;
 *   - LEDs: fm1_led (lit) and fm1_led_dim (the glow): core_tonitor.c turns them into fm1panel_t levels;
 *   - the LCD: lcd.c (the SPI driver) is not built; lcd_fill / lcd_blit write fm1core_tn_lcd (the ST7789's RAM,
 *     big-endian RGB565 as sent: lcd.c swaps the fill colour, gfx.c's canvases go as they are), counted;
 *   - audio: main.c's fm1_alnk0_irq is called per 256-frame half by tonitor_fw.c through common/fifo (the ABI's
 *     128-frame blocks); fm1_audio_free_half names the half it renders;
 *   - the second core (hal/fm1_multicore.h, src/audio_worker.h): CPU1's mailbox as host.c keeps it, a job handed over
 *     runs inline at audio_worker_join, where the device's CPU0 waits for it (the same sound: host.c's `cores 2` and
 *     `cores 1` hash alike); the worker is online, so the firmware renders as it does with CPU1 up;
 *   - the flash: hal/fm1_flash.h, core-glue/common/nor.c's 1 MiB image.
 * Every name here is the firmware's own (the device HAL's); glue-only names carry fm1core_tn_ / FM1CORE_TN_. */

/* ------------------------------------------------------------------ time --- */
#define FM1_TICKS_PER_US 24u                       /* TIMER4 at 24 MHz (fm1_time.h) */
#define FM1_T4_CNT (fm1t_ticks24())                /* (fm1_multicore.h's AW_TICKS reads the register) */
static uint32_t fm1_ticks(void) { return fm1t_ticks24(); }
static uint32_t fm1_micros(void) { return fm1t_us(); }
static void fm1_time_init(void) {}
static void fm1_delay_us(uint32_t us) { (void)us; }
static void fm1_delay_ms(uint32_t ms)              /* on the main loop's coroutine: until that many device ms passed */
{
    const uint32_t t0 = fm1_ms;
    if (!fm1coro_inside())
        return;                                    /* (the power-on: simulated time does not move in a call) */
    while ((uint32_t)(fm1_ms - t0) < ms)
        fm1coro_yield();
}

/* ------------------------------------------------- the system (fm1_sys.h) --- */
static uint32_t fm1_p33_timeouts;
static void fm1_wdt_arm(uint8_t t) { (void)t; }
static void fm1_wdt_feed(void)                     /* where the firmware waits for its interrupts: a yield */
{
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
#define fm1_core1_stop() ((void)0)
/* a reset or the update mode ends the core's "process": exit() is common/exit.h's (the core halts; the host reloads
 * it to power on again). 2: a reboot (a crash screen), 3: UBOOT, the update (an installer's) */
static void fm1_reboot(void) { exit(2); }
static void fm1_enter_uboot(void) { exit(3); }
static void fm1_enter_update(const uint8_t *parm)
{
    (void)parm;
    exit(3);
}
static void fm1_mailbox_clear(void) {}
static void fm1_core_reset(void) { exit(2); }
static void fm1_updata_parm_clear(void) {}
static int fm1_mem_readable(uint32_t a, uint32_t n)
{
    (void)a;
    (void)n;
    return 0;
}

/* --------------------------------------------------------- power, guard --- */
static uint8_t fm1_power_ready;
static int fm1_power_init(void) { return 1; }      /* (stock V15's supplies: held) */
#define FM1_GUARD_STACK 1u
#define FM1_GUARD_WRITE 2u
#define FM1_GUARD_BUS 4u
#define FM1_GUARD_PC 8u
typedef struct { uint32_t vec, pc, emu, dbg, rets, sp; } fm1_crash_t;   /* (main.c fm1_fault: never called) */
static void fm1_guard_enable(uint32_t which) { (void)which; }
static void fm1_guard_lock_top(void) {}
static void fm1_guard_unlock_top(void) {}

/* ------------------------------------------------- interrupts (one thread) --- */
static uint32_t fm1core_tn_irq_depth;
static void fm1_irq_off(void) { fm1core_tn_irq_depth++; }
static void fm1_irq_on(void)
{
    if (fm1core_tn_irq_depth)
        fm1core_tn_irq_depth--;
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

/* ---------------------------------------- the second core (fm1_multicore.h) --- */
/* hal/fm1_core1_bar.h: CPU1 barred after a fault, until power-off (a core's RAM is fresh each power-on) */
static int fm1_core1_barred(void) { return 0; }
static void fm1_core1_bar_set(void) {}
static int fm1_core1_boot_allowed(uint32_t power_ready, uint32_t boot_failed)
{
    return power_ready && !boot_failed && !fm1_core1_barred();
}
/* src/audio_worker.h's mailbox, on one thread (host.c's): submit hands a job over, join runs it, where CPU0 waits
 * for CPU1's half on the device. The firmware splits the block's voices between the two by their last cost and
 * renders its own share between the two calls; the job's share is the same work, so the sound is the same */
typedef void (*audio_worker_fn)(void *);
static int audio_worker_online;
static struct {
    audio_worker_fn fn;
    void *ctx;
    uint32_t jobs;
} fm1core_tn_aw;
static int audio_worker_submit(audio_worker_fn fn, void *ctx)
{
    if (!audio_worker_online || fm1core_tn_aw.fn)
        return 0;
    fm1core_tn_aw.fn = fn;
    fm1core_tn_aw.ctx = ctx;
    return 1;
}
static int audio_worker_join(void)
{
    if (fm1core_tn_aw.fn) {
        audio_worker_fn fn = fm1core_tn_aw.fn;
        fm1core_tn_aw.fn = 0;
        fn(fm1core_tn_aw.ctx);
        fm1core_tn_aw.jobs++;
    }
    return 1;
}
static void audio_worker_check(void) {}
static void audio_worker_retire(void) { audio_worker_online = 0; }
static int fm1_multicore_start(void)               /* CPU1 up (its jobs run inline) */
{
    audio_worker_online = 1;
    return 1;
}

/* ----------------------------------------------------------- the timers --- */
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio)
{
    (void)isr;
    (void)prio;
}
void isr_timer5(void) {}

/* --------------------------------------------------- keys, knobs, LEDs --- */
#define FM1_NCOL 11u
#define FM1_NKEY 41u                               /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
#define FM1_DEB_RELEASE 8u
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
static uint8_t fm1_led[FM1_NCOL];                  /* lit: packed row bits per column */
static uint8_t fm1_led_dim[FM1_NCOL];              /* the glow */
static uint8_t fm1_led_bg[FM1_NCOL];               /* the backlight (unused by Tonitor) */
static volatile uint16_t fm1_led_bg_ns;
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_input_scan(void) {}                /* (recovery.c's power-on check: never run) */
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
static int32_t fm1core_tn_master = 724;            /* the MASTER pot 0..1023 (the panel's) */
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return fm1core_tn_master;
    if (ch == FM1_ADC_BATT)
        return 800;                                /* a healthy battery (the UI shows USB power: usb.config) */
    return -1;
}
static void fm1_adc_init(void) {}

/* --------------------------------------------------------------- display --- */
/* the ST7789's RAM as sent: big-endian RGB565 (lcd.c swaps the fill colour; gfx.c's canvases are pre-swapped). lcd.c
 * is the SPI driver: its four entry points, on this RAM */
static uint16_t fm1core_tn_lcd[240u * 240u];
static uint32_t fm1core_tn_lcd_writes;
static uint32_t fm1_lcd_timeouts;
static void lcd_sync(void) {}
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
            fm1core_tn_lcd[(y + j) * 240u + x + i] = s;
    fm1core_tn_lcd_writes++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    if (!w || !h)
        return;
    for (j = 0; j < h; j++)                        /* (the panel's window: what falls past the edge is dropped) */
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fm1core_tn_lcd[(y + j) * 240u + x + i] = p[j * w + i];
    fm1core_tn_lcd_writes++;
}
static void lcd_init(void) { lcd_fill(0, 0, 240, 240, 0); }   /* (lcd.c: the panel's init, then black) */

/* ----------------------------------------------------- audio (fm1_audio.h) --- */
#define FM1_AUDIO_HALF 0x80u
static uint32_t fm1core_tn_half;                   /* the half the "DMA" hands the ISR */
static uint8_t fm1core_tn_audio_on;                /* audio_init ran, and no fm1_audio_stop since */
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return fm1core_tn_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t prio)
{
    (void)b;
    (void)n;
    (void)isr;
    (void)prio;
    fm1core_tn_audio_on = 1;
}
static void fm1_audio_stop(void) { fm1core_tn_audio_on = 0; }
void isr_alnk0(void) {}

/* ------------------------------------------- the TRS MIDI jack (midi_uart.c) --- */
/* midi_uart.c is the UART1 driver (its registers, DMA): not built. Its three entry points are main.c's (the boot and
 * TIMER5), neither run here; MIDI comes in over "USB" (the core's midi_in) */
static void uart_midi_init(void) {}
static void uart_midi_poll(void) {}
