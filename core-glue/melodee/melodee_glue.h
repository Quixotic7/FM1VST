/* SPDX-License-Identifier: GPL-3.0-only */
/* The melodee family's ABI glue (core-api/fm1core.h ABI 5), shared by the Melodee and Tessera cores: included by a
 * core's unit (core_melodee.c, core_tessera.c) after its firmware side (melodee_fw.c, tessera_fw.c), which defines
 * emu_hal, emu_keymap and the emu_fw_* hooks over melodee_hal.h. The entry points are fm1core_me_* in both cores
 * (each core is its own module: hidden symbols); the descriptor is melodee_desc.h.
 *
 * The input is copied in at the top of tick / frame / idle; the LEDs (fm1_led and the three dim planes as levels:
 * me_leds_out), the LCD and the panel table out after init / frame / idle. */
#pragma once
/* ---- the glue (hidden: reached only through the descriptor) ---- */
fm1panel_t fm1core_me_panel;
#define FM1CORE_PANEL (&fm1core_me_panel)

static void g_in(void) { FM1P_EMU_IN(FM1CORE_PANEL, &emu_hal); }
/* the LEDs as the PWM drives them (hal/fm1_led_pwm.h fm1_led_pwm_level): fm1_led full; else the brightest dim plane
 * holding the LED, at its duty 1 / (mask + 1) (mask 0xFF: dark), as a level 255 / (mask + 1): the keys' LIGHTS
 * 1/2 1/4 1/8 = 128 64 32 (MID, DIM, FAINT classes), the idle buttons' 1/4 = 64, the breathing plane sweeping
 * 255 .. 32 .. 0 .. 32 .. 255 over a second (its level read at each frame) */
static int me_leds_out(fm1panel_t *p)
{
    const uint8_t *const planes[4] = {fm1_led, fm1_led_dim[0], fm1_led_dim[1], fm1_led_dim[2]};
    const uint8_t levels[4] = {FM1P_LED_LIT, fm1p_duty_level(fm1_led_dim_mask[0]), fm1p_duty_level(fm1_led_dim_mask[1]),
                               fm1p_duty_level(fm1_led_dim_mask[2])};
    return fm1p_leds_brightest(p, planes, levels, 4);   /* (common/panel.h; Melodee has no PLAY green) */
}
static void g_out(void)
{
    uint32_t i;
    me_leds_out(FM1CORE_PANEL);
    fm1p_lcd_out(FM1CORE_PANEL, emu_hal.lcd, emu_hal.lcd_writes);
    for (i = 0; i < FM1P_NB; i++)                 /* the panel table (Calibration may have taught a new one) */
        FM1CORE_PANEL->btn_id[i] = panel.btn[i];
    for (i = 0; i < FM1P_NE - 1u; i++) {
        FM1CORE_PANEL->enc_id[i] = panel.enc[i];
        FM1CORE_PANEL->enc_dir[i] = panel.dir[i];
    }
}

int fm1core_me_halted(void) { return fm1core_halted(); }
void fm1core_me_shutdown(void)
{
    fm1core_run_exit();
    emu_fw_free();
}

void fm1core_me_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_exit_cleanup = fm1nor_close;  /* (exit: the flash file closed, later writes RAM only) */
    emu_fw_options(flash_path, no_flash, save_on_exit, headless);
}
int fm1core_me_boot_options(int fail, const char *reason, int stage) { return emu_fw_boot_options(fail, reason, stage); }
void fm1core_me_init(int demo)
{
    fm1core_exit_cleanup = fm1nor_close;
    FM1P_EMU_BOOT(FM1CORE_PANEL, &emu_hal);
    emu_fw_init(demo);
    FM1P_EMU_BOOTED(FM1CORE_PANEL, &emu_hal);
    g_out();
}
void fm1core_me_tick(uint32_t ms)
{
    if (fm1core_halted())
        return;
    g_in();
    emu_fw_tick(ms);
}
void fm1core_me_frame(void)
{
    if (fm1core_halted())
        return;
    g_in();
    emu_fw_frame();
    g_out();
}
void fm1core_me_idle(void)
{
    if (fm1core_halted())
        return;
    g_in();
    emu_fw_idle();
    g_out();
}
void fm1core_me_audio(int16_t *stereo, uint32_t frames)
{
    if (fm1core_halted())
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
    else
        emu_fw_audio(stereo, frames);
}
int fm1core_me_midi_in(uint32_t pkt)
{
    return fm1core_halted() ? 1 : emu_fw_midi_in(pkt);   /* (halted: taken and dropped, never "retry") */
}
int fm1core_me_midi_out_take(uint32_t *pkt) { return fm1core_halted() ? 0 : emu_fw_midi_out_take(pkt); }
void fm1core_me_dump(void) { emu_fw_dump(); }
void fm1core_me_stats(uint32_t *shed, uint32_t *cpu_pct) { emu_fw_stats(shed, cpu_pct); }
void fm1core_me_ui_info(char *buf, uint32_t n) { emu_fw_ui_info(buf, n); }
const int8_t *fm1core_me_keymap(void) { return &emu_keymap[0][0]; }

/* ---- the flash (fm1core.h ABI 2): common/nor.c ---- */
uint8_t *fm1core_me_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_me_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_me_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
/* make the image current: what settings_poll would save (the settings record: palette, panel table, LEDS, HOLD,
 * favourites, ..) is written now if it differs from the record in the flash (no erase when unchanged). Projects
 * and user presets are written by the firmware the moment they are saved (SAVE), so they are current already;
 * the song being edited is RAM on the device as well (lost at power-off unless SAVEd). 1: current; 0: the
 * firmware defers the write (it never erases while the transport runs: settings_poll's transport_busy) */
int fm1core_me_flash_sync(void)
{
    if (fm1core_halted() || !emu_hal.ready || !flash_ok)
        return 1;
    persist_pending = 1;
    settings_poll();
    return persist_pending ? 0 : 1;
}
