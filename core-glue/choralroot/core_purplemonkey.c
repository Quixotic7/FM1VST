/* SPDX-License-Identifier: GPL-3.0-only */
/* The PurpleMonkey FM-1 core (0.11): the firmware unit of PurpleMonkey's emulator, tools/emu/pm_emu_fw.c (the
 * emu_fw_* hooks over pm_firmware.h's ONE include list: hostsim.c with the Felucca engines off, the Mac HAL,
 * pm_engine.c, pm_out.c, audio.c, gfx.c, panel.c, pm_sound.c, pm_ui.c), compiled here as is from the unmodified,
 * pinned submodule, behind the family's shared glue (crf_glue.h).
 *
 * Its emu_hal_t is the 44453d0 base's (tools/emu/emu_hooks.h: byte-identical to ChoralRoot 44453d0's): two LED planes
 * (lit, dim), no velocity, no PA2. It is private to this unit: nothing else here includes it.
 *
 * Nothing is stored (purplemonkey.c builds the flash driver for the update only; pm_emu_fw.c's emu_fw_options keeps
 * the image in RAM whatever it is given): the image stays erased, flash_sync has nothing to do. No exit() is reached
 * from the emulator's paths (hostsim.c's _exit calls are in its own test renders, never called here) and no handler
 * is registered; exit.h still guards the unit. The environment: common/env.h (PM_EMU_MIDI, PM_RIG_DEMO read as unset).
 * The version: the hub's 0.11 (purplemonkey.c says "0.1-dev"; the emulator unit shows no version). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FM1CORE_NO_EMU_NAMES
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"                         /* getenv -> NULL (common/env.h) */
#include "nor_stdio.h"

#define FELUCCA_VERSION "PurpleMonkey " FM1CORE_VERSION
#include "../../cores/PurpleMonkeyFM1/tools/emu/pm_emu_fw.c"

#include "nor_stdio_end.h"
#include "exit_end.h"
#include "panel.h"

static int fm1core_pm_sync(void) { return 1; }   /* (nothing to save) */

#define FM1CORE_CRF_P pm
#define FM1CORE_CRF_VEL 0
#define FM1CORE_CRF_LEVELS 2
#define FM1CORE_CRF_PA2 0
#define FM1CORE_CRF_FLASH_SYNC fm1core_pm_sync
#include "crf_glue.h"
