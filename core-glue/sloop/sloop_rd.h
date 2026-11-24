/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP family's live knob labels and readouts (docs/DYNAMIC-LABELS.md; docs/RFM1.md "Readouts (rd1)"), one adapter
 * for SLOOP, Clipper, KARMA, M-PYRE, ORBIT and Stallion (a unit with FM1CORE_SL_RD 1). NOT A UNIT OF ITS OWN: included
 * by sloop_fw.c after the firmware (it reaches its statics); nothing in the submodules is modified.
 *
 * WHAT IT READS: what the screen drew. Every one of these firmwares draws the page's four knob columns through
 * ui_draw.c's draw_column, which keeps each column's label, value and unit as drawn in ui.big_l / big_v / big_u (its
 * "big values" view); draw_columns runs only on a page or HOME (a layer, a hold, the REC screen, a take, the menu, the
 * song screen, the visualiser, TRACKS and the drum screens return before it). The main loop's breadcrumb
 * felucca_dbg.stage reaches 5 only once draw_columns has run, so after ui_draw (fm1core_sl_rd_drawn, sloop_fw.c's
 * frame) a stage of 5 or more means the columns are on the screen now; they are copied then. KNOB k's turn sets
 * ui.hot_col = k, ui.hot_t = 40 (the white value) when it edited that column.
 *
 * THE LABELS: KNOB 1..4 the columns' labels while the columns are on the screen ("": an empty column), else not given
 * (the host's own: the screen is something else, which this adapter does not read). The rest not given.
 * THE READOUTS (fm1core_rd_knob): KNOB k the core took, the columns drawn after it and column k hot: its label and
 * "value unit" as drawn. Elsewhere (layers, REC, the menu): none. */
#ifndef FM1CORE_SL_RD_H
#define FM1CORE_SL_RD_H
#include "rfm1_ext.h"

static struct {
    uint8_t on;                                  /* the columns were drawn by the last frame */
    uint8_t hot_col, hot_t;
    char l[4][8], v[4][10], u[4][8];
} fm1core_sl_rd;
static void fm1core_sl_rd_drawn(void)            /* (sloop_fw.c, right after ui_draw) */
{
    uint32_t c;
    fm1core_sl_rd.on = felucca_dbg.stage >= 5u;
    if (!fm1core_sl_rd.on)
        return;
    fm1core_sl_rd.hot_col = ui.hot_col, fm1core_sl_rd.hot_t = ui.hot_t;
    for (c = 0; c < 4u; c++) {
        memcpy(fm1core_sl_rd.l[c], ui.big_l[c], sizeof fm1core_sl_rd.l[c]);
        memcpy(fm1core_sl_rd.v[c], ui.big_v[c], sizeof fm1core_sl_rd.v[c]);
        memcpy(fm1core_sl_rd.u[c], ui.big_u[c], sizeof fm1core_sl_rd.u[c]);
        fm1core_sl_rd.l[c][7] = fm1core_sl_rd.v[c][9] = fm1core_sl_rd.u[c][7] = 0;
    }
}

static char fm1core_sl_lbl[4][FM1CORE_LBL_LEN];
static uint8_t fm1core_sl_lbl_on;
static uint32_t fm1core_sl_lbl_epoch;
FM1CORE_EXPORT uint32_t fm1core_labels_epoch(void)
{
    char l[4][FM1CORE_LBL_LEN];
    uint32_t c, i;
    memset(l, 0, sizeof l);
    for (c = 0; c < 4u && fm1core_sl_rd.on; c++)
        for (i = 0; fm1core_sl_rd.l[c][i] && i < FM1CORE_LBL_LEN - 1u; i++)
            l[c][i] = fm1core_sl_rd.l[c][i] >= 'a' && fm1core_sl_rd.l[c][i] <= 'z' ? (char)(fm1core_sl_rd.l[c][i] - 32) :
                      fm1core_sl_rd.l[c][i];
    if (!fm1core_sl_lbl_epoch || fm1core_sl_lbl_on != fm1core_sl_rd.on || memcmp(l, fm1core_sl_lbl, sizeof l)) {
        memcpy(fm1core_sl_lbl, l, sizeof l);
        fm1core_sl_lbl_on = fm1core_sl_rd.on;
        if (++fm1core_sl_lbl_epoch == 0)
            fm1core_sl_lbl_epoch = 1;
    }
    return fm1core_sl_lbl_epoch;
}
FM1CORE_EXPORT const char *fm1core_label(int32_t which)
{
    const int32_t k = which - (FM1CORE_LBL_KNOB0 + FM1P_E_K1);
    return fm1core_sl_lbl_epoch && fm1core_sl_lbl_on && k >= 0 && k < 4 ? fm1core_sl_lbl[k] : 0;
}

#ifdef FM1_HOST_EXT
int fm1core_rd_knob(int role, char *title, char *value, unsigned n, int32_t *frac, unsigned *flags)
{
    const int k = role - FM1P_E_K1;
    *frac = -1, *flags = 0;
    if (k < 0 || k > 3 || !fm1core_sl_rd.on || fm1core_sl_rd.hot_col != k || fm1core_sl_rd.hot_t < 38u ||
        !fm1core_sl_rd.l[k][0])
        return 0;
    snprintf(title, n, "%s", fm1core_sl_rd.l[k]);
    snprintf(value, n, fm1core_sl_rd.u[k][0] ? "%s %s" : "%s", fm1core_sl_rd.v[k], fm1core_sl_rd.u[k]);
    return RFM1_RD_PARAM;
}
#endif
#endif
