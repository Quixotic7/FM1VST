/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot FM-1 core (0.25.1): the firmware unit of the ChoralRoot emulator, compiled here as is.
 *
 * The firmware is built as ONE translation unit (emu_fw.c includes emu_firmware.h, which includes the
 * firmware's own .c files, the host DSP build and the Mac HAL), exactly as the submodule's
 * tools/emu/build.sh does. Including it from here builds it from the unmodified, pinned submodule:
 * nothing is copied or patched. Its includes resolve through the target's include path
 * (cores/ChoralRootFM1/build/gen, firmware/src, tools/emu; see core_choralroot.cmake). The family's shared glue
 * (crf_glue.h) translates its emu_hal (0.25.1's emu_hal_t: velocity and lean per key, four LED levels, PA2) to and
 * from the ABI's panel.
 *
 * The version the firmware shows: build.sh's "-DFELUCCA_VERSION=\"ChoralRoot X\"" (the reference emulator's), here
 * from the version fm1_add_core read out of firmware/src/choralroot.c (emu_firmware.h's own default is "EMU").
 *
 * exit() and atexit() inside a plugin. The emulator ends its process in these places: emu_fw.c's boot guard
 * (BOOT_UBOOT: exit(3)), emu_firmware.h's CR_REBOOT() (Options > Flash Data in SAFE MODE: exit(0)), and the update
 * gesture's leaving (OCT- + OCT+ + HOME held 5 s, cr_backup.c cr_update_poll: CRB_UBOOT_GO exit(0), CRB_STUB_GO
 * exit(0) for the rescue request); it registers its flash / settings save with atexit(emu_fw_exit). In a host
 * neither may touch the process, so both names are redirected for this unit only:
 *   exit(code)  -> fm1core_exit (core-glue/common/exit.h): does what exit() would do to the device's state (runs
 *                  the registered exit handlers once, closes the flash file so nothing later reaches it: the
 *                  family's cleanup), records the code, sets the halted flag and RETURNS. The caller's code then runs
 *                  on to its end (harmless: see below); the entry points turn tick / frame / idle / audio / MIDI
 *                  into no-ops from then on (audio: silence). Powering on again = reloading the module.
 *   atexit(fn)  -> fm1core_atexit: keeps fn for the descriptor's shutdown. Ignored once halted.
 * What runs on after a returning exit():
 *   - the UBOOT path (emu_boot_guard, inside init): init continues with a normal power-on; the flash path was
 *     cleared by fm1core_exit, so the boot runs on a RAM-only flash and never writes the file.
 *   - CR_REBOOT (cu_flash_erase, inside frame's cr_ui_input): the rest of the frame runs; it is SAFE MODE (cr_safe),
 *     so no settings are saved, and the flash file is closed anyway.
 *   - the update's leaving (cr_update_poll, the last statement of its branch; crb_leave: CRB_STUB_GO then
 *     CRB_REBOOT, whose emulator stand-ins only print): plain statements followed by ordinary code, nothing is
 *     corrupted by a return.
 *
 * The environment: common/env.h (getenv is NULL inside the core: EMU_C1=thread would start a thread nobody joins). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit / getenv prototypes must not see the macros */
#include <string.h>
#define FM1CORE_NO_EMU_NAMES              /* (the firmware's emu_hooks.h has them) */
#include "fm1core.h"
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                         /* getenv -> NULL (common/env.h) */
/* Booting from bytes (fm1core.h flash_stage). emu_hal_fw.h's emu_flash_open / emu_flash_sync do stdio on
 * emu_flash_path; when that path is FM1CORE_FLASH_STAGED the "file" is the stage instead (common/nor.h B), exactly as
 * the web build's emu_web.c does: the open "reads" the staged bytes into emu_flash[], every later write-through is
 * dropped (emu_flash[] is the truth; the host reads it with flash()). Any other path is real stdio. */
#include "nor_stdio.h"

#define FELUCCA_VERSION "ChoralRoot " FM1CORE_VERSION
#include "../../cores/ChoralRootFM1/tools/emu/emu_fw.c"

#include "nor_stdio_end.h"
#include "exit_end.h"
#include "panel.h"

/* make the image current: the settings record the UI would save after its quiet time (cr_settings_poll waits
 * CRS_QUIET_MS after a change and CRS_IDLE_MS of silence) is captured and written now if it differs from the one in
 * the flash (crs_write: no erase when unchanged). 1: the image is current; 0: a save is still pending (a loop is
 * playing: the firmware defers flash erases then, CR_SETTINGS_BUSY). Not in SAFE MODE (nothing is saved there),
 * not before the power-on or after a halt. */
static int fm1core_cr_sync(void)
{
    if (fm1core_halt || !crs_loaded || cr_safe)
        return 1;
    cr_settings_save();
    return crs_pending ? 0 : 1;
}

#define FM1CORE_CRF_P cr
#define FM1CORE_CRF_VEL 1
#define FM1CORE_CRF_LEVELS 4
#define FM1CORE_CRF_PA2 1
#define FM1CORE_CRF_FLASH_SYNC fm1core_cr_sync
#include "crf_glue.h"

/* Options > Chord Keys (emu_fw_layout(): 1 FM-1, the default since 0.25.1; 0 Classic) changes the panel's labels: the
 * LFO button (FM-1: LOCK, Classic: LATCH) and the chord keys' (F3..C#4). The descriptor's button_names are the
 * default's; a host that follows the setting needs an ABI hook (the web emulator polls web_layout() each frame). */

/* ---- the Tier 2 parameter map (in this unit: it reaches the firmware's statics) ---- */
#include "core_choralroot_params.c"
