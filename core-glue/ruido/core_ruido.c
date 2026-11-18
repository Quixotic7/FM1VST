/* SPDX-License-Identifier: GPL-3.0-only */
/* Ruido (https://github.com/miguelhd/ruido, the pinned, unmodified submodule cores/ruido: GPL-3.0-only): the core unit.
 * Ruido is a portable C++17 firmware (src/: a four-oscillator Schmitt-trigger drone, its controller and its scope HUD)
 * with two platforms of its own: the FM-1's (platforms/fm1: fm1_main.c and ruido_fm1_bridge.cpp over zvenson/dinghy's
 * platform.c, which its build fetches at f77bf03 into a container; the hardware: the two cores, the panel scan, the
 * SARADC pressure, the LCD's DMA, USB) and the browser simulator's (platforms/sim/wasm_bridge.cpp: the same sources,
 * unity-included, driven from a page). This core is the second kind on the ABI (core-api/fm1core.h): ruido_glue.cpp
 * (C++) unity-includes the sources as wasm_bridge.cpp does and uses the device bridge's portable headers unchanged
 * (the panel, the NVS, the quantizer); this unit is the ABI, the panel's edges and the NOR. Dinghy's platform is not
 * built (nothing of it but its store's record format, below), so nothing is fetched.
 *   init      fm1_main.c app_main without the hardware: ruido_fm1_construct (MASTER's first reading), the NVS boot
 *             load (the four scenes 0xF4000..0xF7FFF, the settings 0xF8000, the LIVE patch 0xF9000, their journals),
 *             ruido_fm1_restore_default_patch; the CRT boot intro armed
 *   frame /   app_main's loop: the panel polled (poll_panel_inputs: pressed / released edges, held masks, detents; a
 *   idle      tap is a press and a release in one poll), MASTER as platform.c's quadratic master_q12 ((pot * pot) >> 8)
 *             into the controller, the NVS poll (journal appends; a scene's SAVE at once; settings and LIVE after
 *             1.5 s idle), and every 16 ms the frame: the controller's tick, the HUD prepared, the 15 strips rendered
 *             (native RGB565, fm1p_lcd_out_native)
 *   audio     Core 1's 64-frame blocks at 44.1 kHz (the two-core unit's rate; the solo half-rate fallback is the
 *             device's own), TPDF-quantized to Q15 as the device does; a flash erase's blocks are silent
 *   MIDI in   System Real-Time (F8 FA FB FC) to the controller's clock (ruido_fm1_on_midi_rt), stamped with the device
 *             ms; nothing else is read (fm1_main.c poll_midi_clock)
 *   LEDs      the controller's LedState at the device's matrix places (Fm1LedModulator::updateTargets), its perceptual
 *             0..1 as 0..255; PLAY's green die and the STOP die apart
 * Not here: Core 1 and its fallbacks, the boot benchmarks, the carbon-pill pressure (no SARADC readings: every key at
 * full pressure, as fm1_panel_io.h gives a digital row, the way fm1-emu runs it), the USB audio out and its drift
 * resampler, the TRS jack, the OTA / UBOOT / Hold OCT- rescue screens, the boot guard. Version and identity:
 * src/core/Version.h's "0.7.1", FM-1_312. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "../common/version.h"
#include "exit.h"
#include "env.h"
#include "nor.h"
#include "exit_end.h"
#include "../common/panel.h"

/* ruido_glue.cpp */
void ruido_glue_boot(uint32_t master_q12);
void ruido_glue_master(uint32_t master_q12);
void ruido_glue_panel(uint32_t now_ms, uint32_t pressed, uint32_t released, uint32_t buttons, const int32_t enc_steps[7],
                      uint32_t notes, uint32_t note_edges);
void ruido_glue_midi_rt(uint8_t status, float seconds);
int ruido_glue_nvs_poll(uint32_t now_ms, uint32_t force);
void ruido_glue_frame(uint32_t elapsed_ms, uint16_t *px);
void ruido_glue_leds(uint8_t level[44]);
void ruido_glue_audio(int16_t *out, uint32_t frames);
void ruido_glue_info(char *buf, uint32_t n);

