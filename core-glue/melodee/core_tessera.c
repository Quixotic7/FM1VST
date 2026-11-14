/* SPDX-License-Identifier: GPL-3.0-only */
/* The Tessera core: Tessera's firmware (https://github.com/Rastapopolix/tessera, Theo Dainis' generative firmware,
 * grown from Melodee (Tess), a fork of Melodee; the pinned, unmodified submodule cores/tessera at v3.5.3) built as one
 * translation unit with the melodee family's glue:
 *   tessera_fw.c        the emu_fw_* hooks (power-on, the 1 ms tick, the UI frame, audio, MIDI)
 *   tessera_firmware.h  the include list (hostsim.c's sound side, the HAL, audio.c, the UI, storage, the SysEx)
 *   melodee_hal.h       the HAL on the host (the family's: time, input, LCD, LEDs, the NOR image)
 *   melodee_glue.h      the ABI entry points (the family's: fm1core_me_*)
 * exit() / atexit() are redirected as in the Melodee core (core_melodee.c): the core ends its "process" where the
 * device would reset into UBOOT (two failed boots in a row, OCT- + OCT+ held 5 s: exit(3)). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#include "tessera_fw.c"

#include "exit_end.h"

#include "melodee_glue.h"                 /* the ABI entry points (fm1core_me_*) */
