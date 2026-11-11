/* SPDX-License-Identifier: GPL-3.0-only */
/* Matryoshka's hardware layer on the host (core_matryoshka.c). Matryoshka keeps its own copy of Felucca's header-only
 * HAL (firmware/hal/fm1_*.h, Felucca at a4d7ca2e: SFRs at fixed addresses, pi32v2 asm), included by name from
 * firmware/src/modular.c and by relative path from usb.c and midi_uart.c ("../hal/fm1_usb.h", "../hal/fm1_uart.h").
 * core_matryoshka.cmake has clang's -ivfsoverlay read THIS file for every one of them, so the submodule compiles
 * unmodified; the guard below makes the second and later ones empty (no #pragma once: the overlay gives one file
 * several names).
 *
 * The same names and shapes as the real headers, with the state the glue drives:
 *   time       TIMER4 (24 MHz) is fm1core_mt_t4, advanced by the glue (2400 ticks per TIMER5 interrupt, ten per ms);
 *              fm1_delay_ms waits in device time (the main loop's coroutine yields), fm1_delay_us returns
 *   watchdog   fm1_wdt_feed is the end of a main-loop pass: the coroutine yields (every loop and wait of the firmware
 *              feeds it: the main loop, the boot's second for a flasher, the splash, sx_room's wait, update mode)
 *   input      fm1_in as fm1_input.h keeps it (debounced held masks, edges, the note-key queue, encoder steps), written
 *              by the glue from the panel; the scan (fm1_input_tick) is the glue's; the LED planes as the device's
 *   audio      ALNK0: a half buffer always pending; fm1_audio_free_half the half the glue's audio() is about to play
 *   ADC        MASTER (ch 4) the panel's pot 0..1023, the battery (ch 3) full
 *   flash      fl_*_ram on common/nor.c's 1 MiB image, with the real driver's range rule (FL_RANGE_OK) and returns
 *   LCD        an ST7789 by its commands: CASET / RASET / RAMWR into a 240 x 240 panel RAM, big-endian as sent
 *   USB        the SIE answers (fm1_usb_sie_done), nothing more: the build is M1_EMU_MIDI=1 (usb.c's mailbox instead
 *              of EP1, as the firmware's emulator build), which the glue fills and drains
 *   UART1      the TRS jack: no bytes (the ABI's MIDI is USB)
 *   reset      fm1_reboot / fm1_enter_uboot / fm1_enter_update halt the core (exit); none is reached in normal use
 * IRQs, the guard unit, the watchdog's arming, the P33 bus and the mailbox are no-ops. */
#ifndef FM1CORE_MT_HAL_H
#define FM1CORE_MT_HAL_H
#include <stdint.h>
#include <string.h>
#include "nor.h"

#define FM1_INLINE static inline

/* the glue's (core_matryoshka.c, after the firmware) */
static void fm1core_mt_yield(void);                 /* fm1_wdt_feed: the pass ends (inside the coroutine) */
static void fm1core_mt_sleep_ms(uint32_t ms);       /* fm1_delay_ms */
static void fm1core_mt_halt(int code, const char *why);

/* ---------------------------------------------------------------- fm1_time.h --- */
#define FM1_TICKS_PER_US 24u
static uint32_t fm1core_mt_t4;                      /* TIMER4's count */
static inline void fm1_time_init(void) {}
static inline uint32_t fm1_ticks(void) { return fm1core_mt_t4; }
static inline uint32_t fm1_micros(void) { return fm1core_mt_t4 / FM1_TICKS_PER_US; }
static inline void fm1_delay_us(uint32_t us) { (void)us; }
static inline void fm1_delay_ms(uint32_t ms) { fm1core_mt_sleep_ms(ms); }

/* ----------------------------------------------------------------- fm1_sys.h --- */
#define FM1_UPDATA_PARM_LEN 112u
static struct {
    uint8_t p3_rst;      /* bit0 power-on */
    uint32_t rst_src;
    uint8_t wdt_con;
} fm1_boot;
static void fm1_reset_reason(void) { fm1_boot.p3_rst = 1u; }   /* (a power-on, every time) */
static void fm1_wdt_arm(uint8_t t) { (void)t; }
static void fm1_wdt_feed(void) { fm1core_mt_yield(); }
static void fm1_wdt_stop(void) {}
static void fm1_reboot(void) { fm1core_mt_halt(0, "reboot"); }
static void fm1_enter_uboot(void) { fm1core_mt_halt(3, "UBOOT (the chip's USB boot)"); }
static void fm1_core_reset(void) { fm1core_mt_halt(0, "core reset"); }
static void fm1_enter_update(const uint8_t *parm) { (void)parm; fm1core_mt_halt(3, "the update loader"); }
static void fm1_updata_parm_clear(void) {}
static void fm1_mailbox_clear(void) {}
static int fm1_mem_readable(uint32_t a, uint32_t n) { (void)a; (void)n; return 0; }

