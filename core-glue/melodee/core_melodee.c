/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core: Melodee's firmware (https://github.com/keremimo/melodee, Felucca's fork with the Prophet, FM6,
 * CZ-1, SID and x0x 909 engines; the pinned, unmodified submodule cores/melodee at v1.5.1) built as one translation unit with this
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
 * The input is copied in at the top of tick / frame / idle; the LEDs (fm1_led and the three dim planes as levels:
 * me_leds_out), the LCD and the panel table out after init / frame / idle. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#include "melodee_fw.c"

#include "exit_end.h"

#include "melodee_glue.h"                 /* the ABI entry points (fm1core_me_*), shared with Tessera */

/* ---- the Tier 2 parameter map (in this unit: it reaches the firmware's statics) ---- */
#include "core_melodee_params.c"
