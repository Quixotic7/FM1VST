/* SPDX-License-Identifier: GPL-3.0-only */
/* The GR!MM family's live labels and readouts (docs/DYNAMIC-LABELS.md; docs/RFM1.md "Readouts (rd1)"): GR!MM 2, Glitch
 * and Arctic Scream keep GR!MM's control map, read here as the screen reads it. NOT A UNIT OF ITS OWN: included by
 * core_grimm.c / core_glitch.c / core_arcticscream.c after the firmware (it reaches its statics); nothing in the
 * submodules is modified.
 *
 * WHAT IT READS: `page` (the page showing), PAGES[] (its name, its button: a button's tap opens next_page(b)),
 * knob_name / knob_param / knob_min / knob_max / knob_text (what KNOB k + 1 turns on a page and its value as the screen
 * shows it), `sel` (the track, ALGORITHM), seq_bpm / ck_ext (the tempo), hd_msg (the header's word: PRESETS' sound),
 * fm1_in.buttons (SEQ / SEL held).
 *
 * THE LABELS (fm1core_label): SELECT, ALGO, PRESETS and K1..K4 as the main loop dispatches them now (grimm.c's
 * ENC_SELECT / ENC_ALGORITHM / ENC_PRESETS and KNOB 1..4 blocks): "" where a knob does nothing; the page buttons by the
 * page a tap opens; MASTER, PLAY, REC, OCT-/+ and the keys NULL (the host's own).
 * THE READOUTS (fm1core_rd_knob, rfm1_ext.h): a knob the core took is read after the frame: its name and knob_text,
 * the bar from knob_min .. knob_max; ALGO the track; SELECT the tempo, the step or the steps; PRESETS the sound's name
 * (hd_msg, what presets_turn said) or the tempo on SEQ's pages. SEL held on SEQ's pages (the step's locks): no
 * readout (the lock's text is drawn, not kept).
 *
 * Options (before the include): FM1CORE_GRM_MIX1_TILE 1: MIXER 1's SELECT picks the tile KNOB 1 sets (mx_sq, MX_NAME:
 * GR!MM 2, Glitch). FM1CORE_GRM_KT_PAGES: the pages knob_text has words for (a mask of PG_ bits; default GR!MM 2's);
 * on the others (GRID, PATTERN, SYSTEM, the slots, FM, PHASE) the value is ui.c's slab (ui_slab_make), as drawn. */
#ifndef FM1CORE_GRM_RD_H
#define FM1CORE_GRM_RD_H
#include <stdio.h>
#include <string.h>
#include "fm1core.h"
#include "rfm1_ext.h"

#ifndef FM1CORE_GRM_MIX1_TILE
#define FM1CORE_GRM_MIX1_TILE 0
#endif
#define FM1CORE_GRM_PG(p) (1ull << (p))
#ifndef FM1CORE_GRM_KT_PAGES                     /* the pages knob_text words (GR!MM 2's switch, SPACE its default) */
#define FM1CORE_GRM_KT_PAGES \
    (FM1CORE_GRM_PG(PG_SOUND) | FM1CORE_GRM_PG(PG_SOURCE) | FM1CORE_GRM_PG(PG_ENV) | FM1CORE_GRM_PG(PG_LFO) | \
     FM1CORE_GRM_PG(PG_SCALE) | FM1CORE_GRM_PG(PG_CHORD) | FM1CORE_GRM_PG(PG_SONG) | FM1CORE_GRM_PG(PG_HOME) | \
     FM1CORE_GRM_PG(PG_MIX1) | FM1CORE_GRM_PG(PG_MIX2) | FM1CORE_GRM_PG(PG_ARP) | FM1CORE_GRM_PG(PG_TAPE) | \
     FM1CORE_GRM_PG(PG_GRAIN) | FM1CORE_GRM_PG(PG_DELAY) | FM1CORE_GRM_PG(PG_SPACE) | FM1CORE_GRM_PG(PG_MASTER) | \
     FM1CORE_GRM_PG(PG_DRIVE))
#endif