/* ----------------------------------------------------------------- fm1_irq.h --- */
#define FM1_IRQ_TARGET 1
static inline uint32_t fm1_icfg(void) { return 0; }
static inline void fm1_icfg_set(uint32_t v) { (void)v; }
static inline void fm1_irq_off(void) {}
static inline void fm1_irq_on(void) {}
#define FM1_CRASH_MAGIC 0x43525348u          /* "CRSH" */
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;
} fm1_crash_t;
static fm1_crash_t fm1_crash;                /* (no crash recorded: the magic stays 0) */
static void fm1_fault(const fm1_crash_t *c);
static void fm1_irq_init(void) {}
static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio) { (void)n; (void)h; (void)prio; }
static void fm1_irq_mask(uint32_t n) { (void)n; }
static void fm1_irq_enable_all(void) {}

/* --------------------------------------------- fm1_cpu1.h, fm1_core1_stop.h --- */
/* 1.11.0's second core (#271): it never starts here, so every job stays on the one core (fm1_cpu1_run says 0: the
 * caller does the work, as on a unit where core 1 is barred). The state the DIAG pages read, at rest */
typedef struct {
    volatile uint32_t job, arg;
    void (*volatile fn)(uint32_t);
    volatile uint32_t check;
    uint32_t pad0[12];
    volatile uint32_t alive, done;
    volatile uint32_t bad, bad_job, bad_done, bad_fn;
    uint32_t pad1[10];
} fm1_c1_mb_t;
static fm1_c1_mb_t fm1_c1_mb;
static volatile uint32_t fm1_c1_trace;
static uint8_t fm1_c1_on;
static uint8_t fm1_c1_gave_up;
#define FM1_C1_BAR_MAGIC 0x43314241u
static struct { uint32_t magic, barred; } fm1_c1_bar;
static volatile uint32_t fm1_c1_race_ticks;
#define FM1_CRASH_EMU_C1 0x000000C1u
static void fm1_cpu1_bar(void) { fm1_c1_bar.magic = FM1_C1_BAR_MAGIC; fm1_c1_bar.barred = 1; }
static inline void fm1_cpu1_halt(void) {}
static void fm1_cpu1_hold(void) { fm1_c1_on = 0; }
static int fm1_cpu1_start(void) { return -1; }
static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
static inline int fm1_cpu1_run_mischecked(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
static inline int fm1_cpu1_wait(void) { return 0; }
static uint32_t fm1_cpu1_stack_used(void) { return 0; }
/* app.ld's core-1 stacks (main.c's DIAG page: the pool left free up to them): a stand-in at the pool's end, so the
 * page reads 0 free beyond the pool */
#define _c1_ustack ((uint32_t *)(void *)_pool_end)
static inline int fm1_cpu1_busy(void) { return 0; }

/* --------------------------------------------------------------- fm1_guard.h --- */
#define FM1_GUARD_STACK 1u
#define FM1_GUARD_WRITE 2u
#define FM1_GUARD_BUS 4u
#define FM1_GUARD_PC 8u
extern char _guard0[], _ustack_lo[], _guard1[], _sstack_lo[], _sstack_top[], _etext[];
extern uint32_t _rt_start[], _rt_end[];
static void fm1_guard_enable(uint32_t which) { (void)which; }
static void fm1_guard_lock_top(void) {}
static void fm1_guard_unlock_top(void) {}
static int fm1_guard_top_locked(void) { return 0; }   /* (1.11.0: c1_boot) */
static void fm1_guard_pc_open(void) {}

/* --------------------------------------------------------------- fm1_timer.h --- */
static uint8_t fm1core_mt_t5_on;
static inline void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; fm1core_mt_t5_on = 1; }
static inline void fm1_timer5_ack(void) {}

/* --------------------------------------------------------------- fm1_audio.h --- */
#define FM1_AUDIO_HALF 0x80u
static int32_t *fm1core_mt_abuf;
static uint32_t fm1core_mt_half_words;
static uint8_t fm1core_mt_audio_on;
static uint32_t fm1core_mt_half;                    /* the half the next render fills */
static inline void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    (void)isr;
    (void)prio;
    fm1core_mt_abuf = buf;
    fm1core_mt_half_words = half_words;
    fm1core_mt_audio_on = 1;
}
static inline uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static inline void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static inline uint32_t fm1_audio_free_half(void) { return fm1core_mt_half; }
static inline void fm1_audio_ack_half(void) {}
static inline void fm1_audio_stop(void) { fm1core_mt_audio_on = 0; }

