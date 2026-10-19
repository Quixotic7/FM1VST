/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL for the Melodee core (firmware side; melodee_firmware.h includes it after hostsim.c).
 * Melodee has no emulator of its own: this is ChoralRoot's tools/emu/emu_hal_fw.h redone for Melodee's sources,
 * which are unmodified (and the Felucca core's felucca_hal.h, which it follows line for line but for the LEDs). It
 * is modelled on the stubs of Melodee's tests/ui_test.c (time, input, display) and tests/persistence_test.c (the NOR
 * flash hooks of storage.c), but live: the display writes
 * emu_hal.lcd, the LEDs are fm1_led / fm1_led_dim, the input edges and encoder steps come from emu_fw_tick (the
 * 1 ms timer, as fm1_input_tick delivers them on the device), the flash is a 1 MiB RAM image.
 *
 * Differences from emu_hal_fw.h:
 *   - no CPU lock: the core ABI (core-api/fm1core.h) runs every entry point on one thread, so the "interrupt-off"
 *     sections only count their nesting (there is no realtime mode with a timer thread here);
 *   - the LEDs are the firmware's own arrays (fm1_led and the three dim planes with their duty masks, below), not
 *     emu_hal's: core_melodee.c turns them into the panel's levels after every frame;
 *   - fm1_wdt_feed is where the firmware waits: inside the main loop's coroutine (melodee_fw.c) it yields to the
 *     host until the next device millisecond (panel_setup's loops feed the watchdog while they wait for a button or
 *     a knob);
 *   - Melodee's storage.c also needs the flash part's JEDEC id (persist_boot: fl_jedec_ram() == 0x856014, else it
 *     stays RAM only), irq_save / irq_restore, FL_FAR and fl_plain_window_init (the XIP plaintext window);
 *   - emu_hal_t is core-glue/common/panel.h's fm1p_emu_hal_t (ChoralRoot's ABI 1..4 emu_hal_t): the glue translates
 *     it to and from the ABI's fm1panel_t (core_melodee.c);
 *   - the NOR image is core-glue/common/nor.c's (fm1nor_image, the stage, the file, the erase stall). */
#include "coro.h"                                 /* (core-glue/common) */

/* -------------------------------------------------------- time --- */
/* fm1_ticks: TIMER4, 24 MHz (FM1_TICKS_PER_US, melodee_firmware.h); simulated (panel ready == 2: what FM1VST always
 * runs) it is the device millisecond's start, wrapping as the 32-bit counter does (every 178.9 s) */
static uint64_t emu_t0_ns;
static uint32_t emu_sim_ms;                       /* simulated time (hal.ready == 2): the device clock */
static uint8_t emu_sim;
static uint32_t fm1_ticks(void)
{
    struct timespec ts;
    if (emu_sim)
        return emu_sim_ms * (1000u * FM1_TICKS_PER_US);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec - emu_t0_ns) * FM1_TICKS_PER_US / 1000u);
}
static void fm1_wdt_feed(void)                   /* a wait: back to the host (the main loop's coroutine) */
{
    if (fm1coro_inside())
        fm1coro_yield();
}

/* ------------------------------------------- the CPU lock (ISRs) --- */
/* one thread runs the whole device (the ABI's threading contract): the timer "ISR", the audio "ISR" and the main
 * loop never overlap, so an interrupt-off section has nothing to exclude */
static uint32_t emu_cpu_depth;
static void fm1_irq_off(void) { emu_cpu_depth++; }
static void fm1_irq_on(void)
{
    if (emu_cpu_depth)
        emu_cpu_depth--;
}
static uint32_t irq_save(void)                    /* storage.c / project.c (the device: PSR, cli) */
{
    fm1_irq_off();
    return 0;
}
static void irq_restore(uint32_t f)
{
    (void)f;
    fm1_irq_on();
}

/* --------------------------------------------- keys, LEDs, knobs --- */
#define FM1_NCOL 11u
#define FM1_KEYMAP emu_keymap
/* hal/fm1_input.h's LEDs, as the firmware declares them: fm1_led (lit: full duty), and THREE dim planes, each with
 * its duty mask (0 full, 1 1/2, 3 1/4, 7 1/8, 0xFF dark): plane 0 the idle keys (LIGHTS), 1 the idle buttons, 2 the
 * breathing ones (ui_input.c ui_leds: its mask swept 0 1 3 7 FF 7 3 1 over a second). The 0.11 glue declared two
 * planes and 1.4's ui_leds writes fm1_led_dim[2][c] and fm1_led_dim_mask[2]: past the end, hidden by -w. The
 * levels the panel gets: core_melodee.c me_leds_out (an LED's brightest plane, as fm1_led_pwm_level drives it) */
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[3][FM1_NCOL], fm1_led_dim_mask[3];
_Static_assert(sizeof fm1_led_dim == 3u * FM1_NCOL && sizeof fm1_led_dim_mask == 3u, "hal/fm1_input.h: three planes");
static uint32_t host_pressed, host_notes;         /* edges since the last take */
static int32_t host_enc[7];
static uint32_t fm1_input_edges(uint32_t *released)   /* hal/fm1_input.h (the UI passes 0) */
{
    uint32_t p = host_pressed;
    if (released)
        *released = 0;
    host_pressed = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t n = host_notes;
    host_notes = 0;
    return n;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = 0;
    return s;
}

/* ----------------------------------------------------- display --- */
/* the ST7789's RAM as sent: fills in big-endian RGB565 (lcd.c swaps them for the SPI), blits as given */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            emu_hal.lcd[(y + j) * 240u + x + i] = s;
    emu_hal.lcd_writes++;
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            emu_hal.lcd[(y + j) * 240u + x + i] = p[j * w + i];
    emu_hal.lcd_writes++;
}

/* ------------------------------------------- audio (for audio.c) --- */
#define FM1_AUDIO_HALF 1u
static uint32_t emu_half;                         /* the half buffer the "DMA" gives the ISR */
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return emu_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), int prio) { (void)b; (void)n; (void)isr; (void)prio; }
void isr_alnk0(void) {}

/* ------------------------------------------------ flash (storage.c's hooks) --- */
/* The FM-1's 1 MiB SPI NOR: core-glue/common/nor.c's image (fm1nor_image = emu_flash, declared through melodee_firmware.h
 * before the sound side), optionally backed by a file (every erase / program written through at once) or booted from
 * the staged bytes. The NOR's rules hold: an erase sets a 4 KiB sector to 0xFF (and stalls the audio ~45 ms, as the
 * device's erase does: the glue's audio() plays fm1nor_stall_take()'s blocks as silence), a program can only clear
 * bits and must not cross a 256-byte page. */
static uint8_t flash_ok;                          /* storage_hw.c's: the part answered (persist_boot sets it) */
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part (storage_hw.c's check) */
#define FL_FAR(fn) (fn)
static void fl_plain_window_init(void) {}         /* (the image is plaintext) */
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return fm1nor_erase(off); }
/* storage_hw.c st_prog -> hal/fm1_flash.h fl_write, which programs page by page (a record that starts past a page's
 * start, the compact user preset bank's, spans pages): fm1nor_prog_span splits it the same way */
static int st_prog(uint32_t off, const void *src, uint32_t n) { return fm1nor_prog_span(off, src, n); }
