/* SPDX-License-Identifier: GPL-3.0-only */
/* The Skiff core (https://github.com/illuminus5/skiff, the pinned, unmodified submodule cores/skiff): a six-track
 * groovebox, a fork of Felucca 1.1.5 with most of 1.6.1 brought over, whose browser unit, web/emu/felucca_web.c, is
 * driven by the family's glue (fw_core.h). The FM-1 build (BOARD fm1: no new flag; the SMK-37 Pro build is another
 * board, left out). What differs from Felucca 1.6.1's unit:
 *   - the patterns' bank (bank.c: bank_boot, proj_migrate), the unit's CLOCK MIDI IN TUNE (unit_g_boot);
 *   - no idle LED animation (1.2's: not in Skiff), so ANIM_IDLE stays 0;
 *   - MENU > CPU CLOCK 320 MHZ is refused (no PLL), the second core built off, as in the browser.
 * Version and identity as tools/build.py --release 1.4 builds them: FELUCCA_VERSION "v1.4" (felucca.c's own default),
 * FELUCCA_ID FM-1_714 (Skiff's release builds are FM-1_7XY; the Hub's package). */
#define FM1CORE_FW_P sk
#define FM1CORE_FW_WEB_C "../../cores/skiff/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 0
#define FM1CORE_FW_EDITOR_C "../../cores/skiff/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/skiff/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_714"                                           /* (build.py --release 1.4) */
#include "fw_core.h"