/* GR!MM's button ids (platform.c BTN_) in FM1P_B_* order */
static const uint8_t fm1core_grm_btn[FM1P_NB] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME,
                                                 BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC, BTN_OCT_DOWN, BTN_OCT_UP};
static int fm1core_grm_held(uint32_t b) { return (int)(fm1_in.buttons >> b & 1u); }
static int fm1core_grm_seqpg(void) { return PAGES[page].btn == BTN_SEQ; }
static void fm1core_grm_up(char *d, const char *s)   /* a label: upper case, 10 characters */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < FM1CORE_LBL_LEN - 1u; i++)
        d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}

/* ---- the live labels */
static char fm1core_grm_lbl[FM1CORE_LBL_N][FM1CORE_LBL_LEN];
static uint8_t fm1core_grm_given[FM1CORE_LBL_N];     /* 0: NULL (the host's own) */
static uint32_t fm1core_grm_epoch;
static void fm1core_grm_labels(char (*l)[FM1CORE_LBL_LEN], uint8_t *g)
{
    uint32_t k, b;
    const char *s;
    memset(l, 0, sizeof fm1core_grm_lbl);
    memset(g, 0, sizeof fm1core_grm_given);
    /* SELECT: SEQ held the track's STEPS; SEQ's pages the step; HOME the tempo (not a MIDI clock's); MIXER 1 the tile */
    s = fm1core_grm_held(BTN_SEQ) ? "STEPS" : fm1core_grm_seqpg() ? "STEP" : page == PG_HOME && !ck_ext ? "TEMPO" :
        FM1CORE_GRM_MIX1_TILE && page == PG_MIX1 ? "TILE" : "";
    fm1core_grm_up(l[FM1CORE_LBL_KNOB0 + FM1P_E_SELECT], s), g[FM1CORE_LBL_KNOB0 + FM1P_E_SELECT] = 1;
    fm1core_grm_up(l[FM1CORE_LBL_KNOB0 + FM1P_E_ALGO], "TRACK"), g[FM1CORE_LBL_KNOB0 + FM1P_E_ALGO] = 1;
    s = fm1core_grm_seqpg() ? (ck_ext ? "" : "TEMPO") : "SOUNDS";
    fm1core_grm_up(l[FM1CORE_LBL_KNOB0 + FM1P_E_PRESETS], s), g[FM1CORE_LBL_KNOB0 + FM1P_E_PRESETS] = 1;
    for (k = 0; k < 4u; k++) {                   /* KNOB 1..4: the page's (SEL held on SEQ's pages: the step's locks) */
        const int lock = fm1core_grm_held(BTN_SEL) && fm1core_grm_seqpg();
        fm1core_grm_up(l[FM1CORE_LBL_KNOB0 + FM1P_E_K1 + k], lock ? "LOCK" : knob_param(page, k) ? knob_name(page, k) : "");
        g[FM1CORE_LBL_KNOB0 + FM1P_E_K1 + k] = 1;
    }
    for (b = 0; b < FM1P_NB; b++) {              /* a page button: the page its tap opens */
        const uint32_t id = fm1core_grm_btn[b];
        uint32_t p, has = 0;
        for (p = 0; p < NPAGE && !has; p++)
            has = PAGES[p].btn == id && PAGES[p].cyc;
        if (has && (p = next_page(id)) < NPAGE) {
            fm1core_grm_up(l[FM1CORE_LBL_BTN0 + b], PAGES[p].name);
            g[FM1CORE_LBL_BTN0 + b] = 1;
        }
    }
}
FM1CORE_EXPORT uint32_t fm1core_labels_epoch(void)
{
    static char l[FM1CORE_LBL_N][FM1CORE_LBL_LEN];
    static uint8_t g[FM1CORE_LBL_N];
    if (!fm1core_dg_running())
        return fm1core_grm_epoch;
    fm1core_grm_labels(l, g);
    if (!fm1core_grm_epoch || memcmp(l, fm1core_grm_lbl, sizeof l) || memcmp(g, fm1core_grm_given, sizeof g)) {
        memcpy(fm1core_grm_lbl, l, sizeof l);
        memcpy(fm1core_grm_given, g, sizeof g);
        if (++fm1core_grm_epoch == 0)
            fm1core_grm_epoch = 1;
    }
    return fm1core_grm_epoch;
}
FM1CORE_EXPORT const char *fm1core_label(int32_t which)
{
    return fm1core_grm_epoch && which >= 0 && which < FM1CORE_LBL_N && fm1core_grm_given[which] ?
           fm1core_grm_lbl[which] : 0;
}

