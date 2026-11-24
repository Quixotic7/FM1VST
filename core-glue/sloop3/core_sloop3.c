/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 3 (https://github.com/isod89/sloop3-fm1, the pinned submodule cores/sloop3-fm1): the core unit. SLOOP 3's
 * firmware, fw/main.c, unchanged, on a simulated FM-1, as its author's own PC twin runs it (host/hal.c, `make twin`):
 * this file is that HAL (hal/fm1/*.h) rewritten for a core (an .rfm1 is Wasm: no mmap, no signals, no threads), then
 * fw/main.c itself, then the glue to and from the ABI (core-api/fm1core.h).
 *   time       the twin's: the firmware's clock (time_ticks, 60 MHz) is a simulated ns count that each HAL call moves
 *              on by what it costs on the device (a panel column 60 us, a flash read 0.4 us a byte, an ADC read
 *              0.2 ms, a tile's SPI time); main.c's app_main never returns, so it runs on a coroutine (common/coro.h)
 *              that frame() / idle() resume until the firmware's clock reaches the host's ms. It gives the clock back
 *              where the device waits: the panel's step, the flash's busy poll, a tile still being sent, a us wait
 *   audio      main.c's two 256-frame halves (24 bits in int32, 44117.6 Hz: the stock audio PLL) are rendered when the
 *              output ring is short of two halves (the ABI's 44.1 kHz pulls it), then served 128 frames at a time as
 *              int16 (>> 8)
 *   two cores  the voices are split over two cores (snd_render_voices 0/2 and 1/2, then snd_mix) as on the device:
 *              the second core's job runs on this thread when main.c waits for it (cpu1_wait), as the twin does
 *   flash      common/nor.c's 1 MiB image, the store's erases (45 ms) and page writes timed as the twin times them,
 *              the flash window (XIP, flash 0x4000 up) off while they run: the window is the image itself. The
 *              factory sample bank (assets/samples.bin, flash 0x48800 as tools/mkfw.py places it) is written into the
 *              image at power-on when it is not there; projects, user sounds, user samples, the FM6 and SYN banks and
 *              the settings are the firmware's own store in the image (flash() / flash_dirty())
 *   panel      buttons, keys and encoders in the matrix order (SLOOP 3's ids are the matrix's; its calibration remap,
 *              panel_map, is kept); the LEDs at their four levels; the screen's tiles into the panel's RAM
 *   MIDI       usb.c's packets: the host's MIDI in, MIDI out (the web editor's SysEx too)
 * Not here (hardware): the TRS jack, USB audio out, the watchdog, the 4 kHz interrupt (the panel is read directly),
 * the chip's download mode (RESCUE + REC halts the core: exit 3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"
#include "coro.h"
#include "nor.h"

#include "../../cores/sloop3-fm1/hal/fm1/fm1.h"
#include "../../cores/sloop3-fm1/hal/fm1/board.h"
#include "../../cores/sloop3-fm1/hal/fm1/audio.h"
#include "../../cores/sloop3-fm1/hal/fm1/cpu1.h"
#include "../../cores/sloop3-fm1/hal/fm1/adc.h"
#include "../../cores/sloop3-fm1/hal/fm1/panel.h"
#include "../../cores/sloop3-fm1/hal/fm1/flash.h"
#include "../../cores/sloop3-fm1/hal/fm1/usb.h"
#include "../../cores/sloop3-fm1/hal/fm1/uart.h"
#include "../../cores/sloop3-fm1/hal/fm1/irq.h"
#include "../../cores/sloop3-fm1/hal/fm1/sys.h"

/* the factory sample bank, at its flash place (tools/mkfw.py BANK_AT: area 0x44800 = flash 0x48800) */
static const uint8_t fm1core_s3_bank[] = {
#embed "../../cores/sloop3-fm1/assets/samples.bin"
};
#define FM1CORE_S3_BANK_AT 0x48800u

/* ---------------------------------------------------------------- time */
static uint64_t fm1core_s3_ns;                /* the firmware's clock */
static uint64_t fm1core_s3_until;             /* the coroutine runs until the clock reaches this (the host's ms) */
static uint32_t fm1core_s3_lsb = 60;
static void fm1core_s3_adv(uint32_t ns) { fm1core_s3_ns += ns; }
static void fm1core_s3_wait(void)              /* where the device waits: the clock back to the host, if it is due */
{
    if (fm1core_s3_ns >= fm1core_s3_until && fm1coro_inside())
        fm1coro_yield();
}
void time_init(uint32_t m) { fm1core_s3_lsb = m; }
uint32_t time_ticks(void) { fm1core_s3_adv(50); return (uint32_t)(fm1core_s3_ns * fm1core_s3_lsb / 1000u); }
uint32_t time_mhz(void) { return fm1core_s3_lsb; }
void time_wait_us(uint32_t us) { fm1core_s3_adv(us * 1000u); fm1core_s3_wait(); }

/* ---------------------------------------------------------------- the flash and its window */
uint8_t *host_win;                             /* hal/fm1/flash.h FLASH_WIN: flash 0x4000.. */
static int fm1core_s3_win_off, fm1core_s3_busy;
static uint64_t fm1core_s3_busy_until;
static uint32_t fm1core_s3_erases, fm1core_s3_writes;
int flash_open(void) { return 0; }
uint32_t flash_jedec(void) { return 0x00EF4014u; }
int flash_busy(void)
{
    fm1core_s3_adv(2000);
    fm1core_s3_wait();
    if (fm1core_s3_busy && fm1core_s3_ns < fm1core_s3_busy_until)
        return 1;
    fm1core_s3_busy = 0;
    fm1core_s3_win_off = 0;
    return 0;
}
void flash_wait_idle(void) { while (flash_busy()) { } }
int flash_read(uint32_t a, void *dst, uint32_t n)
{
    if (a > FM1NOR_SIZE || n > FM1NOR_SIZE - a)
        return -1;
    while (flash_busy()) { }
    fm1core_s3_adv(n * 400u);                 /* (20 MHz SPI: 0.4 us a byte) */
    memcpy(dst, fm1nor_image + a, n);
    return 0;
}
int flash_erase_start(uint32_t a)
{
    if ((a & 4095u) || a >= FM1NOR_SIZE || flash_busy())
        return -1;
    if (a < 0x4000u) {                         /* (m-bootloader's: the twin stops there) */
        printf("%s: an erase of m-bootloader's sector 0x%X refused\n", FM1CORE_ID, (unsigned)a);
        return -1;
    }
    fm1nor_erase(a);
    fm1core_s3_erases++;
    fm1core_s3_win_off = 1;
    fm1core_s3_busy = 1;
    fm1core_s3_busy_until = fm1core_s3_ns + 45000000ull;   /* 45 ms */
    return 0;
}
int flash_program_start(uint32_t a, const void *src, uint32_t n)
{
    if (!n || (a & 255u) + n > 256u || a + n > FM1NOR_SIZE || flash_busy())
        return -1;
    if (a < 0x4000u) {
        printf("%s: a write to m-bootloader at 0x%X refused\n", FM1CORE_ID, (unsigned)a);
        return -1;
    }
    fm1nor_prog(a, src, n);
    fm1core_s3_writes++;
    fm1core_s3_win_off = 1;
    fm1core_s3_busy = 2;
    fm1core_s3_busy_until = fm1core_s3_ns + 700000ull + n * 3000ull;
    return 0;
}
int flash_xip_on(void) { return !fm1core_s3_win_off; }
void flash_plain_from(uint32_t from) { (void)from; }      /* (the device's cipher below it: nothing to undo here) */
void flash_xip_forget(uint32_t off, uint32_t len) { (void)off; (void)len; }   /* (the window is the image) */

/* ---------------------------------------------------------------- the screen: the panel's RAM, big-endian */
static uint16_t fm1core_s3_lcd[240 * 240];
static uint32_t fm1core_s3_lcd_writes;
static uint64_t fm1core_s3_tile_until;
void lcd_init(void) { memset(fm1core_s3_lcd, 0, sizeof fm1core_s3_lcd); fm1core_s3_lcd_writes++; }
void lcd_fill(int x, int y, int w, int h, uint16_t c)
{
    const uint16_t be = (uint16_t)(c >> 8 | c << 8);
    for (int j = y; j < y + h && j < 240; j++)
        for (int i = x; i < x + w && i < 240; i++)
            if (i >= 0 && j >= 0)
                fm1core_s3_lcd[j * 240 + i] = be;
    fm1core_s3_lcd_writes++;
}
void lcd_text(int col, int row, const char *s, uint16_t fg, uint16_t bg) { (void)col; (void)row; (void)s; (void)fg; (void)bg; }
void lcd_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale) { (void)x; (void)y; (void)c; (void)fg; (void)bg; (void)scale; }
void lcd_tile_start(int x, int y, int w, int h, const void *px)
{
    const uint8_t *b = px;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const int X = x + i, Y = y + j;
            if (X >= 0 && X < 240 && Y >= 0 && Y < 240)
                memcpy(&fm1core_s3_lcd[Y * 240 + X], b + 2 * (j * w + i), 2);   /* (the bytes as sent) */
        }
    fm1core_s3_lcd_writes++;
    fm1core_s3_tile_until = fm1core_s3_ns + (uint64_t)w * h * 16u * 1000u / 12u;   /* (the panel's SPI: 12 MHz) */
}
int lcd_tile_busy(void)
{
    fm1core_s3_adv(3000);
    fm1core_s3_wait();
    return fm1core_s3_ns < fm1core_s3_tile_until;
}
void keys_init(void) { }
int key_down(uint32_t c) { (void)c; return 0; }

