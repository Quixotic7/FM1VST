/* SPDX-License-Identifier: GPL-3.0-only */
/* The GHOULBOX core (https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth, the pinned, unmodified submodule
 * cores/ghoulbox-fm1-dungeon-synth): its own browser unit, web/emu/felucca_web.c (Felucca 1.6's, with GHOULBOX's
 * project capture exports), driven by the family's glue (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - its boot guard is GHOULBOX's own (bootguard_begin: a second failed boot starts the USB RESCUE, not UBOOT):
 *     not emulated (boot_options NULL);
 *   - web_lit_keys' chord overlay (the browser lights a chord's other keys) is the page's, not the device's: the
 *     panel shows fm1_led only, as the FM-1 does.
 * Randomness (DREAM / EVOL): libc.c's xorshift from a fixed seed (rng_state 0x1234567 at power-on), so a run is
 * deterministic in simulated time, as on the device after a reset. */
#define FM1CORE_FW_P gb
#define FM1CORE_FW_WEB_C "../../cores/ghoulbox-fm1-dungeon-synth/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_BOOTGUARD 0
#include "fw_core.h"
