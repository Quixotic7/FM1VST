/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL for the Felucca core (firmware side; felucca_firmware.h includes it after hostsim.c).
 * Felucca has no emulator of its own: this is ChoralRoot's tools/emu/emu_hal_fw.h redone for Felucca's sources,
 * which are unmodified. It is modelled, as that file was, on the stubs of Felucca's tests/ui_test.c (time, input,
 * display) and tests/persistence_test.c (the NOR flash hooks of storage.c), but live: the display writes
 * emu_hal.lcd, the LEDs are emu_hal.led / led_dim, the input edges and encoder steps come from emu_fw_tick (the
 * 1 ms timer, as fm1_input_tick delivers them on the device), the flash is a 1 MiB RAM image.
 *
 * Differences from emu_hal_fw.h:
 *   - no CPU lock: the core ABI (core-api/fm1core.h) runs every entry point on one thread, so the "interrupt-off"
 *     sections only count their nesting (there is no realtime mode with a timer thread here);
 *   - Felucca's storage.c also needs the flash part's JEDEC id (persist_boot: fl_jedec_ram() == 0x856014, else it
 *     stays RAM only), irq_save / irq_restore, FL_FAR and fl_plain_window_init (the XIP plaintext window: the
 *     user sample slots are read in place from the image, felucca_firmware.h SMP_USER_XIP);
 *   - emu_hal_t is core-glue/common/panel.h's fm1p_emu_hal_t (ChoralRoot's ABI 1..4 emu_hal_t): the glue translates
 *     it to and from the ABI's fm1panel_t (core_felucca.c);
 *   - the NOR image is core-glue/common/nor.c's (fm1nor_image, the stage, the file, the erase stall). */

/* -------------------------------------------------------- time --- */
#define FM1_TICKS_PER_US 1u                       /* fm1_ticks: microseconds */
static uint64_t emu_t0_ns;
static uint32_t emu_sim_us;                       /* simulated time (hal.ready == 2): the device clock */
static uint8_t emu_sim;
static uint32_t fm1_ticks(void)
{
    struct timespec ts;
    if (emu_sim)
        return emu_sim_us;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec - emu_t0_ns) / 1000u);
}
static void fm1_wdt_feed(void) {}

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
#define fm1_led (emu_hal.led)
#define fm1_led_dim (emu_hal.led_dim)
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
/* The FM-1's 1 MiB SPI NOR: core-glue/common/nor.c's image (fm1nor_image = emu_flash, declared through felucca_firmware.h
 * before the sound side), optionally backed by a file (every erase / program written through at once) or booted from
 * the staged bytes. The NOR's rules hold: an erase sets a 4 KiB sector to 0xFF (and stalls the audio ~45 ms, as the
 * device's erase does: the glue's audio() plays fm1nor_stall_take()'s blocks as silence), a program can only clear
 * bits and must not cross a 256-byte page. */
static uint8_t flash_ok;                          /* storage_hw.c's: the part answered (persist_boot sets it) */
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part (storage_hw.c's check) */
#define FL_FAR(fn) (fn)
static void fl_plain_window_init(void) {}         /* (the image is plaintext: SMP_USER_XIP reads it directly) */
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return fm1nor_erase(off); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { return fm1nor_prog(off, src, n); }