/* ---------------------------------------------------------------- the panel */
static uint32_t fm1core_s3_btn, fm1core_s3_keys;   /* matrix buttons, note keys held (the tick's) */
static int32_t fm1core_s3_enc[7];                  /* matrix encoders' detents not taken */
static uint32_t fm1core_s3_btn_seen, fm1core_s3_keys_seen;
static uint32_t fm1core_s3_steps;
static uint8_t fm1core_s3_b_raw[14] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static uint8_t fm1core_s3_b_id[14] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static uint8_t fm1core_s3_e_raw[7] = {0, 1, 2, 3, 4, 5, 6};
static int8_t fm1core_s3_e_dir[7] = {1, 1, 1, 1, 1, 1, 1};
static int8_t fm1core_s3_kcol[PANEL_NKEY], fm1core_s3_krow[PANEL_NKEY];
static uint8_t fm1core_s3_led[PANEL_NCOL], fm1core_s3_lv2[PANEL_NCOL], fm1core_s3_lv1[PANEL_NCOL],
    fm1core_s3_glow[PANEL_NCOL];
static uint32_t fm1core_s3_ids(uint32_t raw)
{
    uint32_t o = 0;
    for (int i = 0; i < 14; i++)
        if ((raw >> i) & 1u)
            o |= 1u << fm1core_s3_b_id[i];
    return o;
}
void panel_init(void)
{
    for (int i = 0; i < PANEL_NKEY; i++)
        fm1core_s3_kcol[i] = fm1core_s3_krow[i] = -1;
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < PANEL_NCOL; c++)
            if (PANEL_MAP[r][c] >= 0) {
                fm1core_s3_kcol[PANEL_MAP[r][c]] = (int8_t)c;
                fm1core_s3_krow[PANEL_MAP[r][c]] = (int8_t)r;
            }
}
int panel_step(void)                           /* a column: 60 us; a frame each 11 */
{
    fm1core_s3_adv(60000);
    fm1core_s3_wait();
    return ++fm1core_s3_steps % 11u == 0;
}
void panel_dark(void) { }
void panel_ticking(void) { }
void panel_tick(void) { }
void panel_glow_us(int us) { (void)us; }
uint32_t panel_keys(void) { return fm1core_s3_keys; }
uint32_t panel_buttons(void) { return fm1core_s3_ids(fm1core_s3_btn); }
uint32_t panel_key_edges(uint32_t *rel)
{
    const uint32_t p = fm1core_s3_keys & ~fm1core_s3_keys_seen;
    if (rel)
        *rel = fm1core_s3_keys_seen & ~fm1core_s3_keys;
    fm1core_s3_keys_seen = fm1core_s3_keys;
    return p;
}
uint32_t panel_button_edges(uint32_t *rel)
{
    const uint32_t p = fm1core_s3_btn & ~fm1core_s3_btn_seen;
    if (rel)
        *rel = fm1core_s3_ids(fm1core_s3_btn_seen & ~fm1core_s3_btn);
    fm1core_s3_btn_seen = fm1core_s3_btn;
    return fm1core_s3_ids(p);
}
int panel_enc_take(int e)
{
    const int r = fm1core_s3_e_raw[e], s = fm1core_s3_enc[r];
    fm1core_s3_enc[r] = 0;
    return s * fm1core_s3_e_dir[e];
}
static int fm1core_s3_led_at(int id, int *c, uint8_t *b)
{
    if (id >= 0 && id < 14)
        id = fm1core_s3_b_raw[id];
    if (id < 0 || id >= PANEL_NKEY || fm1core_s3_krow[id] < 1 || fm1core_s3_krow[id] > 4)
        return 0;
    *c = fm1core_s3_kcol[id];
    *b = (uint8_t)(1u << fm1core_s3_krow[id]);
    return 1;
}
void panel_led(int id, int on)
{
    int c;
    uint8_t b;
    if (!fm1core_s3_led_at(id, &c, &b))
        return;
    if (on)
        fm1core_s3_led[c] |= b;
    else
        fm1core_s3_led[c] &= (uint8_t)~b;
}
void panel_glow(int id)
{
    int c;
    uint8_t b;
    if (fm1core_s3_led_at(id, &c, &b))
        fm1core_s3_glow[c] |= b;
}
void panel_glow_clear(void)
{
    memset(fm1core_s3_glow, 0, sizeof fm1core_s3_glow);
    memset(fm1core_s3_lv1, 0, sizeof fm1core_s3_lv1);
    memset(fm1core_s3_lv2, 0, sizeof fm1core_s3_lv2);
}
void panel_level(int id, int lvl)              /* 0 off, 1 faint, 2 a third, 3 two thirds, 4 full */
{
    int c;
    uint8_t b;
    if (!fm1core_s3_led_at(id, &c, &b))
        return;
    fm1core_s3_led[c] &= (uint8_t)~b;
    fm1core_s3_glow[c] &= (uint8_t)~b;
    fm1core_s3_lv1[c] &= (uint8_t)~b;
    fm1core_s3_lv2[c] &= (uint8_t)~b;
    if (lvl == 1)
        fm1core_s3_glow[c] |= b;
    else if (lvl == 2)
        fm1core_s3_lv1[c] |= b;
    else if (lvl == 3)
        fm1core_s3_lv2[c] |= b;
    else if (lvl >= 4)
        fm1core_s3_led[c] |= b;
}
void panel_glow_off(void) { }
int panel_map(const uint8_t btn[14], const uint8_t enc[7], const int8_t dir[7])
{
    uint8_t seen[14] = {0}, eseen[7] = {0};
    for (int i = 0; i < 14; i++) {
        if (btn[i] >= 14 || seen[btn[i]])
            return -1;
        seen[btn[i]] = 1;
    }
    for (int i = 0; i < 7; i++) {
        if (enc[i] >= 7 || eseen[enc[i]] || (dir[i] != 1 && dir[i] != -1))
            return -1;
        eseen[enc[i]] = 1;
    }
    for (int i = 0; i < 14; i++) {
        fm1core_s3_b_raw[i] = btn[i];
        fm1core_s3_b_id[btn[i]] = (uint8_t)i;
    }
    for (int i = 0; i < 7; i++) {
        fm1core_s3_e_raw[i] = enc[i];
        fm1core_s3_e_dir[i] = dir[i];
    }
    return 0;
}
uint32_t panel_raw_buttons(void) { return fm1core_s3_btn; }
int panel_raw_enc_take(int e)
{
    const int s = fm1core_s3_enc[e];
    fm1core_s3_enc[e] = 0;
    return s;
}

