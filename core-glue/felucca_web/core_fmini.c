/* SPDX-License-Identifier: GPL-3.0-only */
/* The FMini-1 core (https://github.com/ruppert/fmini-1, the pinned, unmodified submodule cores/fmini-1 with its
 * Felucca-Engines submodule, firmware/engines): a three-oscillator monosynth on Felucca 1.6.1 (FELUCCA_FMINI_ONLY:
 * fmini_app.c's screen and controls), whose browser unit, web/emu/felucca_web.c, is driven by the family's glue
 * (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - its 3 s splash (fa_splash): main.c's loop starts 3030 ms after power-on (web_boot_ms), so a key sounds after it
 *     (conformance_fmini.txt settle_ms);
 *   - the UI is fmini_app.c's (UI_INPUT / UI_LEDS / UI_DRAW), track 1 the instrument (fa_init);
 *   - the device's second core (FELUCCA_CPU2) is built off by the web unit, as Felucca's.
 * Version and identity as its releases build them (since 1.14 a plain ./build.sh, no --release: build.py's --release
 * takes one digit per part): FELUCCA_VERSION felucca.c's default "v1.6.1" (core_fmini.cmake), FELUCCA_ID build.py's
 * PRODUCT FM-1_900; its screen shows core.h's FMINI_VERSION. */
#define FM1CORE_FW_P fm
#define FM1CORE_FW_WEB_C "../../cores/fmini-1/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/fmini-1/firmware/src/editor.c"   /* the web editors' SysEx */
#define FM1CORE_FW_OTA_C "../../cores/fmini-1/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"                                             /* (build.py's PRODUCT, ./build.sh) */
static void fm1core_fmini_rd(void);
#define FM1CORE_FW_AFTER_FRAME() fm1core_fmini_rd()   /* (its knob row for the host, after each frame: below) */
#include "fw_core.h"

/* FMini-1 (FELUCCA_FMINI_ONLY: fmini_app.c fa_draw is the screen): the page's four knobs as fa_knob_col draws them
 * (the label f->lb, the value fmg_card_at's text, the RANGE names or fa_val's 0.0..10.0), or an FX pop-up's four
 * (fa_pop_knob). Nothing: the splash, the screen dark, ABOUT and the GLO pop-ups (fa_gl), the restore question, the
 * SEQ / SEQ settings / ARP / SAVE dialogs, TAP TEMPO, and the boxes over the values (a message, the preset pop-up, CPU). */
#include "rd_cols.h"
static void fm1core_fmini_rd(void)
{
    uint32_t i, pg = fa_page;
    rdc_begin();
    if (!web_booted || web_boot_ms || scrn.st == SCR_OFF || scrn.st == SCR_WAKE || scrn.st == SCR_SLPOUT || ui.uboot ||
        fa_gl || fb_rs == FB_RS_ASK || fb_rs == FB_RS_OK || fa_sc || fa_sq || fa_ar || fa_sv || fa_tp || fa_msg_on() ||
        fa_pp_on() || fa_cpu_on())
        return;
    for (i = 0; i < 4u; i++) {
        const char *lb, *un;
        char val[16];
        int32_t ratio;
        uint32_t ic;
        if (fmg_fx) {                                    /* fa_fx_dialog: K1..K4 are its four knobs */
            const uint32_t fp = 7u + fmg_fx;
            const fmgf_t *f = fmg_field(fp, i);
            fmg_card_at(fp, i, -1, &lb, val, &un, &ratio, &ic);
            if (f->kind == FK_PAR || (f->kind == FK_GLB && GP[f->a].fmt == F_PCT))
                fa_val(val, ratio, f);
            rdc_col(i, f->lb, val);
        } else {                                         /* fa_scene: fa_knob_col */
            const fmgf_t *f = fmg_field(pg, i);
            fmg_card_at(pg, i, -1, &lb, val, &un, &ratio, &ic);
            if (f->kind == FK_NAME && f->b == FN_RANGE)
                str_cpy(val, FMG_RANGE[clamp((int32_t)fmg_byte(f->a), 0, 5)], 16);
            else
                fa_val(val, ratio, f);
            rdc_col(i, f->lb, val);
        }
    }
}
