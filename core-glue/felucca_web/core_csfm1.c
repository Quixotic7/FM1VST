/* SPDX-License-Identifier: GPL-3.0-only */
/* The cursed os core (https://github.com/cursedsound-music/CS_FM1, tag v0.20.0 = d4415c3, the pinned, unmodified
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
static void fm1core_csfm1_rd(void);
#define FM1CORE_FW_AFTER_FRAME() fm1core_csfm1_rd()   /* (its knob row for the host, after each frame: below) */
#include "fw_core.h"

/* cursed os (COS_SLIM: cos.c draws the whole screen): the four dials under every page (cos.c cos_scene's "dials:",
 * cos_dial): the label (upper case, 7 letters) and the value text as cos_dial makes it (param_format + its special
 * cases, upper case, 7 letters; the unit is never drawn). Nothing: before the first frame (splash), the screen dark
 * (MENU > SCREEN OFF, waking), UPDATE MODE (ui.uboot), the IDLE screen (cz.idle_up). */
#include "rd_cols.h"
static void fm1core_csfm1_rd(void)
{
    uint32_t k;
    rdc_begin();
    if (!web_booted || web_boot_ms || scrn.st == SCR_OFF || scrn.st == SCR_WAKE || scrn.st == SCR_SLPOUT || ui.uboot ||
        cz.idle_up)
        return;
    for (k = 0; k < 4u; k++) {
        uint64_t held = cz.mode == CM_TRIG && !cos_on_cnd() ? cos_held() : 0u;
        const cpg_t *p = held ? cos_lk_page() : cos_page();
        int16_t *vp, lv;
        const param_desc_t *d = cos_knob(p, k, &vp);   /* (cos_cdesc: its staging cz.cc_v[3], the value the frame set) */
        char val[24], b[16];
        const char *unit = 0;
        int32_t v;
        if (!d || !vp) {
            rdc_col(k, "", "");                          /* ("-", no knob here) */
            continue;
        }
        v = *vp;
        if (held && p->kind == CK_TRK && motion_param(p->id[k & 3u])) {
            uint32_t i;
            for (i = 0; i < NSTEP && !((held >> i) & 1u); i++) {}
            v = motion_lock_get(TSEL, i, p->id[k & 3u], &lv) ? lv : motion_base_value(TSEL, p->id[k & 3u]);
        }
        str_cpy(b, d->label, sizeof b);
        cos_up(b);
        if (str_len(b) > 7u) b[7] = 0;
        param_format(d, v, val, &unit);
        if (p->id[k & 3u] == ID_BPM) fmt_int(val, v / 100);
        if (p->id[k & 3u] == ID_BFINE) { val[0] = '.'; val[1] = (char)('0' + v / 10 % 10); val[2] = (char)('0' + v % 10); val[3] = 0; }
        if (p->kind == CK_COS && p->id[k & 3u] == CC_MOCC && v < 0) str_cpy(val, "OFF", sizeof val);
        if (p->kind == CK_COS && p->id[k & 3u] == CC_DSRC)
            cos_dsrc_name(cos_dsrc_same(dx_ui_lane) ? (uint32_t)v : 0u, val);
        if (p->kind == CK_COS && p->id[k & 3u] == CC_TSND) {
            if (v > 0) cos_dsrc_name((uint32_t)v, val); else str_cpy(val, "--", 3);
        }
        if (p->kind == CK_COS && p->id[k & 3u] == CC_IDLE) {
            if (v <= 0) str_cpy(val, "OFF", sizeof val);
            else { fmt_int(val, v); str_cpy(val + str_len(val), " MIN", 5); }
        }
        cos_up(val);
        if (str_len(val) > 7u) val[7] = 0;
        rdc_col(k, b, val);
    }
}
