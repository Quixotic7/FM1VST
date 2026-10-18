/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot FM-1 core: the firmware unit of the ChoralRoot emulator, compiled here as is.
 *
 * The firmware is built as ONE translation unit (emu_fw.c includes emu_firmware.h, which includes the
 * firmware's own .c files, the host DSP build and the Mac HAL), exactly as the submodule's
 * tools/emu/build.sh does. Including it from here builds it from the unmodified, pinned submodule:
 * nothing is copied or patched. Its includes resolve through the target's include path
 * (cores/ChoralRootFM1/build/gen, firmware/src, tools/emu; see core.cmake).
 *
 * exit() and atexit() inside a plugin. The emulator ends its process in two places: emu_fw.c's boot guard
 * (BOOT_UBOOT: exit(3)) and emu_firmware.h's CR_REBOOT() (Options > Flash Data in SAFE MODE: exit(0)); and it
 * registers its flash / settings save with atexit(emu_fw_exit). In a host neither may touch the process, so both
 * names are redirected for this unit only:
 *   exit(code)  -> fm1core_exit (core-glue/common/exit.h): does what exit() would do to the device's state (runs
 *                  the registered exit handlers once, closes the flash file so nothing later reaches it: cr_cleanup),
 *                  records the code, sets the halted flag and RETURNS. The caller's code then runs on to its end
 *                  (harmless: see below); the entry points below turn tick / frame / idle / audio / MIDI into no-ops
 *                  from then on (audio: silence). Powering on again = reloading the module.
 *   atexit(fn)  -> fm1core_atexit: keeps fn for fm1core_cr_shutdown (the descriptor's shutdown). Ignored once
 *                  halted (the process would be gone: nothing registered after it runs).
 * What runs on after a returning exit():
 *   - the UBOOT path (emu_boot_guard, inside init): init continues with a normal power-on; the flash path was
 *     cleared by fm1core_exit, so the boot runs on a RAM-only flash and never writes the file (the emulator exits
 *     before emu_flash_open, so the file is untouched there too).
 *   - CR_REBOOT (cu_flash_erase, inside frame's cr_ui_input): the rest of the frame runs (cr_ui_frame, the draw);
 *     it is SAFE MODE (cr_safe), so no settings are saved, and the flash file is closed anyway. No memory is
 *     touched that a returning exit could corrupt: both sites are plain statements followed by ordinary code. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#define FM1CORE_NO_EMU_NAMES              /* (the firmware's emu_hooks.h has them) */
#include "fm1core.h"
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
/* Booting from bytes (fm1core.h flash_stage). emu_hal_fw.h's emu_flash_open / emu_flash_sync do stdio on
 * emu_flash_path; when that path is FM1CORE_FLASH_STAGED the "file" is the stage instead (common/nor.h B), exactly as
 * the web build's emu_web.c does: the open "reads" the staged bytes into emu_flash[], every later write-through is
 * dropped (emu_flash[] is the truth; the host reads it with flash()). Any other path is real stdio. */
#include "nor_stdio.h"

#include "../../cores/ChoralRootFM1/tools/emu/emu_fw.c"

#include "nor_stdio_end.h"
#include "exit_end.h"
#include "panel.h"

/* ---- the glue (hidden: reached only through the descriptor) ----
 * ABI 5: the firmware keeps its own emu_hal (emu_hooks.h's emu_hal_t, as its emulator has it); the ABI's panel is
 * fm1core_cr_panel. The input is copied in at the top of tick / frame / idle (common/panel.h FM1P_EMU_IN: the
 * firmware reads emu_hal.master in its frame, so a MASTER set after a tick reaches that ms's frame, as in the
 * reference emulator), the LEDs and the LCD out after init / frame / idle when they changed: fm1_led = LIT,
 * fm1_led_dim (the glow) = DIM, PLAY green from its position. */
fm1panel_t fm1core_cr_panel;
#define CRP (&fm1core_cr_panel)

static void cr_cleanup(void)              /* exit()'s last step: the flash file closed, later writes RAM only */
{
    if (emu_flash_f) {
        fm1nor_fflush(emu_flash_f);
        fm1nor_fclose(emu_flash_f);
        emu_flash_f = NULL;
    }
    emu_flash_path[0] = 0;                /* (a later emu_flash_open: RAM only) */
}

static void cr_in(void) { FM1P_EMU_IN(CRP, &emu_hal); }
static void cr_out(void)
{
    static const uint8_t LV[2] = {FM1P_LED_LIT, FM1P_LED_DIM};
    const uint8_t *const pl[2] = {emu_hal.led, emu_hal.led_dim};
    fm1p_leds_planes(CRP, pl, LV, 2, emu_hal.led_play_green);
    fm1p_lcd_out(CRP, emu_hal.lcd, emu_hal.lcd_writes);
}

int fm1core_cr_halted(void) { return fm1core_halted(); }
void fm1core_cr_shutdown(void) { fm1core_run_exit(); }

void fm1core_cr_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_exit_cleanup = cr_cleanup;
    emu_fw_options(flash_path, no_flash, save_on_exit, headless);
}
int fm1core_cr_boot_options(int fail, const char *reason, int stage) { return emu_fw_boot_options(fail, reason, stage); }
void fm1core_cr_init(int demo)
{
    fm1core_exit_cleanup = cr_cleanup;
    FM1P_EMU_BOOT(CRP, &emu_hal);
    emu_fw_init(demo);
    FM1P_EMU_BOOTED(CRP, &emu_hal);
    cr_out();
}
void fm1core_cr_tick(uint32_t ms)
{
    if (fm1core_halted())
        return;
    cr_in();
    emu_fw_tick(ms);
}
void fm1core_cr_frame(void)
{
    if (fm1core_halted())
        return;
    cr_in();
    emu_fw_frame();
    cr_out();
}
void fm1core_cr_idle(void)
{
    if (fm1core_halted())
        return;
    cr_in();
    emu_fw_idle();
    cr_out();
}
void fm1core_cr_audio(int16_t *stereo, uint32_t frames)
{
    if (fm1core_halted())
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
    else
        emu_fw_audio(stereo, frames);
}
int fm1core_cr_midi_in(uint32_t pkt)
{
    return fm1core_halted() ? 1 : emu_fw_midi_in(pkt);   /* (halted: taken and dropped, never "retry") */
}
int fm1core_cr_midi_out_take(uint32_t *pkt) { return fm1core_halted() ? 0 : emu_fw_midi_out_take(pkt); }
void fm1core_cr_dump(void) { emu_fw_dump(); }
void fm1core_cr_stats(uint32_t *shed, uint32_t *cpu_pct) { emu_fw_stats(shed, cpu_pct); }
void fm1core_cr_ui_info(char *buf, uint32_t n) { emu_fw_ui_info(buf, n); }
const int8_t *fm1core_cr_keymap(void) { return &emu_keymap[0][0]; }

/* ---- the flash (fm1core.h ABI 2) ---- */
uint8_t *fm1core_cr_flash(void) { return emu_flash; }
uint32_t fm1core_cr_flash_dirty(void) { return emu_flash_writes; }
int fm1core_cr_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
/* make the image current: the settings record the UI would save after its quiet time (cr_settings_poll waits
 * CRS_QUIET_MS after a change and CRS_IDLE_MS of silence) is captured and written now if it differs from the one in
 * the flash (crs_write: no erase when unchanged). 1: the image is current; 0: a save is still pending (a loop is
 * playing: the firmware defers flash erases then, CR_SETTINGS_BUSY). Not in SAFE MODE (nothing is saved there),
 * not before the power-on or after a halt. */
int fm1core_cr_flash_sync(void)
{
    if (fm1core_halt || !crs_loaded || cr_safe)
        return 1;
    cr_settings_save();
    return crs_pending ? 0 : 1;
}

/* ---- the Tier 2 parameter map (in this unit: it reaches the firmware's statics) ---- */
#include "core_choralroot_params.c"
