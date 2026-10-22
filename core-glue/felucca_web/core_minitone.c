/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mini-Tone core (https://github.com/eldontyrellcorp/Mini-Tone, the pinned, unmodified submodule cores/Mini-Tone):
 * Felucca 1.5 with six tracks and new engines (SID, CHIP / DUALVCO / ANALOG4, BRAIDS, SWARMER / FMTONE / WAVETONE /
 * FMDRUM, TB303), whose browser unit, web/emu/felucca_web.c (Felucca 1.6.1's less its second-core stubs: Mini-Tone has
 * no second core), is driven by the family's glue (fw_core.h). The idle LED animation is there (ANIM_IDLE).
 * Version and identity as tools/build.py --release 1.0.1 builds them: FELUCCA_VERSION "v1.0.1" (core_minitone.cmake),
 * FELUCCA_ID FM-1_910 (release builds are FM-1_9XY). */
#define FM1CORE_FW_P mt
#define FM1CORE_FW_WEB_C "../../cores/Mini-Tone/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/Mini-Tone/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/Mini-Tone/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_910"                                               /* (build.py --release 1.0.1) */
#include "fw_core.h"
