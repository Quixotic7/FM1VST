/* SPDX-License-Identifier: GPL-3.0-only */
/* The Golana core (https://github.com/Gadlele/Golana, the pinned, unmodified submodule cores/Golana): Gadlele's
 * single analog-style synth laid out like the microKORG (two oscillators, five filters incl. MS20, two EGs, two LFOs, a
 * patch bay, an arpeggiator and a 16-step sequencer, 32 programs: eng_golana.c, golana_ui.c, golana_prog.c ..), a fork
 * of Felucca by Leo Kuroshita (Hügelton Instruments). Its browser unit, web/emu/felucca_web.c (its "Try" page's), is
 * driven by the family's glue (fw_core.h). The idle LED animation is there (ANIM_IDLE).
 * Version and identity as tools/build.py --release 1.3 builds them (RELEASING.md; the release's Golana-1.3.fwsc):
 * FELUCCA_VERSION "v1.3" (core_golana.cmake, felucca.c's own default too), FELUCCA_ID FM-1_913 (release builds are
 * FM-1_9XY; felucca.c's FM-1_900 is the plain, non-release build's). */
#define FM1CORE_FW_P gn
#define FM1CORE_FW_WEB_C "../../cores/Golana/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/Golana/firmware/src/editor.c"   /* the installer's INFO / backup SysEx */
#define FM1CORE_FW_OTA_C "../../cores/Golana/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_913"       /* build.py --release 1.3: FM-1_9 + X + Y */
static void fm1core_golana_rd(void);
#define FM1CORE_FW_AFTER_FRAME() fm1core_golana_rd()   /* (its knob row for the host, after each frame: below) */
#include "fw_core.h"

/* GOLANA's knob cards as gu_draw_card draws them (firmware/src/golana_ui.c:592): the card's gu_par_t from gu_pk (the
 * page's, or METAL's / the OSC 1 wave's CTRL swaps), its label, its value text from gu_text (GV_NONE: an empty card).
 * Nothing given: before the loop (the splash, !gu.ready), UBOOT (ui.uboot: gu_draw_uboot), WRITE (gu.save:
 * gu_draw_save, the knobs pick the slot / letters). Golana's own UI (GOLANA 1): Felucca's ui_draw is not run. */
#include "rd_cols.h"
static void fm1core_golana_rd(void)
{
    uint32_t k;
    rdc_begin();
    if (!web_booted || web_boot_ms || !gu.ready || ui.uboot || gu.save)
        return;
    for (k = 0; k < 4u; k++) {
        const gu_par_t *q = gu_pk(&GU_PAGES[gu.page], k);
        char b[16];
        if (q->id == GV_NONE) {
            rdc_col(k, "", "");
            continue;
        }
        gu_text(q, b);
        rdc_col(k, q->label ? q->label : "", b);
    }
}
