/* SPDX-License-Identifier: GPL-3.0-only */
/* The JIANT FM core (https://github.com/juanjiant-bit/JIANT, the pinned, unmodified submodule cores/JIANT): a Felucca
 * 1.1.5.1 fork with SLOOP's song system, whose browser unit, web/emu/felucca_web.c (Felucca 1.6's design: JIANT's
 * boot with prefs_update, the signature splash and session_dice, song_poll in the frame), is driven by the family's
 * glue (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - the session's random macros (session_dice) take the page's seed (web_seed): the core gives the web unit's own
 *     default, 1, always (FM1CORE_JIANT_SEED), so a run is deterministic (no host clock is read in a core);
 *   - a fifth LED plane, fm1_led_hi (0.6.96: a brighter steady level than mid);
 *   - MENU > RESTORE LAST is OFF by default (PREF_RESTORE_ON): no autosave until it is turned on;
 *   - six parts (NPART 6); the dual-core render is built off in the web unit (FELUCCA_DUAL_CORE 0). */
#ifndef FM1CORE_JIANT_SEED
#define FM1CORE_JIANT_SEED 1u
#endif
#define FM1CORE_FW_P ji
#define FM1CORE_FW_WEB_C "../../cores/JIANT/web/emu/felucca_web.c"
#define FM1CORE_FW_BEFORE_BOOT() web_seed(FM1CORE_JIANT_SEED)
#define FM1CORE_FW_RESTORE_ON() ((ui_prefs & PREF_RESTORE_ON) != 0)
#define FM1CORE_JIANT_HI_LEVEL 160u        /* fm1_led_hi: lit on a third of the mid phases, between MID and lit */
#define FM1CORE_FW_LED(c, r, lv)                                                                             \
    do {                                                                                                     \
        if (((fm1_led_hi[c] >> (r)) & 1u) && (lv) < FM1CORE_JIANT_HI_LEVEL)                                  \
            (lv) = FM1CORE_JIANT_HI_LEVEL;                                                                   \
    } while (0)
#include "fw_core.h"
