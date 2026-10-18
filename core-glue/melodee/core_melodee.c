/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core: Melodee's firmware (https://github.com/keremimo/melodee, Felucca's fork with the FM6 and CZ-1
 * engines and USB audio; the pinned, unmodified submodule cores/melodee) built as one translation unit with this
 * repo's glue, the equivalent of ChoralRoot's emulator unit:
 *   melodee_fw.c        the emu_fw_* hooks (power-on, the 1 ms tick, the UI frame, audio, MIDI), as emu_fw.c
 *   melodee_firmware.h  the include list (hostsim.c's sound side, the HAL, audio.c, the UI, storage), as
 *                       emu_firmware.h
 *   melodee_hal.h       the HAL on the host (time, input, LCD, LEDs, the NOR image), as emu_hal_fw.h
 * Its includes resolve through the target's include path (cores/melodee/build/gen, firmware/src; see
 * CMakeLists.txt fm1_add_core).
 *
 * exit() and atexit() inside a plugin, as core_choralroot.c: the core ends its "process" where the device would
 * reset into UBOOT (two failed boots in a row, OCT- + OCT+ held 5 s, the USB UBOOT SysEx: exit(3)), and registers
 * its settings flush with atexit(emu_fw_exit). Both names are redirected for this unit:
 *   exit(code)  -> fm1core_exit: runs the registered exit handlers once, closes the flash file, records the code,
 *                  sets the halted flag and RETURNS; the call sites return right after it (melodee_fw.c), and the
 *                  descriptor's wrappers turn the clock into no-ops from then on (audio: silence).
 *   atexit(fn)  -> fm1core_atexit: keeps fn for the descriptor's shutdown.
 * The flash is core-glue/common/nor.c's image (a file, the staged bytes, or RAM only); exit / atexit are
 * core-glue/common/exit.h's.
 *
 * ABI 5: the firmware HAL keeps its emu_hal (common/panel.h fm1p_emu_hal_t); the ABI's panel is fm1core_me_panel.
 * The input is copied in at the top of tick / frame / idle, the LEDs (fm1_led (and a dim plane lit every frame) = LIT, the other dim planes (melodee_hal.h emu_led_sync) = DIM, PLAY green from
 * its position) and the LCD out after init / frame / idle when they changed. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */

#include "melodee_fw.c"

#include "exit_end.h"

/* ---- the glue (hidden: reached only through the descriptor) ---- */
fm1panel_t fm1core_me_panel;
#define FM1CORE_PANEL (&fm1core_me_panel)

static void g_in(void) { FM1P_EMU_IN(FM1CORE_PANEL, &emu_hal); }
static void g_out(void)
{
    static const uint8_t LV[2] = {FM1P_LED_LIT, FM1P_LED_DIM};
    const uint8_t *const pl[2] = {emu_hal.led, emu_hal.led_dim};
    fm1p_leds_planes(FM1CORE_PANEL, pl, LV, 2, emu_hal.led_play_green);
    fm1p_lcd_out(FM1CORE_PANEL, emu_hal.lcd, emu_hal.lcd_writes);
}

int fm1core_me_halted(void) { return fm1core_halted(); }
void fm1core_me_shutdown(void) { fm1core_run_exit(); }

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

/* ---- the Tier 2 parameter map (in this unit: it reaches the firmware's statics) ---- */
#include "core_melodee_params.c"
