/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mini-Tone core (https://github.com/eldontyrellcorp/Mini-Tone, the pinned, unmodified submodule cores/Mini-Tone):
 * Felucca 1.5 with six tracks and new engines (SID, CHIP / DUALVCO / ANALOG4, BRAIDS, SWARMER / FMTONE / WAVETONE /
 * FMDRUM, TB303), whose browser unit, web/emu/felucca_web.c (Felucca 1.6.1's less its second-core stubs: Mini-Tone has
 * no second core), is driven by the family's glue (fw_core.h). The idle LED animation is there (ANIM_IDLE).
 * Version and identity as tools/build.py --release 1.0.5 builds them: FELUCCA_VERSION "v1.0.5" (core_minitone.cmake),
 * FELUCCA_ID FM-1_910 (release builds are FM-1_9XY). */
#define FM1CORE_FW_P mt
#define FM1CORE_FW_WEB_C "../../cores/Mini-Tone/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/Mini-Tone/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/Mini-Tone/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"       /* the Hub's minitone 1.0.5 package: build.py PRODUCT (not a --release build) */
#include "fw_core.h"

/* the Tier 2 map in the .rfm1 (readouts, live knob labels): Felucca's, checked against this fork (fw_map_fork.h) */
#ifdef FM1CORE_RFM1
/* Mini-Tone's differences from Felucca 1.8.2's dispatch (ui_input.c, from Felucca 1.5): SELECT steps the pages under
 * MENU > SELECT PAGES (GLO + SELECT the tempo), else the tempo (BPM LOCK: GLO held only); no SELECT modes, no EUCLID */
static int32_t fm1core_mt_select_target(void);
#define FE_SELECT_TARGET fm1core_mt_select_target()
#define FE_EUCLID_ON 0
#endif
#include "fw_map_fork.h"
#ifdef FM1CORE_RFM1
static int32_t fm1core_mt_select_target(void)
{
    const int glo = layer_held() && ui.ly == LAYER_GLO;
    if (!glo && ((ui_prefs2 & (PREF_SEL_PAGES >> 16)) || (ui_prefs & PREF_BPM_LOCK)))
        return -1;                                /* (SELECT PAGES; BPM LOCK) */
    return fe_index_of(FEK_GP, G_BPM);
}
#endif
