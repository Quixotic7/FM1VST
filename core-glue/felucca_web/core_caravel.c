/* SPDX-License-Identifier: GPL-3.0-only */
/* The Caravel core (https://github.com/bonszaimusic/caravel, the pinned, unmodified submodule cores/caravel with its
 * Felucca-Engines submodule, firmware/engines): bonszaimusic's fork of Felucca 1.6.1 (SEM, HADES, DRONE, DRUM-X,
 * LFO2, MORPH, the STASIS layer, HOME performance), whose browser unit, web/emu/felucca_web.c, is driven by the
 * family's glue (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - the profile web/emu/build.sh compiles it with (core_caravel.cmake): FELUCCA_HOME_PERF, FELUCCA_PAIR_MORPH and
 *     FELUCCA_PERF_HW on (the HOME performance and the engine pair morph, from experiments/home-perf and
 *     experiments/engine-pair-morph, which the unit includes; the performance store on the device's 1 MiB NOR, as
 *     the release's build.py profile), and the second core off (FELUCCA_CPU2=0: one core whatever the menu says);
 *   - the action banks (experiments/action-banks, CARAVEL_ACTION_BANKS) and the STASIS expansion are in, as on the
 *     device (felucca.c's defaults).
 * No Tier 2 parameter map: Caravel's pages and knob dispatch (the HOME performance, the action banks, its own menus)
 * are not Felucca's, so Felucca's map (core_felucca_params.c) is not carried over; the plugin has the panel only, as
 * the other forks.
 * Identity as its releases build it: FELUCCA_ID "FM-1_999" (tools/release_artifacts.py identity: one fixed identity for
 * every Caravel release), FELUCCA_VERSION felucca.c's "v0.13.0". */
#define FM1CORE_FW_P cv
#define FM1CORE_FW_WEB_C "../../cores/caravel/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/caravel/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/caravel/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_999"                                             /* (release_artifacts.py identity) */
#include "fw_core.h"
