/* SPDX-License-Identifier: GPL-3.0-only */
/* The Yehnah core (https://github.com/Mello-D/yehnah, tag v0.1.0 = e8b59f6, the pinned, unmodified submodule
 * cores/yehnah): Mello-D's chord-harp fork of JIANT FM (itself a Felucca fork), whose browser unit,
 * web/emu/yehnah_web.c (JIANT's, with the FELUCCA_* names renamed YEHNAH_*), is driven by the family's glue
 * (fw_core.h). After the author's own glue (tools/rfm1/core_yehnah.c) and core_jiant.c, whose notes hold here too:
 *   - the session's random macros (session_dice) take the web unit's default seed, 1 (no host clock in a core);
 *   - a fifth LED plane, fm1_led_hi (a brighter steady level than mid);
 *   - MENU > RESTORE LAST is OFF by default (PREF_RESTORE_ON). */
#ifndef FM1CORE_YEHNAH_SEED
#define FM1CORE_YEHNAH_SEED 1u
#endif
#define FM1CORE_FW_P yn
#define FM1CORE_FW_WEB_C "../../cores/yehnah/web/emu/yehnah_web.c"
#define FM1CORE_FW_BEFORE_BOOT() web_seed(FM1CORE_YEHNAH_SEED)
#define FM1CORE_FW_RESTORE_ON() ((ui_prefs & PREF_RESTORE_ON) != 0)
#define FM1CORE_YEHNAH_HI_LEVEL 160u       /* fm1_led_hi: lit on a third of the mid phases, between MID and lit */
#define FM1CORE_FW_LED(c, r, lv)                                                                             \
    do {                                                                                                     \
        if (((fm1_led_hi[c] >> (r)) & 1u) && (lv) < FM1CORE_YEHNAH_HI_LEVEL)                                 \
            (lv) = FM1CORE_YEHNAH_HI_LEVEL;                                                                  \
    } while (0)
#define FM1CORE_FW_EDITOR_C "../../cores/yehnah/firmware/src/editor.c"  /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/yehnah/firmware/src/ota.c"        /* the M-UPGRADE identity */
#define YEHNAH_ID "FM-1_069"        /* tools/build.py PRODUCT (yehnah.c): ota.c's identity */
#define FELUCCA_ID YEHNAH_ID        /* (common/sysex_ota.h's name for it) */
#include "fw_core.h"