/* ----------------------------------------------------------------- fm1_adc.h --- */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1core_mt_master = 512;             /* the pot, 0..1023 (the panel's) */
static void fm1_adc_init(void) {}
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? fm1core_mt_master : 1023; }

/* --------------------------------------------------------------- fm1_flash.h --- */
#define RAMFN
#define RAMINL static inline
#define FL_FAR(fn) (fn)
#define FL_DATA_LO      0x00097000u
#define FL_DATA_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u
#define FL_GLOB_HI      0x000FF000u
#define FL_OTA_LO       0x000E0000u
#define FL_OTA_HI       0x000E5000u
#define FL_AUTO_LO      0x000E5000u
#define FL_AUTO_HI      0x000E7000u
#define FL_VBANK_LO     0x000E7000u
#define FL_VBANK_HI     0x000E8000u
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_VBANK_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
#define FM1_XIP(off) (fm1nor_image + (uint32_t)(off))
#define FL_XIP(off) FM1_XIP(off)
static inline const uint8_t *fm1_xip_ptr(uint32_t off) { return fm1nor_image + off; }
/* fm1_flash_id.h (1.11.0): the two 1 MiB parts the FM-1 ships with */
#define FL_JEDEC_PUYA 0x856014u
#define FL_JEDEC_UCUN 0xB36014u
#define FL_JEDEC_OK(id) ((id) == FL_JEDEC_PUYA || (id) == FL_JEDEC_UCUN)
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the expected 1 MiB part */
static uint32_t fl_status_ram(void) { return 0; }
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    if (!FL_RANGE_OK(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    if (took_us)
        *took_us = 45000u;
    return fm1nor_erase(off) ? -2 : 0;
}
static int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    if (!FL_RANGE_OK(off, n) || n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u)
        return -1;                                   /* one page, no wrap */
    if (took_us)
        *took_us = 700u;
    return fm1nor_prog(off, src, n) ? -2 : 0;
}
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n) { return fm1nor_read(off, dst, n) ? -2 : 0; }
static void fl_plain_window_init(void) {}

/* ---------------------------------------------------------- fm1_flash_step.h --- */
/* 1.11.0's store writes in steps (storage_hw.c st_op, #656): the real one suspends the chip so the interrupts run
 * between steps. Here there are no interrupts to serve: the write is done in its first step, on the same image and
 * with the same range rule as fl_erase4k_ram / fl_prog_ram. The struct as the real header has it (st_op fills it) */
typedef struct {
    uint32_t op, off, n;
    uint8_t hdr[4];
    const uint8_t *src;
    volatile int32_t *quiet;
    uint32_t quiet_n;
    uint32_t serve;
    uint32_t run_min, run_max, serve_max;
    uint32_t state;
    uint32_t t_start, off_max, sus_fail;
    int rc;
} fl_step_t;
static void fl_step_ram(fl_step_t *b)
{
    int r = b->op == 0x20u ? fl_erase4k_ram(b->off, 0) : b->op == 0x02u ? fl_prog_ram(b->off, b->src, b->n, 0) : -1;
    b->rc = r == -2 ? -4 : r;                        /* (-1 outside the store's areas; an image failure as a timeout) */
    b->state = 2u;
}
static inline uint32_t irq_save(void) { return 0; }
static inline void irq_restore(uint32_t f) { (void)f; }
static int fl_erase4k(uint32_t off, uint32_t *took_us) { return fl_erase4k_ram(off, took_us); }
static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n)
{
    uint32_t took;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu);
        int rc;
        if (chunk > n)
            chunk = n;
        rc = fl_prog_ram(off, src_ram, chunk, &took);
        if (rc)
            return rc;
        off += chunk;
        src_ram += chunk;
        n -= chunk;
    }
    return 0;
}
static int fl_verify_xip(uint32_t off, const uint8_t *ref, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (off + i >= FM1NOR_SIZE || fm1nor_image[off + i] != ref[i])
            return -(int)(i + 1);
    return 0;
}

