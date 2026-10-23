/* SPDX-License-Identifier: GPL-3.0-only */
/* Tonitor (https://github.com/nzimas/tonitor, the pinned submodule cores/tonitor): the core unit. The firmware
 * (tonitor_fw.c, tonitor_firmware.h, tonitor_hal.h over the unmodified submodule) built as one translation unit with
 * the glue that translates to and from the ABI's panel (core-api/fm1core.h):
 *   input    at the top of tick / frame / idle: the held keys and buttons, the taps (a press now, its release on the
 *            next tick), the encoder detents (matrix order, taken), MASTER (the pot, 0..1023)
 *   output   after init / frame / idle: the LEDs as levels (fm1_led lit = LIT, fm1_led_dim the glow = DIM; the
 *            brightest wins) and the LCD (big-endian RGB565 as sent, counted)
 *   exit()   common/exit.h: UBOOT (OCT- + OCT+ held 5 s) and the boot guard's rescue mode halt the core
 *            (halted() = 0x100 | code); shutdown runs the handlers
 *   flash    common/nor.c's image
 * Key velocity and lean: Tonitor reads none from its keys (the FM-1's have no velocity sensing; MIDI notes carry
 * theirs, and the balls set their own): key_vel / key_lean are ignored. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#include "tonitor_fw.c"

#include "exit_end.h"

/* ---- the glue (hidden: reached only through the descriptor) ---- */
fm1panel_t fm1core_tn_panel;
#define FM1CORE_TN_PANEL (&fm1core_tn_panel)
static fm1p_edges_t fm1core_tn_keys_e, fm1core_tn_btn_e;   /* (taps: a press this tick, the release the next) */

static void fm1core_tn_in(void)           /* MASTER (frame and idle read it too) */
{
    fm1core_tn_fw_master(FM1CORE_TN_PANEL->master);
}
static void fm1core_tn_out(void)
{
    fm1panel_t *p = FM1CORE_TN_PANEL;
    uint32_t c, l;
    for (c = 0; c < FM1P_NCOL; c++)
        for (l = 0; l < FM1P_NLINE; l++) {
            const uint32_t bit = 1u << (l + 1u);
            const uint8_t v = (fm1_led[c] & bit) ? FM1P_LED_LIT : (fm1_led_dim[c] & bit) ? FM1P_LED_DIM : FM1P_LED_OFF;
            if (p->led[c][l] != v)
                p->led[c][l] = v;
        }
    p->led_play_green = p->led[FM1P_PLAY_GREEN_POS >> 3][(FM1P_PLAY_GREEN_POS & 7u) - 1u];
    fm1p_lcd_out(p, fm1core_tn_lcd, fm1core_tn_lcd_writes);
}

int fm1core_tn_halted(void) { return fm1core_halted(); }
void fm1core_tn_shutdown(void)
{
    fm1core_run_exit();
    fm1core_tn_fw_free();
}

void fm1core_tn_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_exit_cleanup = fm1nor_close;  /* (exit: the flash file closed, later writes RAM only) */
    fm1core_tn_fw_options(flash_path, no_flash, save_on_exit, headless);
}
int fm1core_tn_boot_options(int fail, const char *reason, int stage)
{
    return fm1core_tn_fw_boot_options(fail, reason, stage);
}
void fm1core_tn_init(int demo)
{
    fm1panel_t *p = FM1CORE_TN_PANEL;
    fm1core_exit_cleanup = fm1nor_close;
    if (p->master)
        fm1core_tn_fw_master(p->master);
    fm1p_panel_default(p);                /* (the halted guard: still a valid table) */
    fm1core_tn_fw_init(demo);
    if (fm1core_halted())
        return;
    fm1core_tn_fw_panel_table(p->btn_id, p->enc_id, p->enc_dir);
    if (!p->master)
        p->master = fm1core_tn_master;    /* the firmware's default MASTER (fm1_adc_read's) */
    p->ready = 1;
    fm1core_tn_out();
}
void fm1core_tn_tick(uint32_t ms)
{
    fm1panel_t *p = FM1CORE_TN_PANEL;
    int32_t enc[FM1P_NENC];
    uint32_t i, keys, buttons;
    if (fm1core_halted())
        return;
    fm1core_tn_in();
    keys = fm1p_edges(&fm1core_tn_keys_e, p->keys, fm1p_take_key_taps(p), NULL, NULL);
    buttons = fm1p_edges(&fm1core_tn_btn_e, p->buttons, fm1p_take_button_taps(p), NULL, NULL);
    for (i = 0; i < FM1P_NENC; i++)
        enc[i] = fm1p_take_enc(p, i);
    fm1core_tn_fw_tick(ms, buttons, keys, enc);
}
static void fm1core_tn_run(void)          /* frame and idle: the main loop's coroutine, this device ms */
{
    if (fm1core_halted())
        return;
    fm1core_tn_in();
    fm1core_tn_fw_run();
    if (!fm1core_halted())
        fm1core_tn_out();
    else
        fm1p_lcd_out(FM1CORE_TN_PANEL, fm1core_tn_lcd, fm1core_tn_lcd_writes);   /* (UBOOT's last screen) */
}
void fm1core_tn_frame(void) { fm1core_tn_run(); }
void fm1core_tn_idle(void) { fm1core_tn_run(); }
void fm1core_tn_audio(int16_t *stereo, uint32_t frames)
{
    if (fm1core_halted())
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
    else
        fm1core_tn_fw_audio(stereo, frames);
}
int fm1core_tn_midi_in(uint32_t pkt) { return fm1core_halted() ? 1 : fm1core_tn_fw_midi_in(pkt); }
int fm1core_tn_midi_out_take(uint32_t *pkt) { return fm1core_halted() ? 0 : fm1core_tn_fw_midi_out_take(pkt); }
void fm1core_tn_dump(void) { fm1core_tn_fw_dump(); }
void fm1core_tn_stats(uint32_t *shed, uint32_t *cpu_pct) { fm1core_tn_fw_stats(shed, cpu_pct); }
void fm1core_tn_ui_info(char *buf, uint32_t n) { fm1core_tn_fw_ui_info(buf, n); }
const int8_t *fm1core_tn_keymap(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2): common/nor.c ---- */
uint8_t *fm1core_tn_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_tn_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_tn_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_tn_flash_sync(void)
{
    if (fm1core_halted() || !FM1CORE_TN_PANEL->ready)
        return 1;
    return fm1core_tn_fw_flash_sync();
}