/* ---- the readouts (rd1: the shim asks after a frame, for each knob the core took) */
#ifdef FM1_HOST_EXT
int fm1core_rd_knob(int role, char *title, char *value, unsigned n, int32_t *frac, unsigned *flags)
{
    const int32_t bpm = clamp(seq_bpm, 20, 300);
    *flags = 0;
    if (!fm1core_dg_running() || n < 12u)
        return 0;
    switch (role) {
    case FM1P_E_ALGO:
        snprintf(title, n, "TRACK"), snprintf(value, n, "T%u", (unsigned)sel + 1u);
        *frac = RFM1_FRAC(sel, 0, NT - 1);
        return RFM1_RD_PARAM;
    case FM1P_E_SELECT:
        if (fm1core_grm_held(BTN_SEQ)) {
            snprintf(title, n, "STEPS"), snprintf(value, n, "%d", (int)seq[sel].steps);
            *frac = RFM1_FRAC(seq[sel].steps, 1, SEQ_N);
        } else if (fm1core_grm_seqpg()) {
            snprintf(title, n, "STEP"), snprintf(value, n, "%d", (int)seq[sel].cursor + 1);
            *frac = RFM1_FRAC(seq[sel].cursor, 0, SEQ_N - 1);
        } else if (page == PG_HOME && !ck_ext) {
            snprintf(title, n, "TEMPO"), snprintf(value, n, "%d BPM", (int)bpm);
            *frac = RFM1_FRAC(bpm, 20, 300);
#if FM1CORE_GRM_MIX1_TILE
        } else if (page == PG_MIX1) {
            snprintf(title, n, "TILE"), snprintf(value, n, "%s", MX_NAME[clamp(mx_sq, 0, MX_N - 1)]);
            *frac = RFM1_FRAC(clamp(mx_sq, 0, MX_N - 1), 0, MX_N - 1);
#endif
        } else
            return 0;
        return RFM1_RD_PARAM;
    case FM1P_E_PRESETS:
        if (fm1core_grm_seqpg()) {
            if (ck_ext)
                return 0;
            snprintf(title, n, "TEMPO"), snprintf(value, n, "%d BPM", (int)bpm);
            *frac = RFM1_FRAC(bpm, 20, 300);
            return RFM1_RD_PARAM;
        }
        snprintf(title, n, "SOUND"), snprintf(value, n, "%s", hd_msg);   /* (presets_turn's hd_say: the sound) */
        *frac = -1;
        return value[0] ? RFM1_RD_PRESET : 0;
    default: {
        const uint32_t k = (uint32_t)(role - FM1P_E_K1);
        volatile int32_t *p;
        int32_t lo, hi;
        char t[64];
        if (role < FM1P_E_K1 || role > FM1P_E_K4 || (fm1core_grm_held(BTN_SEL) && fm1core_grm_seqpg()) ||
            !(p = knob_param(page, k)))
            return 0;
        lo = knob_min(page, k), hi = knob_max(page, k);
        t[0] = 0;
        if (FM1CORE_GRM_KT_PAGES >> page & 1u)  /* knob_text's words ("7 ms", "81%") where it has that page */
            knob_text(page, k, t);
        else {                                   /* else the slab's value, as ui.c draws it ("C4", "ON", "75") */
            slab_t S;
            memset(&S, 0, sizeof S);
            ui_slab_make(k, &S);
            snprintf(t, sizeof t, "%s", S.val);
        }
        snprintf(title, n, "%s", knob_name(page, k)), snprintf(value, n, "%s", t);
        *frac = hi > lo ? RFM1_FRAC(clamp(*p, lo, hi), lo, hi) : -1;
        *flags = lo < 0 && hi > 0 ? RFM1_RD_BIPOLAR : 0u;
        return RFM1_RD_PARAM;
    }
    }
}
#endif
#endif
