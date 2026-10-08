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
 *   - the emu_hal_t struct is ChoralRoot's emu_hooks.h (through core-api/fm1core.h): not duplicated. */

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
/* The FM-1's 1 MiB SPI NOR as a RAM image (emu_flash[], declared in felucca_firmware.h before the sound side: the
 * SAMPLE engine reads its user slots from it), optionally backed by a file: loaded at power-on, every erase /
 * program written through at once. The NOR's rules hold: an erase sets a 4 KiB sector to 0xFF, a program can only
 * clear bits and must not cross a 256-byte page. */
static char emu_flash_path[1024];                 /* "": RAM only */
static FILE *emu_flash_f;
static uint32_t emu_flash_writes;                 /* erases + programs (the descriptor's flash_dirty) */

static void emu_flash_open(void)
{
    memset(emu_flash, 0xFF, sizeof emu_flash);
    if (!emu_flash_path[0])
        return;
    emu_flash_f = fopen(emu_flash_path, "r+b");
    if (emu_flash_f) {
        size_t n = fread(emu_flash, 1, sizeof emu_flash, emu_flash_f);
        if (n < sizeof emu_flash)                 /* short (or new): pad the file to the full image */
            memset(emu_flash + n, 0xFF, sizeof emu_flash - n);
    } else {
        emu_flash_f = fopen(emu_flash_path, "w+b");
    }
    if (!emu_flash_f) {
        fprintf(stderr, "felucca: flash file %s: cannot open (RAM only)\n", emu_flash_path);
        return;
    }
    fseek(emu_flash_f, 0, SEEK_SET);
    fwrite(emu_flash, 1, sizeof emu_flash, emu_flash_f);
    fflush(emu_flash_f);
}
static void emu_flash_sync(uint32_t off, uint32_t n)
{
    emu_flash_writes++;
    if (!emu_flash_f)
        return;
    fseek(emu_flash_f, (long)off, SEEK_SET);
    fwrite(emu_flash + off, 1, n, emu_flash_f);
    fflush(emu_flash_f);
}
static uint8_t flash_ok;                          /* storage_hw.c's: the part answered (persist_boot sets it) */
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part (storage_hw.c's check) */
#define FL_FAR(fn) (fn)
static void fl_plain_window_init(void) {}         /* (the image is plaintext: SMP_USER_XIP reads it directly) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > EMU_FLASH_SIZE || n > EMU_FLASH_SIZE - off)
        return -8;
    memcpy(dst, emu_flash + off, n);
    return 0;
}
/* the device's erase (storage_hw.c st_erase): the audio buffer zeroed and every IRQ off for the 4 KiB erase (typ.
 * ~45 ms on the FM-1's NOR): no render, the DMA loops silence. The core plays the same hole, as ChoralRoot's
 * emulator does: EMU_ERASE_MS of zero blocks, the firmware's audio ISR not run meanwhile (emu_fw_audio) */
#define EMU_ERASE_MS 45u
static uint32_t emu_stall_blocks, emu_stalls, emu_stall_blocks_all;
static int st_erase(uint32_t off)
{
    off &= ~0xFFFu;
    if (off >= EMU_FLASH_SIZE)
        return -8;
    emu_stall_blocks += (EMU_ERASE_MS * 44100u / 1000u + HALF_FRAMES - 1u) / HALF_FRAMES;
    emu_stalls++;
    memset(emu_flash + off, 0xFF, 0x1000u);
    emu_flash_sync(off, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off > EMU_FLASH_SIZE || n > EMU_FLASH_SIZE - off || (off & 0xFFu) + n > 256u)
        return -8;                                /* (a page program must not wrap: the device's rule) */
    for (i = 0; i < n; i++)
        emu_flash[off + i] &= ((const uint8_t *)src)[i];
    emu_flash_sync(off, n);
    return 0;
}
