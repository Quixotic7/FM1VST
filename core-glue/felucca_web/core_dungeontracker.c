/* SPDX-License-Identifier: GPL-3.0-only */
/* The Dungeon Tracker core (https://github.com/BourbonWX/Dungeon-Tracker, the pinned, unmodified submodule
 * cores/Dungeon-Tracker with its Felucca-Engines and felucca-bsp submodules): BourbonWX's fork of Felucca 1.6.1, a
 * four-track tracker groovebox (FMKIT, FMTONE, WAVES, SID, THE PARTY) with a dungeon crawl on HOME, whose browser unit,
 * web/emu/felucca_web.c, is driven by the family's glue (fw_core.h) as Felucca 1.6.1's: the same profile (web/emu/
 * build.sh adds only FELUCCA_VERSION), the second core off, the idle LED animation in. The ADPCM engine is built off
 * (core.h FELUCCA_ADPCM 0, as on the device), so neither its LGPL DSP nor felucca_adpcm.h is compiled in.
 * No Tier 2 parameter map (its pages and tracker are not Felucca's): the panel only, as the other forks.
 * Version and identity as tools/build.py --release 1.0 builds the v1.0.0 release (dungeon-tracker-1.0.fwsc):
 * FELUCCA_VERSION "v1.0" (felucca.c's own default says v1.1), FELUCCA_ID "FM-1_915" (FM-1_9XY). */
#define FM1CORE_FW_P dt
#define FM1CORE_FW_WEB_C "../../cores/Dungeon-Tracker/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/Dungeon-Tracker/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/Dungeon-Tracker/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_915"                                                     /* (build.py --release 1.0) */
#include "fw_core.h"

/* the Tier 2 map in the .rfm1 (readouts, live knob labels): Felucca's, checked against this fork (fw_map_fork.h) */
#ifdef FM1CORE_RFM1
/* Dungeon Tracker's differences from Felucca 1.8.2's dispatch (ui_input.c): SEQ held, SELECT is the RONDO track
 * picker (tk_pick_turn); the DICE page's knobs are its own (relative) */
#define FE_SELECT_FELUCCA 1
#define FE_SELECT_TARGET (ui.ly == LAYER_SEQ && ly_down(LAYER_SEQ) ? -1 : fe_select_target())
#define FE_PAGE_OWN(pg, k) ((pg)->graph == GR_DICE)
#endif
#include "fw_map_fork.h"
