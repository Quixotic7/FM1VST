/* SPDX-License-Identifier: GPL-3.0-only */
/* The cursed os core (https://github.com/cursedsound-music/CS_FM1, tag v0.18.5 = 93dec8e, the pinned, unmodified
 * submodule cores/CS_FM1): cursedsound's Elektron-style groovebox, a fork of JIANT FM V10.19 (itself a Felucca fork),
 * whose browser unit, web/emu/felucca_web.c (JIANT's, with cos.c and kits.c in it), is driven by the family's glue
 * (fw_core.h). After core_jiant.c, whose notes hold here too:
 *   - the session's random macros (session_dice) take the web unit's default seed, 1 (no host clock in a core);
 *   - a fifth LED plane, fm1_led_hi (a brighter steady level than mid);
 *   - MENU > RESTORE LAST is OFF by default (PREF_RESTORE_ON). */
#ifndef FM1CORE_CSFM1_SEED
#define FM1CORE_CSFM1_SEED 1u
#endif
#define FM1CORE_FW_P cs
#define FM1CORE_FW_WEB_C "../../cores/CS_FM1/web/emu/felucca_web.c"
#define FM1CORE_FW_BEFORE_BOOT() web_seed(FM1CORE_CSFM1_SEED)
#define FM1CORE_FW_RESTORE_ON() ((ui_prefs & PREF_RESTORE_ON) != 0)
#define FM1CORE_CSFM1_HI_LEVEL 160u        /* fm1_led_hi: lit on a third of the mid phases, between MID and lit */
#define FM1CORE_FW_LED(c, r, lv)                                                                             \
    do {                                                                                                     \
        if (((fm1_led_hi[c] >> (r)) & 1u) && (lv) < FM1CORE_CSFM1_HI_LEVEL)                                  \
            (lv) = FM1CORE_CSFM1_HI_LEVEL;                                                                   \
    } while (0)
#define FM1CORE_FW_EDITOR_C "../../cores/CS_FM1/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/CS_FM1/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_420"       /* tools/build.py cos_product(): cursed os's own identity since 0.18.5 */
#include "fw_core.h"
