/* SPDX-License-Identifier: GPL-3.0-only */
/* The CoralFlower core (0.2): the firmware unit of CoralFlower's emulator, tools/emu/emu_fw.c (ChoralRoot 0.22's
 * emulator with CoralFlower's instrument: emu_firmware.h's include list, cf_*.c, flora/), compiled here as is from
 * the unmodified, pinned submodule, behind the family's shared glue (crf_glue.h).
 *
 * Its emu_hal_t is the 0.22 line's (tools/emu/emu_hooks.h): four LED planes (led, led_mid, led_dim, led_faint), PA2,
 * the strip / pump hooks; no key velocity or lean, no knob_quick, no keys_classic. It differs from ChoralRoot
 * 0.25.1's and from PurpleMonkey's; each stays private to its own unit (core-api/fm1core.h includes none of them).
 *
 * exit(): emu_fw.c's boot guard (BOOT_UBOOT: exit(3)) and the update gesture's leaving (OCT- + OCT+ + HOME held 5 s:
 * cf_shim.c cr_update_poll through emu_firmware.h's CRB_STUB_GO / CRB_UBOOT_GO, exit(0)), each a plain statement
 * followed by ordinary code: common/exit.h records it and halts the core (see core_choralroot.c). Its atexit handler
 * is empty. The environment: crf_env.h (EMU_MASTER, EMU_STUB read as unset). The version: the hub's 0.2
 * (coralflower.c still says "CoralFlower 0.1"; tools/emu/build.sh even passes choralroot.c's "ChoralRoot 0.22", which
 * the splash then shows); the splash here reads "CoralFlower 0.2". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FM1CORE_NO_EMU_NAMES
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "crf_env.h"
#include "nor_stdio.h"

#define FELUCCA_VERSION "CoralFlower " FM1CORE_VERSION
#include "../../cores/CoralFlower/tools/emu/emu_fw.c"

#include "nor_stdio_end.h"
#include "exit_end.h"
#pragma push_macro("fb")                  /* (cf_fb.c's `#define fb ((uint8_t *)cv_px)` would rewrite common/panel.h's
                                           * parameter of that name) */
#undef fb
#include "panel.h"
#pragma pop_macro("fb")

/* make the image current: the settings record (cf_settings.c) the frame's settings_poll would write once the change
 * has rested CF_SET_QUIET_MS and nothing has sounded for CF_SET_SILENT_MS, written now if it differs from flash's.
 * Not in SAFE MODE, not before the power-on, not after a restore (cfs_restore_lock) or a halt. 1: current */
static int fm1core_cf_sync(void)
{
    if (fm1core_halt || cr_safe || !cu.booted || cfs_restore_lock)
        return 1;
    if (cfs_fnv(&cs, sizeof cs) == cfs_saved)
        return 1;
    return cf_settings_write();
}

#define FM1CORE_CRF_P cf
#define FM1CORE_CRF_VEL 0
#define FM1CORE_CRF_LEVELS 4
#define FM1CORE_CRF_PA2 1
#define FM1CORE_CRF_FLASH_SYNC fm1core_cf_sync
#include "crf_glue.h"