static const int8_t FM1CORE_RU_KEYMAP[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;

/* ---- Dinghy f77bf03's store (firmware/src/store.h, platform.c), as Ruido's fm1_main.c builds it (STORE_MAGIC "RUID"):
 * NBLOB one-sector blobs from BLOBS_AT, each [magic][length][crc32] then the bytes (area_save: erase, the bytes, the
 * header last); the journals (fm1_nvs_glue.h) program slots past the record without an erase, read back to check ---- */
#define FM1CORE_RU_MAGIC 0x52554944u      /* "RUID" */
#define FM1CORE_RU_BLOBS_AT 0x000F4000u
#define FM1CORE_RU_NBLOB 8u
#define FM1CORE_RU_SECTOR_MAX (4096u - 12u)
static uint32_t fm1core_ru_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
int fm1core_ru_blob_load(uint32_t i, void *p, uint32_t n)   /* sector_load: a load that fails leaves p as it was */
{
    uint32_t h[3], at;
    if (i >= FM1CORE_RU_NBLOB || n > FM1CORE_RU_SECTOR_MAX)
        return -1;
    at = FM1CORE_RU_BLOBS_AT + i * 0x1000u;
    memcpy(h, fm1nor_image + at, sizeof h);
    if (h[0] != FM1CORE_RU_MAGIC || h[1] != n || fm1core_ru_crc32(fm1nor_image + at + sizeof h, n) != h[2])
        return -1;
    memcpy(p, fm1nor_image + at + sizeof h, n);
    return 0;
}
int fm1core_ru_blob_save(uint32_t i, const void *p, uint32_t n)   /* area_save, then nvs_save_c1_safe's readback */
{
    uint32_t h[3], at;
    int rc;
    if (i >= FM1CORE_RU_NBLOB || n > FM1CORE_RU_SECTOR_MAX)
        return -1;
    at = FM1CORE_RU_BLOBS_AT + i * 0x1000u;
    h[0] = FM1CORE_RU_MAGIC;
    h[1] = n;
    h[2] = fm1core_ru_crc32((const uint8_t *)p, n);
    if ((rc = fm1nor_erase(at)) != 0)
        return rc;
    if ((rc = fm1nor_prog_span(at + sizeof h, p, n)) != 0)
        return rc;
    if ((rc = fm1nor_prog_span(at, h, sizeof h)) != 0)
        return rc;
    return memcmp(fm1nor_image + at + sizeof h, p, n) == 0 ? 0 : -5;
}
int fm1core_ru_tail_read(uint32_t sec, uint32_t off, void *dst, uint32_t n)   /* nvs_tail_read_boot */
{
    const uint32_t min_off = sec == 4u ? 0x20u : 0x100u;
    if (n == 0u || off < min_off || off + n > 0x1000u || sec >= 6u)
        return -1;
    memcpy(dst, fm1nor_image + FM1CORE_RU_BLOBS_AT + sec * 0x1000u + off, n);
    return 0;
}
int fm1core_ru_tail_write(uint32_t sec, uint32_t off, const void *src, uint32_t n)   /* nvs_tail_write_c1_safe */
{
    const uint32_t min_off = sec == 4u ? 0x20u : 0x100u, req = sec == 4u ? 16u : 128u;
    uint32_t at;
    int rc;
    if (off < min_off || n != req || (off & (req - 1u)) || off + n > 0x1000u || sec >= 6u)
        return -1;
    at = FM1CORE_RU_BLOBS_AT + sec * 0x1000u + off;
    if ((rc = fm1nor_prog_span(at, src, n)) != 0)
        return rc;
    return memcmp(fm1nor_image + at, src, n) == 0 ? 0 : -5;
}

/* ---- the panel ---- */
fm1panel_t fm1core_ru_panel;
static uint16_t fm1core_ru_screen[FM1P_LCD_W * FM1P_LCD_H];
static uint32_t fm1core_ru_ms, fm1core_ru_frame_at, fm1core_ru_prev_b, fm1core_ru_prev_k;
static int32_t fm1core_ru_pot = 1023;
static uint8_t fm1core_ru_up, fm1core_ru_first, fm1core_ru_led[44];

static uint32_t fm1core_ru_q12(int32_t pot)   /* platform.c: MASTER's quadratic master_q12, 0..4088 */
{
    const uint32_t m = (uint32_t)(pot < 0 ? 0 : pot > 1023 ? 1023 : pot);
    return (m * m) >> 8;
}
static void fm1core_ru_in(void)           /* poll_panel_inputs */
{
    fm1panel_t *p = &fm1core_ru_panel;
    const uint32_t bt = fm1p_take_button_taps(p), kt = fm1p_take_key_taps(p);
    const uint32_t b = p->buttons & 0x3FFFu, k = p->keys & 0x7FFFFFFu;
    int32_t enc[7], m;
    uint32_t i;
    for (i = 0; i < 7u; i++)
        enc[i] = fm1p_take_enc(p, i);
    ruido_glue_panel(fm1core_ru_ms, (b & ~fm1core_ru_prev_b) | bt, (fm1core_ru_prev_b & ~b) | (bt & ~b), b, enc, k,
                     (k & ~fm1core_ru_prev_k) | kt);
    fm1core_ru_prev_b = b;
    fm1core_ru_prev_k = k;
    m = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    if (m != fm1core_ru_pot) {           /* sync_master_pot_target */
        fm1core_ru_pot = m;
        ruido_glue_master(fm1core_ru_q12(m));
    }
}
static void fm1core_ru_out(void)
{
    fm1panel_t *p = &fm1core_ru_panel;
    uint8_t lv[44];
    uint32_t c, r;
    ruido_glue_leds(lv);
    for (c = 0; c < FM1P_NCOL; c++)
        for (r = 0; r < FM1P_NLINE; r++)
            p->led[c][r] = lv[c * 4u + r];
    p->led_play_green = lv[8u * 4u + 0u];
}
static void fm1core_ru_pass(void)         /* one pass of app_main's loop */
{
    const uint32_t now = fm1core_ru_ms;
    fm1core_ru_in();
    (void)ruido_glue_nvs_poll(now, 0);
    if (fm1core_ru_first || now - fm1core_ru_frame_at >= 16u) {   /* RUIDO_LCD_FRAME_PERIOD_MS */
        const uint32_t elapsed = fm1core_ru_first ? 16u : now - fm1core_ru_frame_at;
        fm1core_ru_first = 0;
        fm1core_ru_frame_at = now;
        ruido_glue_frame(elapsed, fm1core_ru_screen);
        fm1p_lcd_out_native(&fm1core_ru_panel, fm1core_ru_screen);
    }
    fm1core_ru_out();
}

/* ---- the ABI ---- */
int fm1core_ru_halted(void) { return fm1core_halted(); }
void fm1core_ru_shutdown(void) { fm1core_run_exit(); }

void fm1core_ru_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

void fm1core_ru_init(int demo)
{
    fm1panel_t *p = &fm1core_ru_panel;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_open();
    fm1p_panel_default(p);                /* Ruido's ButtonId and dinghy's ENC_* are the matrix ids */
    fm1core_ru_pot = p->master > 0 ? (p->master > 1023 ? 1023 : p->master) : 1023;
    p->master = fm1core_ru_pot;
    fm1core_ru_ms = 0;
    fm1core_ru_prev_b = fm1core_ru_prev_k = 0;
    fm1core_ru_first = 1;
    memset(fm1core_ru_led, 0, sizeof fm1core_ru_led);
    ruido_glue_boot(fm1core_ru_q12(fm1core_ru_pot));
    fm1core_ru_up = 1;
    p->ready = 1;
    fm1core_ru_pass();
}

void fm1core_ru_tick(uint32_t ms) { fm1core_ru_ms = ms; }
void fm1core_ru_frame(void)
{
    if (fm1core_halted() || !fm1core_ru_up)
        return;
    fm1core_ru_pass();
}
void fm1core_ru_idle(void) { fm1core_ru_frame(); }

void fm1core_ru_audio(int16_t *out, uint32_t frames)
{
    uint32_t f;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        int16_t *o = out + 2u * f;
        if (fm1core_halted() || !fm1core_ru_up || fm1nor_stall_take()) {   /* (an erase: prepare_for_nvs_erase) */
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        ruido_glue_audio(o, FM1P_BLOCK);
    }
    if (f < frames)
        memset(out + 2u * f, 0, (size_t)(frames - f) * 2u * sizeof *out);
}

/* ---- MIDI in: USB-MIDI packets; System Real-Time only (poll_midi_clock) ---- */
int fm1core_ru_midi_in(uint32_t pkt)
{
    const uint8_t s = (uint8_t)(pkt >> 8);
    if (fm1core_halted() || !fm1core_ru_up)
        return 1;
    if ((pkt & 15u) == 0xFu && (s == 0xF8u || s == 0xFAu || s == 0xFBu || s == 0xFCu))
        ruido_glue_midi_rt(s, (float)(fm1core_ru_ms % 64000u) * 0.001f);
    return 1;
}
int fm1core_ru_midi_out_take(uint32_t *pkt)   /* (Ruido sends no MIDI) */
{
    (void)pkt;
    return 0;
}

/* ---- diagnostics ---- */
void fm1core_ru_ui_info(char *buf, uint32_t n) { ruido_glue_info(buf, n); }
void fm1core_ru_dump(void)
{
    char b[160];
    ruido_glue_info(b, sizeof b);
    printf("ruido: %u ms; %s; master %d\n", (unsigned)fm1core_ru_ms, b, (int)fm1core_ru_pot);
    printf("  flash: %s, %u writes\n", fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty());
}
void fm1core_ru_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = 0;
}
const int8_t *fm1core_ru_keymap(void) { return &FM1CORE_RU_KEYMAP[0][0]; }

/* ---- the flash (ABI 2): common/nor.c ---- */
uint8_t *fm1core_ru_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_ru_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_ru_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_ru_flash_sync(void)           /* what is pending, now (the OTA path's forceFlush) */
{
    if (!fm1core_ru_up)
        return 1;
    return ruido_glue_nvs_poll(fm1core_ru_ms, 1);
}
