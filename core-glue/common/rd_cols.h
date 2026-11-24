/* SPDX-License-Identifier: GPL-3.0-only */
/* The four knob columns as a firmware's screen shows them, for the live labels and the readouts (docs/DYNAMIC-LABELS.md;
 * docs/RFM1.md "Readouts (rd1)"): a glue copies what the firmware itself keeps of its drawn knob row (a web mirror, a
 * screen reader's cards, a view model, a per-knob query) into this after each UI frame, and this header answers the
 * host. Included once by a core's unit, after the firmware (its feeder reads the firmware's statics).
 *
 * The glue's side, after each UI frame:
 *   rdc_begin();                         nothing known this frame (every column: not given)
 *   rdc_col(k, label, value);            KNOB k + 1's job and its value as drawn ("" label: no job there)
 *   rdc_knob(role, label, value);        the same for any knob by role (FM1P_E_SELECT .. FM1P_E_K4)
 *
 * THE LABELS: the knobs given (KNOB 1..4: the columns) = their labels (upper case, 10 characters; "" no job); a column the glue said nothing
 * about this frame: not given (the host's own). SELECT, ALGORITHM, PRESETS, MASTER and the rest: not given, unless
 * the glue sets rdc.other[] (FM1CORE_LBL_* slots) itself.
 * THE READOUTS: KNOB k turned (fm1core_rd_turn notes its column's value before the core takes the detents), then, as
 * soon as a frame draws that column with ANOTHER value under the same label (the shim asks for 8 frames): its label and
 * value. A turn that changes nothing on the screen (a value at its end, a knob with no job): no readout. */
#ifndef FM1CORE_RD_COLS_H
#define FM1CORE_RD_COLS_H
#include <stdio.h>
#include <string.h>
#include "fm1core.h"
#ifdef FM1_HOST_EXT
#include "rfm1_ext.h"
#endif

#define RDC_LEN 24
#define RDC_N (FM1P_E_K4 + 1)            /* SELECT ALGO PRESETS K1..K4, by role */
static struct {
    uint8_t known[RDC_N];                /* the knob was given this frame */
    char l[RDC_N][RDC_LEN], v[RDC_N][RDC_LEN];
    uint8_t based[RDC_N];                /* a turn's "before" is held */
    char bl[RDC_N][RDC_LEN], bv[RDC_N][RDC_LEN];
    const char *other[FM1CORE_LBL_N];    /* the glue's own labels for other slots (static strings), or 0 */
} rdc;
static void rdc_begin(void) { memset(rdc.known, 0, sizeof rdc.known); }
static void rdc_knob(int role, const char *label, const char *value)
{
    if (role < 0 || role >= RDC_N)
        return;
    snprintf(rdc.l[role], RDC_LEN, "%s", label ? label : "");
    snprintf(rdc.v[role], RDC_LEN, "%s", value ? value : "");
    rdc.known[role] = 1;
}
static void rdc_col(uint32_t k, const char *label, const char *value)
{
    if (k <= 3u)
        rdc_knob(FM1P_E_K1 + (int)k, label, value);
}

static char rdc_lbl[FM1CORE_LBL_N][FM1CORE_LBL_LEN];
static uint8_t rdc_given[FM1CORE_LBL_N];
static uint32_t rdc_epoch;
static void rdc_up(char *d, const char *s)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < FM1CORE_LBL_LEN - 1u; i++)
        d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i] < 32 || s[i] > 126 ? ' ' : s[i];
    d[i] = 0;
}
FM1CORE_EXPORT uint32_t fm1core_labels_epoch(void)
{
    char l[FM1CORE_LBL_N][FM1CORE_LBL_LEN];
    uint8_t g[FM1CORE_LBL_N];
    uint32_t k;
    memset(l, 0, sizeof l);
    memset(g, 0, sizeof g);
    for (k = 0; k < RDC_N; k++)
        if (rdc.known[k])
            rdc_up(l[FM1CORE_LBL_KNOB0 + k], rdc.l[k]), g[FM1CORE_LBL_KNOB0 + k] = 1;
    for (k = 0; k < FM1CORE_LBL_N; k++)
        if (rdc.other[k] && !g[k])
            rdc_up(l[k], rdc.other[k]), g[k] = 1;
    if (!rdc_epoch || memcmp(l, rdc_lbl, sizeof l) || memcmp(g, rdc_given, sizeof g)) {
        memcpy(rdc_lbl, l, sizeof l);
        memcpy(rdc_given, g, sizeof g);
        if (++rdc_epoch == 0)
            rdc_epoch = 1;
    }
    return rdc_epoch;
}
FM1CORE_EXPORT const char *fm1core_label(int32_t which)
{
    return rdc_epoch && which >= 0 && which < FM1CORE_LBL_N && rdc_given[which] ? rdc_lbl[which] : 0;
}

#ifdef FM1_HOST_EXT
void fm1core_rd_turn(int role)
{
    const int k = role;
    if (k < 0 || k >= RDC_N)
        return;
    rdc.based[k] = rdc.known[k] && rdc.l[k][0];
    memcpy(rdc.bl[k], rdc.l[k], RDC_LEN);
    memcpy(rdc.bv[k], rdc.v[k], RDC_LEN);
}
int fm1core_rd_knob(int role, char *title, char *value, unsigned n, int32_t *frac, unsigned *flags)
{
    const int k = role;
    *frac = -1, *flags = 0;
    if (k < 0 || k >= RDC_N || !rdc.based[k] || !rdc.known[k] || !rdc.l[k][0] || strcmp(rdc.l[k], rdc.bl[k]) ||
        !strcmp(rdc.v[k], rdc.bv[k]))
        return 0;                            /* (not drawn, another column now, or not changed yet) */
    rdc.based[k] = 0;
    snprintf(title, n, "%s", rdc.l[k]);
    snprintf(value, n, "%s", rdc.v[k]);
    return RFM1_RD_PARAM;
}
#endif
#endif
