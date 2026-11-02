/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mini-Tone core (https://github.com/FBNFRTG/drift-fm1, the pinned, unmodified submodule cores/drift-fm1):
 * Felucca 1.5 with six tracks and new engines (SID, CHIP / DUALVCO / ANALOG4, BRAIDS, SWARMER / FMTONE / WAVETONE /
 * FMDRUM, TB303), whose browser unit, web/emu/felucca_web.c (Felucca 1.6.1's less its second-core stubs: Mini-Tone has
 * no second core), is driven by the family's glue (fw_core.h). The idle LED animation is there (ANIM_IDLE).
 * Version and identity as tools/build.py --release 1.0.5 builds them: FELUCCA_VERSION "v1.0.5" (core_drift.cmake),
 * FELUCCA_ID FM-1_910 (release builds are FM-1_9XY). */
#define FM1CORE_FW_P mt
#define FM1CORE_FW_WEB_C "../../cores/drift-fm1/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/drift-fm1/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/drift-fm1/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"       /* the Hub's minitone 1.0.5 package: build.py PRODUCT (not a --release build) */
#include "fw_core.h"