/* -------------------------------------------------------------- fm1_lcd_hw.h --- */
/* an ST7789 by its commands (lcd.c): CASET 2A / RASET 2B (4 bytes each), RAMWR 2C then the pixels, big-endian, into the
 * window row by row; DISPOFF 28 / SLPIN 10 dark until DISPON 29 (the picture kept, as the panel's RAM) */
static uint16_t fm1core_mt_lcd[240u * 240u];        /* the panel's RAM as sent (big-endian bytes) */
static uint16_t fm1core_mt_lcd_shown[240u * 240u];  /* what the panel shows: its RAM, or dark while off */
static uint32_t fm1core_mt_lcd_writes;
static uint8_t fm1core_mt_lcd_cmd, fm1core_mt_lcd_arg[4], fm1core_mt_lcd_argn, fm1core_mt_lcd_on = 1, fm1core_mt_bl = 1;
static uint8_t fm1core_mt_lcd_half, fm1core_mt_lcd_hb;   /* a pixel's first byte waiting */
static uint32_t fm1core_mt_xs, fm1core_mt_xe = 239u, fm1core_mt_ys, fm1core_mt_ye = 239u, fm1core_mt_x, fm1core_mt_y;
static uint32_t fm1_lcd_timeouts;
static inline void fm1_lcd_hw_init(void) {}
static inline void fm1_lcd_backlight(uint32_t on) { fm1core_mt_bl = (uint8_t)(on != 0); fm1core_mt_lcd_writes++; }
static inline void fm1_lcd_baud(uint32_t b) { (void)b; }
static void fm1_lcd_wait(void) {}
static inline void fm1_lcd_deselect(void) {}
static inline void fm1_lcd_send_cmd(uint8_t c)
{
    fm1core_mt_lcd_cmd = c;
    fm1core_mt_lcd_argn = 0;
    fm1core_mt_lcd_half = 0;
    if (c == 0x2Cu) {
        fm1core_mt_x = fm1core_mt_xs;
        fm1core_mt_y = fm1core_mt_ys;
    } else if (c == 0x28u || c == 0x10u) {
        fm1core_mt_lcd_on = 0;
        fm1core_mt_lcd_writes++;
    } else if (c == 0x29u) {
        fm1core_mt_lcd_on = 1;
        fm1core_mt_lcd_writes++;
    }
}
static void fm1core_mt_lcd_px(uint8_t hi, uint8_t lo)
{
    if (fm1core_mt_y > fm1core_mt_ye || fm1core_mt_y >= 240u)
        return;                                      /* (past the window: dropped, as the panel does) */
    if (fm1core_mt_x < 240u)
        fm1core_mt_lcd[fm1core_mt_y * 240u + fm1core_mt_x] = (uint16_t)(hi | (uint16_t)lo << 8);
    if (++fm1core_mt_x > fm1core_mt_xe) {
        fm1core_mt_x = fm1core_mt_xs;
        fm1core_mt_y++;
    }
}
static inline void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i;
    switch (fm1core_mt_lcd_cmd) {
    case 0x2Au:
    case 0x2Bu:
        for (i = 0; i < n && fm1core_mt_lcd_argn < 4u; i++)
            fm1core_mt_lcd_arg[fm1core_mt_lcd_argn++] = b[i];
        if (fm1core_mt_lcd_argn == 4u) {
            const uint32_t s = (uint32_t)fm1core_mt_lcd_arg[0] << 8 | fm1core_mt_lcd_arg[1];
            const uint32_t e = (uint32_t)fm1core_mt_lcd_arg[2] << 8 | fm1core_mt_lcd_arg[3];
            if (fm1core_mt_lcd_cmd == 0x2Au) {
                fm1core_mt_xs = s;
                fm1core_mt_xe = e;
            } else {
                fm1core_mt_ys = s;
                fm1core_mt_ye = e;
            }
        }
        break;
    case 0x2Cu:
    case 0x3Cu:
        for (i = 0; i < n; i++) {
            if (!fm1core_mt_lcd_half) {
                fm1core_mt_lcd_hb = b[i];
                fm1core_mt_lcd_half = 1;
            } else {
                fm1core_mt_lcd_px(fm1core_mt_lcd_hb, b[i]);
                fm1core_mt_lcd_half = 0;
            }
        }
        fm1core_mt_lcd_writes++;
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- fm1_input.h --- */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
#define FM1_KQ 32u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;
    uint8_t kq[FM1_KQ];          /* note-key edges in order: key n | 0x80 pressed */
    uint8_t kq_w, kq_r;
    uint8_t raw[FM1_NCOL];
    uint8_t cnt[FM1_NKEY];
    uint32_t note_t0[27];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];
    uint16_t enc_still[FM1_NENC];
    int8_t enc_sub[FM1_NENC];
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    int8_t enc_dir[FM1_NENC];
    uint32_t enc_dfr[FM1_NENC];
    uint32_t frames;
} fm1_in;
static volatile uint32_t fm1_virt_btn, fm1_virt_note;   /* the device test mode's keys (main.c m1_test_key) */
static volatile int16_t fm1_virt_enc[FM1_NENC];
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint8_t fm1_led_breath[FM1_NCOL];
static uint8_t fm1_led_mid[FM1_NCOL];
static uint8_t fm1_br_step = 1;                     /* the breath's speed: 1, 2 or 4 frames a frame */
static uint32_t fm1core_mt_dim_level;               /* fm1_led_dim_level: 0 the glow, 1 DIM LO, 2 DIM HI */
static void fm1_led_dim_level(uint32_t level) { fm1core_mt_dim_level = level; }
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}                 /* (the scan: the glue's fm1core_mt_in) */
static void fm1__led_lines(uint32_t rowmask) { (void)rowmask; }
static inline uint32_t fm1__lock(void) { return 0; }
static inline void fm1__unlock(uint32_t v) { (void)v; }
static int fm1_enc_waiting(uint32_t e) { return fm1_in.enc_steps[e] + fm1_virt_enc[e] != 0; }
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1_in.enc_steps[e] + fm1_virt_enc[e];
    fm1_in.enc_steps[e] = 0;
    fm1_virt_enc[e] = 0;
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
static int fm1_input_key_peek(void)
{
    uint8_t r = fm1_in.kq_r;
    return r == fm1_in.kq_w ? -1 : (int)fm1_in.kq[r % FM1_KQ];
}
static void fm1_input_key_pop(void)
{
    if (fm1_in.kq_r != fm1_in.kq_w)
        fm1_in.kq_r++;
}
static void fm1_led_key(uint32_t id, int on)
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
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }
static int fm1_led_anim_on(void) { return 0; }
static void fm1_led_idle_start(uint32_t glow) { (void)glow; }
static int fm1_led_idle_on(void) { return 0; }
static void fm1_led_anim_stop(void) {}