/* ---------------------------------------------------------------- the audio: main.c's halves into a ring */
#define FM1CORE_S3_RING 4096u                  /* frames */
#define FM1CORE_S3_LEVEL 512u                  /* a half is free while the ring holds less than this after it */
static int32_t *fm1core_s3_abuf;
static uint32_t fm1core_s3_ahalf;              /* a half, in int32 words */
static int fm1core_s3_afill;
static int16_t fm1core_s3_ring[FM1CORE_S3_RING * 2];
static uint32_t fm1core_s3_rr, fm1core_s3_rw, fm1core_s3_under;
int audio_start(int32_t *buf, uint32_t half_words)
{
    fm1core_s3_abuf = buf;
    fm1core_s3_ahalf = half_words;
    fm1core_s3_afill = 0;
    return AUDIO_OK;
}
int audio_half_free(void)
{
    return fm1core_s3_abuf && fm1core_s3_rw - fm1core_s3_rr + fm1core_s3_ahalf / 2u <= FM1CORE_S3_LEVEL;
}
int32_t *audio_free_half(void) { return fm1core_s3_abuf + (uint32_t)fm1core_s3_afill * fm1core_s3_ahalf; }
int audio_playing_half(void) { return fm1core_s3_afill ^ 1; }
void audio_half_done(void)
{
    const int32_t *h = fm1core_s3_abuf + (uint32_t)fm1core_s3_afill * fm1core_s3_ahalf;
    for (uint32_t f = 0; f < fm1core_s3_ahalf / 2u; f++) {
        int16_t *o = &fm1core_s3_ring[(fm1core_s3_rw++ % FM1CORE_S3_RING) * 2u];
        for (int c = 0; c < 2; c++) {
            const int32_t v = h[2u * f + (uint32_t)c] >> 8;   /* (24 bits in 32) */
            o[c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    fm1core_s3_afill ^= 1;
}
void audio_stop(void) { fm1core_s3_abuf = NULL; }
uint32_t audio_rate_milli(void) { return 44117647u; }

/* ---------------------------------------------------------------- the second core: its job run when waited for */
static cpu1_fn fm1core_s3_job;
static uint32_t fm1core_s3_job_arg, fm1core_s3_jobs;
int cpu1_start(void) { return 0; }
int cpu1_running(void) { return 1; }
int cpu1_run(cpu1_fn fn, uint32_t arg)
{
    fm1core_s3_job = fn;
    fm1core_s3_job_arg = arg;
    return 1;
}
int cpu1_wait(uint32_t t)
{
    (void)t;
    if (fm1core_s3_job) {
        cpu1_fn f = fm1core_s3_job;
        fm1core_s3_job = NULL;
        f(fm1core_s3_job_arg);
        fm1core_s3_jobs++;
    }
    return 0;
}
void cpu1_stop(void) { fm1core_s3_job = NULL; }
void cpu0_traps_quiet(void) { }
uint32_t cpu1_jobs(void) { return fm1core_s3_jobs; }
uint32_t cpu1_misreads(void) { return 0; }
uint32_t cpu1_core_id(void) { return 1; }

/* ---------------------------------------------------------------- the power block, the interrupt */
void sys_download_mode(void)                   /* RESCUE + REC: the chip's download mode, which a core has not */
{
    printf("%s: the chip's download mode asked for (RESCUE + REC): halted\n", FM1CORE_ID);
    exit(3);
}
void sys_supplies_stock(void)
{
    for (int i = 0; i < 8; i++)
        time_wait_us(100);
}
uint32_t sys_reset_bits(void) { return 1u; }   /* a power-on */
void sys_wdt_arm(void) { }
void sys_wdt_feed(void) { }
void sys_wdt_stop(void) { }
uint32_t sys_p33_timeouts(void) { return 0; }
void irq_start(void (*fn)(void)) { (void)fn; }  /* (the panel and USB are served directly) */
uint32_t irq_ticks(void) { return (uint32_t)(fm1core_s3_ns / 500000u); }
void irq_off(void) { }
void irq_on(void) { }

/* ---------------------------------------------------------------- USB MIDI: a ring each way; TRS: none */
#define FM1CORE_S3_MQ 1024u
static uint32_t fm1core_s3_min[FM1CORE_S3_MQ], fm1core_s3_mout[FM1CORE_S3_MQ];
static uint32_t fm1core_s3_mi_r, fm1core_s3_mi_w, fm1core_s3_mo_r, fm1core_s3_mo_w, fm1core_s3_mlost;
void usb_midi_open(void) { }
void usb_midi_poll(void) { fm1core_s3_adv(1000); }
int usb_midi_ready(void) { return 1; }
int usb_host_live(void) { return 1; }
void usb_midi_close(void) { }
int usb_midi_take(uint8_t pk[4])
{
    if (fm1core_s3_mi_r == fm1core_s3_mi_w)
        return 0;
    const uint32_t p = fm1core_s3_min[fm1core_s3_mi_r++ % FM1CORE_S3_MQ];
    for (int i = 0; i < 4; i++)
        pk[i] = (uint8_t)(p >> (8 * i));
    return 1;
}
int usb_midi_put(const uint8_t pk[4])
{
    if (fm1core_s3_mo_w - fm1core_s3_mo_r >= FM1CORE_S3_MQ) {
        fm1core_s3_mlost++;
        return 0;
    }
    fm1core_s3_mout[fm1core_s3_mo_w++ % FM1CORE_S3_MQ] =
        (uint32_t)pk[0] | (uint32_t)pk[1] << 8 | (uint32_t)pk[2] << 16 | (uint32_t)pk[3] << 24;
    return 1;
}
uint32_t usb_midi_lost(void) { return fm1core_s3_mlost; }
void usb_tick(void) { }
void usb_by_tick(void) { }
void usb_audio_put(const int32_t *lr24, uint32_t frames) { (void)lr24; (void)frames; }
void usb_audio_rate(int mode) { (void)mode; }
void trs_midi_open(void) { }
int trs_midi_byte(void) { return -1; }
uint32_t trs_midi_count(void) { return 0; }
static int32_t fm1core_s3_master = 1023;
void adc_init(void) { }
int32_t adc_read(uint32_t ch)                  /* (0.2 ms a conversion) */
{
    fm1core_s3_adv(200000);
    return ch == ADC_CH_BATT ? 600 : fm1core_s3_master;   /* (the battery: 4.06 V, as the twin's) */
}

/* ---------------------------------------------------------------- the firmware */
#include "../../cores/sloop3-fm1/fw/main.c"

#include "exit_end.h"
#include "../common/panel.h"              /* (by its path: hal/fm1/panel.h is the firmware's) */

static const int8_t FM1CORE_S3_KEYMAP[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;   /* = hal/fm1/fm1.h PANEL_MAP */

fm1panel_t fm1core_s3_panel;
static fm1coro_t *fm1core_s3_co;
static fm1p_edges_t fm1core_s3_keys_e, fm1core_s3_btn_e;

static void fm1core_s3_main(void *arg)
{
    (void)arg;
    app_main(60);                              /* (m-bootloader's LSB clock: 60 MHz) */
}

static void fm1core_s3_out(void)
{
    fm1panel_t *p = &fm1core_s3_panel;
    const uint8_t *planes[4] = {fm1core_s3_led, fm1core_s3_lv2, fm1core_s3_lv1, fm1core_s3_glow};
    static const uint8_t levels[4] = {FM1P_LED_LIT, FM1P_LED_MID, FM1P_LED_DIM, FM1P_LED_FAINT};
    fm1p_leds_planes(p, planes, levels, 4, FM1P_PLAY_GREEN_POS);
    fm1p_lcd_out(p, fm1core_s3_lcd, fm1core_s3_lcd_writes);
}

int fm1core_s3_halted(void) { return fm1core_halted(); }
void fm1core_s3_shutdown(void)
{
    fm1core_run_exit();
    if (fm1core_s3_co)
        fm1coro_free(fm1core_s3_co);
    fm1core_s3_co = NULL;
}

void fm1core_s3_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                        /* (the store writes through) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}
int fm1core_s3_boot_options(int fail, const char *reason, int stage)
{
    (void)fail;
    (void)reason;
    (void)stage;
    return 0;
}

void fm1core_s3_init(int demo)
{
    fm1panel_t *p = &fm1core_s3_panel;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1core_exit_park = fm1coro_park;
    if (p->master)
        fm1core_s3_master = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    fm1p_panel_default(p);                     /* (SLOOP 3's ids are the matrix's, its encoders too) */
    fm1nor_set_stall_ms(0);                    /* (the flash's time is the twin's here: the window, not the IRQs) */
    fm1nor_open();
    if (memcmp(fm1nor_image + FM1CORE_S3_BANK_AT, fm1core_s3_bank, sizeof fm1core_s3_bank))
        fm1nor_write(FM1CORE_S3_BANK_AT, fm1core_s3_bank, sizeof fm1core_s3_bank);
    host_win = fm1nor_image + 0x4000u;
    fm1core_s3_ns = 0;
    fm1core_s3_until = 0;
    fm1core_s3_co = fm1coro_new(fm1core_s3_main, NULL, 0);
    if (!fm1core_s3_co) {
        printf("%s: no memory for the main loop's stack\n", FM1CORE_ID);
        exit(1);
        return;
    }
    fm1coro_resume(fm1core_s3_co);             /* (the start, up to its first wait) */
    if (fm1core_halted())
        return;
    if (!p->master)
        p->master = fm1core_s3_master;
    p->ready = 1;
    fm1core_s3_out();
}

void fm1core_s3_tick(uint32_t ms)
{
    fm1panel_t *p = &fm1core_s3_panel;
    if (fm1core_halted())
        return;
    fm1core_s3_until = (uint64_t)(ms + 1u) * 1000000u;   /* (this ms's end) */
    fm1core_s3_master = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    fm1core_s3_btn = fm1p_edges(&fm1core_s3_btn_e, p->buttons, fm1p_take_button_taps(p), NULL, NULL) & 0x3FFFu;
    fm1core_s3_keys = fm1p_edges(&fm1core_s3_keys_e, p->keys, fm1p_take_key_taps(p), NULL, NULL) & 0x7FFFFFFu;
    for (int i = 0; i < FM1P_NENC; i++)
        fm1core_s3_enc[i] += fm1p_take_enc(p, (uint32_t)i);
}

static void fm1core_s3_run(void)               /* frame and idle: the firmware up to this ms's end */
{
    if (fm1core_halted())
        return;
    if (fm1core_s3_co && !fm1coro_done(fm1core_s3_co))
        fm1coro_resume(fm1core_s3_co);
    if (!fm1core_halted())
        fm1core_s3_out();
}
/* the live knob labels and readouts (common/rd_cols.h): the view main.c draws (V, ui_view: each knob's label and value,
 * as app/ui.c draws them; a knob without a label is not drawn), copied after each frame */
#include "rd_cols.h"
static void fm1core_s3_rd(void)
{
    uint32_t k;
    rdc_begin();
    if (V.page == UI_SPLASH || V.page == UI_VIS)
        return;
    for (k = 0; k < 4u; k++)
        if (V.knob[k].label && V.knob[k].label[0])
            rdc_col(k, V.knob[k].label, V.knob[k].value);
}
void fm1core_s3_frame(void) { fm1core_s3_run(); fm1core_s3_rd(); }
void fm1core_s3_idle(void) { fm1core_s3_run(); }

void fm1core_s3_audio(int16_t *out, uint32_t frames)
{
    for (uint32_t f = 0; f < frames; f++) {
        if (!fm1core_halted() && fm1core_s3_rr != fm1core_s3_rw) {
            const int16_t *s = &fm1core_s3_ring[(fm1core_s3_rr++ % FM1CORE_S3_RING) * 2u];
            out[2u * f] = s[0];
            out[2u * f + 1u] = s[1];
        } else {
            out[2u * f] = out[2u * f + 1u] = 0;
            if (fm1core_s3_abuf && !fm1core_halted())
                fm1core_s3_under++;
        }
    }
}

int fm1core_s3_midi_in(uint32_t pkt)
{
    if (fm1core_halted())
        return 1;
    if (fm1core_s3_mi_w - fm1core_s3_mi_r >= FM1CORE_S3_MQ)
        return 0;
    fm1core_s3_min[fm1core_s3_mi_w++ % FM1CORE_S3_MQ] = pkt;
    return 1;
}
int fm1core_s3_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted() || fm1core_s3_mo_r == fm1core_s3_mo_w)
        return 0;
    *pkt = fm1core_s3_mout[fm1core_s3_mo_r++ % FM1CORE_S3_MQ];
    return 1;
}

/* ---- diagnostics */
void fm1core_s3_dump(void)
{
    printf("sloop3: clock %llu us, track %d, %s, voices %d, load avg %u%% peak %u%%, second core %s (%u jobs)\n",
           (unsigned long long)(fm1core_s3_ns / 1000u), trk + 1, Q.playing ? "playing" : "stopped", snd_voices(&S),
           (unsigned)load_avg, (unsigned)load_peak, two ? "on" : "off", (unsigned)fm1core_s3_jobs);
    printf("  audio ring %u frames, %u frames short; flash %s: %u erases, %u writes%s; MIDI lost %u\n",
           (unsigned)(fm1core_s3_rw - fm1core_s3_rr), (unsigned)fm1core_s3_under,
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1core_s3_erases, (unsigned)fm1core_s3_writes,
           saving ? ", saving" : "", (unsigned)fm1core_s3_mlost);
    if (fm1core_s3_co)
        printf("  the main loop's coroutine: %u resumes, its stack's peak %u of %u bytes\n",
               (unsigned)fm1coro_resumes(fm1core_s3_co), (unsigned)fm1coro_stack_peak(fm1core_s3_co),
               (unsigned)FM1CORO_STACK);
}
void fm1core_s3_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = load_avg;
}
void fm1core_s3_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "SLOOP 3, track %d%s", trk + 1, Q.playing ? ", playing" : "");
}
const int8_t *fm1core_s3_keymap(void) { return &FM1CORE_S3_KEYMAP[0][0]; }

/* ---- the flash (ABI 2): common/nor.c */
uint8_t *fm1core_s3_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_s3_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_s3_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_s3_flash_sync(void)
{
    if (fm1core_halted() || !fm1core_s3_panel.ready)
        return 1;
    return !saving && !fm1core_s3_busy;        /* (a save under way: its pages still to come) */
}