/* ---------------------------------------------------------------- fm1_uart.h --- */
static inline void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len) { (void)ring; (void)len; }
static inline uint32_t fm1_uart1_rx_take(void) { return 0; }   /* (no bytes on the TRS jack) */

/* ----------------------------------------------------------------- fm1_usb.h --- */
static inline void fm1_usb_reset(void) {}
static inline void fm1_usb_attach(void *ep0) { (void)ep0; }
static inline void fm1_usb_off(void) {}
static inline uint32_t fm1_usb_sie_on(void) { return 4u; }
static inline void fm1_usb_sie_wr_start(uint32_t r, uint32_t v) { (void)r; (void)v; }
static inline void fm1_usb_sie_rd_start(uint32_t r) { (void)r; }
static inline uint32_t fm1_usb_sie_done(void) { return 0x8000u; }
static inline uint32_t fm1_usb_sie_data(void) { return 0; }
static inline void fm1_usb_ep0_buf(void *p) { (void)p; }
static inline void fm1_usb_ep_txbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static inline void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static inline void fm1_usb_ep0_send(void *p, uint32_t n) { (void)p; (void)n; }
static inline void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n) { (void)ep; (void)p; (void)n; }
static inline void fm1_usb_ep4_txbuf(void *p) { (void)p; }
static inline void fm1_usb_ep4_send(void *p, uint32_t n) { (void)p; (void)n; }
static inline void fm1_usb_rx_sync(void) {}
static inline void fm1_usb_ep_enable(uint32_t eps) { (void)eps; }
static inline uint32_t fm1_usb_sof_take(void) { return 1u; }
/* fm1_clock.h / fm1_power.h (1.13.0, #735): the CPU clock and the rails read for DIAG page 131, the stock V15 supply
 * setup at boot. No registers here: the setup counts as done and checked, the probe reads zeros */
enum { FM1_POWER_NOT_RUN, FM1_POWER_OK, FM1_POWER_FAILED };
static uint8_t fm1_power_state = FM1_POWER_OK;
static uint32_t fm1_power_before;
static inline void fm1_power_boot(void) { fm1_power_state = FM1_POWER_OK; }
static inline uint32_t fm1_power_report(void) { return (uint32_t)fm1_power_state; }
static inline void fm1_clock_probe(uint32_t v[8]) { for (int i = 0; i < 8; i++) v[i] = 0; }

#endif
