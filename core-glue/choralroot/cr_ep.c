/* SPDX-License-Identifier: GPL-3.0-only */
/* ChoralRoot's engine parameters (ep1: engine-params/SPEC.md, INVENTORY.md 1; docs/RFM1.md "Engine parameters (ep1)").
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_choralroot.c includes it after the Tier 2 map, only in an .rfm1 built with
 * WANTS ep1 (FM1_HOST_EP1), so the editor's statics (cr_edit.c ce_scr, ce_label, ce_text, ce_param, ce_send_ref; cr_ui.c
 * cu_send / cu_send_set / cu_edited; the engines' deep pages) are in scope. Nothing in the submodule is modified, and the
 * native core and the FM-1's image do not have these lines.
 *
 * THE DESCRIPTION is made once, at the first fm1core_ep() (after init), from the same walk the editor and its MIDI CC
 * blocks use: for each engine in ENGINE_ORDER, each group OSC FILT ENV LFO MOD, each screen (ce_scr), its lanes of four
 * cells. Lanes become Move pages of eight:
 *   - a stack's page and its "+" page are one page ("OSC 2" = OSC 2 on knobs 1-4, OSC 2+ on 5-8);
 *   - another stack: two instances side by side ("LFO 1/2", "MOD 1/2": the first on knobs 1-4, the second on 5-8; their
 *     names carry the instance, "Rate 1", "Rate 2");
 *   - an edit8 screen's two lanes (A on 1-4, B on 5-8), a page of its own (1-4), "LFO SYN" (its four columns, 1-4);
 *   - an engine without deep pages (WHEEL): EDIT 1 + EDIT 2 as one page, ENV + LFO as one, the MOD routes;
 *   - the mixer screen is left out (its LEVELs are on the OSC pages), the EXPR screen is the shared EXPR page;
 *   - then, on every engine, the shared EXPR (VEL dest, VEL amt, AT dest, AT amt), FX (the sends) and MIX (Level Pan
 *     Voice Glide | Transpose Detune Priority Gl. mode) pages, one pid each across engines.
 * Names: the editor's long labels (ce_label); values: the firmware's own text (ce_text) for every value, fitted to an
 * ep1 format (INT, FIXED, PERCENT) when that format gives the same words for every value, else a TABLE of the words
 * (shared between parameters with the same words).
 *
 * THE IDS are frozen in cr_ep_ids.h, by key: "<engine id>/<deep page title>/<column label>" for an engine's own cell,
 * "*\/TRK/<label>" for a part parameter several engines share, "*\/EXPR/<VEL|AT> <DEST|AMT>". A cell whose key the table
 * lacks gets no pid (its knob stays empty) and the ctest rfm1_ep_choralroot fails until the table is regenerated
 * (append-only: tests/cr_ep_test --gen).
 *
 * WRITES do what the editor's knob does for that cell (cr_edit.c ce_knob, the same three ways ce_cc_in takes): a deep
 * column through the engine's set (an F_ENUM past its aliases as the knob steps: params.c enum_step), a send through
 * cu_send_set, a part parameter written with the IRQ off; the sound marked edited when it changed. Without ce_knob's
 * hot cell, trace, CC Out or popup: the shim sets rfm1_ep_host and drops readouts. An EXPR slot not yet set takes its
 * defaults first, as ce_knob does. READS: ce_param's value (a send: the part's one value). */
#include "rfm1_ep.h"
#include "cr_ep_ids.h"

#define CEP_NENG NENG_SHOWN
#define CEP_MAXPG 32                   /* Move pages a engine (FM6: 23 with the shared ones) */
#define CEP_MAXP 1024                  /* parameters */
#define CEP_POOL (160u * 1024u)        /* the value words */
#define CEP_PTRS (24u * 1024u)

typedef struct { uint8_t g, scr, ln, col; } cep_at_t;          /* the editor's place of a cell (its knob) */
typedef struct {
    uint8_t eng;                       /* the engine's index (cep_eng), 0xFF a shared part parameter */
    uint8_t expr;                      /* 1: an EXPR cell (its slot's source at r.a - 1 or - 2) */
    uint8_t dyn;
    ce_ref_t r;
} cep_x_t;
static rfm1_ep_param_t cep_par[CEP_MAXP];
static cep_x_t cep_x[CEP_MAXP];
static char cep_pname[CEP_MAXP][11];
static uint16_t cep_np;
static rfm1_ep_page_t cep_pg[CEP_NENG][CEP_MAXPG];
static char cep_pgname[CEP_NENG][CEP_MAXPG][11];
static cep_at_t cep_at[CEP_NENG][CEP_MAXPG][8];   /* (the ctest's: where the editor's knob reaches each cell) */
static rfm1_ep_engine_t cep_eng[CEP_NENG];
static char cep_pool[CEP_POOL];
static const char *cep_ptr[CEP_PTRS];
static uint32_t cep_npool, cep_nptr;
static uint16_t cep_missing;           /* cells whose key cr_ep_ids.h lacks (the ctest fails) */
static const char *const CEP_LAYERS[2] = {"Chord", "Bass"};
static rfm1_ep_src_t cep_src;
static int cep_made;

static track_t *cep_trk(int layer) { return &trk[layer ? CR_PART_BASS : CR_PART_CHORD]; }
static void cep_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    while (s && s[i] && i + 1 < n) {
        d[i] = s[i] >= 32 && s[i] < 127 ? s[i] : (s[i] == '\267' ? '.' : ' ');
        i++;
    }
    d[i] = 0;
}
/* a name in 10 characters: spaces before a digit dropped first ("PITCH 1 1-4" -> "PITCH1 1-4"), then cut */
static void cep_fit(char *d, const char *s)
{
    char b[32];
    uint32_t i, j;
    cep_cpy(b, s, sizeof b);
    for (i = 0; str_len(b) > 10u && b[i]; i++)
        if (b[i] == ' ' && b[i + 1] >= '0' && b[i + 1] <= '9' && i)
            for (j = i; b[j]; j++)
                b[j] = b[j + 1];
    cep_cpy(d, b, 11);
}

/* ------------------------------------------------------------------------------------------- the formats --- */
/* a value's words as ep1 shows a format (SPEC.md 3 "Showing a value"): the host draws them so */
static void cep_show(uint8_t fmt, uint8_t arg, uint8_t flags, const char *unit, int32_t mn, int32_t mx, int32_t v,
                     char *o, uint32_t n)
{
    char b[24];
    uint32_t k = 0;
    int32_t a;
    if (fmt == RFM1_EP_ONOFF) {
        cep_cpy(o, v == mn ? "Off" : "On", n);
        return;
    }
    if (fmt == RFM1_EP_PERCENT) {
        int32_t p, r;
        if (flags & RFM1_EPF_BIPOLAR) {
            r = -mn > mx ? -mn : mx;
            p = v >= 0 ? (v * 100 + r / 2) / r : -((-v * 100 + r / 2) / r);
        } else
            p = ((v - mn) * 100 + (mx - mn) / 2) / (mx - mn);
        if ((flags & RFM1_EPF_BIPOLAR) && p > 0)
            b[k++] = '+';
        else if (p < 0)
            b[k++] = '-', p = -p;
        b[k] = 0;
        cu_int(b + k, p, 0, sizeof b - k);
        cu_cat(b, "%", sizeof b);
        cep_cpy(o, b, n);
        return;
    }
    if (v < 0)
        b[k++] = '-';
    else if ((flags & RFM1_EPF_BIPOLAR) && v > 0)
        b[k++] = '+';
    a = v < 0 ? -v : v;
    b[k] = 0;
    if (fmt == RFM1_EP_FIXED && arg >= 1 && arg <= 4) {
        int32_t p = arg == 1 ? 10 : arg == 2 ? 100 : arg == 3 ? 1000 : 10000, f = a % p, i;
        char d[8];
        cu_int(b + k, a / p, 0, sizeof b - k);
        cu_cat(b, ".", sizeof b);
        for (i = arg - 1; i >= 0; i--, f /= 10)
            d[i] = (char)('0' + f % 10);
        d[arg] = 0;
        cu_cat(b, d, sizeof b);
    } else
        cu_int(b + k, a, 0, sizeof b - k);
    if (unit && unit[0]) {
        if (!(unit[0] == '%' && !unit[1]))
            cu_cat(b, " ", sizeof b);
        cu_cat(b, unit, sizeof b);
    }
    cep_cpy(o, b, n);
}
/* the firmware's words for value v of a cell (the editor's cell text, ce_text), 10 characters */
static void cep_words(const track_t *t, ce_ref_t r, const param_desc_t *d, int32_t v, char *o)
{
    char b[24];
    ce_text(t, r, d, v, b, sizeof b);
    cep_cpy(o, b, 11);
}
/* the format that gives the firmware's words for every value; else a TABLE of them (up to 512 values) */
static void cep_format(rfm1_ep_param_t *q, const track_t *t, ce_ref_t r, const param_desc_t *d)
{
    static char w[4096][11];
    char u[8], x[11];
    const int32_t n = q->max - q->min + 1;
    int32_t v, c, i;
    const char *s;
    struct { uint8_t fmt, arg, flags; } C[8];
    int nc = 0;
    const uint8_t bip = q->min < 0 ? RFM1_EPF_BIPOLAR : 0;
    for (v = 0; v < n && v < 4096; v++)
        cep_words(t, r, d, q->min + v, w[v]);
    /* the unit: what follows the number at the top value ("420 ms" -> "ms", "46%" -> "%") */
    for (s = w[n - 1 < 4095 ? n - 1 : 4095]; *s && (*s == '+' || *s == '-' || *s == '.' || (*s >= '0' && *s <= '9')); s++)
        ;
    while (*s == ' ')
        s++;
    cep_cpy(u, s, 5);
    C[nc].fmt = RFM1_EP_INT, C[nc].arg = 0, C[nc++].flags = bip;
    C[nc].fmt = RFM1_EP_INT, C[nc].arg = 0, C[nc++].flags = 0;
    C[nc].fmt = RFM1_EP_FIXED, C[nc].arg = 2, C[nc++].flags = bip;
    C[nc].fmt = RFM1_EP_FIXED, C[nc].arg = 1, C[nc++].flags = bip;
    C[nc].fmt = RFM1_EP_PERCENT, C[nc].arg = 0, C[nc++].flags = bip;
    C[nc].fmt = RFM1_EP_PERCENT, C[nc].arg = 0, C[nc++].flags = 0;
    for (c = 0; c < nc; c++)
        for (i = 0; i < 2; i++) {
            const char *un = i ? u : "";
            if (C[c].fmt == RFM1_EP_PERCENT && i)
                continue;
            if (i && !u[0])
                continue;
            for (v = 0; v < n; v += n > 4096 ? 7 : 1) {   /* (a range past 4096: every 7th value compared) */
                int32_t vv = q->min + v;
                if (n > 4096) {
                    char b[24];
                    ce_text(t, r, d, vv, b, sizeof b);
                    cep_cpy(x, b, 11);
                } else
                    cep_cpy(x, w[v], 11);
                {
                    char y[11];
                    cep_show(C[c].fmt, C[c].arg, C[c].flags, un, q->min, q->max, vv, y, 11);
                    if (!cu_eq(x, y))
                        break;
                }
            }
            if (v >= n) {
                q->fmt = C[c].fmt, q->arg = C[c].arg;
                q->flags = (uint8_t)((q->flags & ~RFM1_EPF_BIPOLAR) | C[c].flags);
                q->unit = 0;
                if (un[0]) {               /* (the unit kept in the pool) */
                    char *p = cep_pool + cep_npool;
                    if (cep_npool + 6u <= CEP_POOL) {
                        cep_cpy(p, un, 5);
                        cep_npool += 6u;
                        q->unit = p;
                    }
                }
                return;
            }
        }
    q->flags = (uint8_t)((q->flags & ~RFM1_EPF_BIPOLAR) | bip);
    if (n > 512) {                     /* (too many words: the nearest number, the hundredths of F_FIX2) */
        q->fmt = d->fmt == F_FIX2 ? RFM1_EP_FIXED : RFM1_EP_INT;
        q->arg = d->fmt == F_FIX2 ? 2 : 0;
        return;
    }
    {   /* a TABLE: the same words as a table made before share it */
        uint32_t k;
        for (k = 0; k < cep_np; k++) {
            const rfm1_ep_param_t *o = &cep_par[k];
            if (o->fmt == RFM1_EP_TABLE && o->names && o->min == q->min && o->max == q->max) {
                for (v = 0; v < n && cu_eq(o->names[v], w[v]); v++)
                    ;
                if (v == n) {
                    q->fmt = RFM1_EP_TABLE, q->names = o->names;
                    return;
                }
            }
        }
        if (cep_nptr + (uint32_t)n > CEP_PTRS)
            return;
        for (v = 0; v < n; v++) {
            const uint32_t l = str_len(w[v]) + 1u;
            if (cep_npool + l > CEP_POOL)
                return;
            memcpy(cep_pool + cep_npool, w[v], l);
            cep_ptr[cep_nptr + (uint32_t)v] = cep_pool + cep_npool;
            cep_npool += l;
        }
        q->fmt = RFM1_EP_TABLE;
        q->names = &cep_ptr[cep_nptr];
        cep_nptr += (uint32_t)n;
    }
}

/* ----------------------------------------------------------------------------------------- the parameters --- */
static char cep_key[CEP_MAXP][48];    /* each parameter's key (cr_ep_ids.h) */
static const char *cep_key_of(uint32_t i) { return cep_key[i]; }
static void cep_keys_put(uint32_t i, const char *k) { cep_cpy(cep_key[i], k, sizeof cep_key[i]); }
static int cep_gen;                    /* (the ctest's --gen: a key the table lacks gets the next free pid) */
static uint16_t cep_gen_next;
static uint16_t cep_pid_of(const char *key)
{
    uint32_t i;
    for (i = 0; i < NELEM(CR_EP_IDS); i++)
        if (CR_EP_IDS[i].key && cu_eq(CR_EP_IDS[i].key, key))
            return CR_EP_IDS[i].pid;
    cep_missing++;
    if (!cep_gen)
        return 0;
    if (!cep_gen_next) {
        cep_gen_next = 1;
        for (i = 0; i < NELEM(CR_EP_IDS); i++)
            if (CR_EP_IDS[i].pid >= cep_gen_next)
                cep_gen_next = (uint16_t)(CR_EP_IDS[i].pid + 1u);
    }
    return cep_gen_next++;
}
/* the dynamic columns: their names or value words follow another value (the engines' eng_deep_t.desc hooks: the VA's
 * WAVE / SHAPE by the oscillator's MODE, an LFO's RATE by its SYNC; FM6's COARSE by Ratio / Fixed; FM TONE's FILTER+
 * by the filter type; GRAIN's LFO RATE) */
static int cep_dyn(uint32_t e, const eng_deep_t *dd, ce_ref_t r)
{
    const char *pt, *cl;
    if (r.k != CE_R_DEEP || !dd->desc)
        return 0;
    pt = dd->pages[r.a].title, cl = dd->pages[r.a].col[r.b & 3u].label;
    switch (e) {
    case ENGI_VA:                      /* (eng_va.c va_desc) */
        return (ce_inst(pt) && !cp_eq(ce_suf(pt), "+") && cp_eq(cl, "WAVE") && !cu_eq(pt, "LFO")) ||
               (cp_eq(ce_suf(pt), "+") && cp_eq(cl, "SHAPE")) || ((pt[0] == 'L' || pt[0] == 'l') && cp_eq(cl, "RATE"));
    case ENGI_FM6: return cp_eq(cl, "COARSE");   /* (eng_fm6.c fm6_ddesc) */
    case ENGI_QUAD: return cp_eq(pt, "FILTER+") && (r.b & 3u) >= 1u && (r.b & 3u) <= 2u;   /* (quad_desc) */
    case 8u: return cp_eq(cl, "RATE");   /* GRAIN (grain_desc) */
    default: return 0;
    }
}
/* the parameter of a cell (made the first time its key is seen): its index, -1 none */
static int cep_param(const track_t *t, uint32_t ei, ce_ref_t r, uint32_t expr, const char *inst)
{
    const eng_deep_t *dd = cp_deep(t);
    const param_desc_t *d;
    char key[48], lb[24];
    uint32_t i, e = eng_idx(t->eng_req);
    rfm1_ep_param_t *q;
    if (r.k == CE_R_DEEP && dd && r.a < dd->npages && dd->pages[r.a].col[r.b & 3u].label) {
        d = &dd->pages[r.a].col[r.b & 3u];
        cep_cpy(key, "", sizeof key);
        cu_int(key, (int32_t)e, 0, sizeof key);
        cu_cat(key, "/", sizeof key);
        cu_cat(key, dd->pages[r.a].title, sizeof key);
        cu_cat(key, "/", sizeof key);
        cu_cat(key, d->label, sizeof key);
    } else if (r.k == CE_R_TRK && r.a < P_COUNT) {
        d = param_desc_of(e, r.a);
        if (expr) {
            cep_cpy(key, r.a <= P_MVEL_SRC + 2u ? "*/EXPR/VEL " : "*/EXPR/AT ", sizeof key);
            cu_cat(key, (r.a == P_MVEL_SRC + 1u || r.a == P_MAT_SRC + 1u) ? "DEST" : "AMT", sizeof key);
        } else if (r.a >= P_E0 && r.a <= P_E7) {
            cep_cpy(key, "", sizeof key);
            cu_int(key, (int32_t)e, 0, sizeof key);
            cu_cat(key, "/TRK/E", sizeof key);
            ce_cat_num(key, r.a - P_E0, sizeof key);
        } else {
            cep_cpy(key, "*/TRK/", sizeof key);
            ce_label(t, r, d, lb, sizeof lb);
            cu_cat(key, lb, sizeof key);
        }
    } else
        return -1;
    cep_cpy(key, key, sizeof key);     /* (printable: the editor's titles have its middle dot) */
    for (i = 0; i < cep_np; i++)
        if (cep_par[i].pid && cu_eq(cep_key_of(i), key))
            return (int)i;
    if (cep_np >= CEP_MAXP)
        return -1;
    q = &cep_par[cep_np];
    memset(q, 0, sizeof *q);
    q->pid = cep_pid_of(key);
    if (!q->pid)
        return -1;
    cep_keys_put(cep_np, key);
    ce_label(t, r, d, lb, sizeof lb);
    i = str_len(lb);                   /* "Sync1" -> "Sync 1" */
    if (i >= 2u && i < 10u && lb[i - 1u] >= '1' && lb[i - 1u] <= '9' && lb[i - 2u] >= 'a' && lb[i - 2u] <= 'z') {
        lb[i + 1u] = 0, lb[i] = lb[i - 1u], lb[i - 1u] = ' ';
    }
    if (inst && inst[0]) {             /* "Rate 1", "Rate 2": the instance after the name (cut to fit) */
        uint32_t li = str_len(inst);
        if (str_len(lb) + 1u + li > 10u)
            lb[10u - 1u - li] = 0;
        cu_cat(lb, " ", sizeof lb);
        cu_cat(lb, inst, sizeof lb);
    }
    cep_cpy(cep_pname[cep_np], lb, 11);
    q->name = cep_pname[cep_np];
    q->min = d->min, q->max = d->max > d->min ? d->max : (int16_t)(d->min + 1);
    q->def = d->def < q->min ? q->min : d->def > q->max ? q->max : d->def;
    q->cc = 255;
    q->flags = RFM1_EPF_SOUND;
    if (r.k == CE_R_TRK && ce_send_ref(t, r) >= 0)
        q->flags = 0;                  /* (the chord's sends are the FX amounts: not the sound's) */
    if (d->fmt == F_ENUM || d->fmt == F_ONOFF)
        q->flags |= RFM1_EPF_LIST;
    else if (d->names && d->fmt == F_INT && !ce_pair(d)) {
        for (i = 0; d->names[i]; i++)
            ;
        if (i == (uint32_t)(q->max - q->min + 1) && i <= 16u)
            q->flags |= RFM1_EPF_LIST;
    }
    if (d->fmt == F_FIX2)
        q->step = 100;                 /* (the editor's whole number; Shift: a hundredth) */
    cep_x[cep_np].eng = r.k == CE_R_DEEP || (r.a >= P_E0 && r.a <= P_E7) ? (uint8_t)ei : 0xFFu;
    cep_x[cep_np].expr = (uint8_t)expr;
    cep_x[cep_np].r = r;
    cep_x[cep_np].dyn = (uint8_t)(expr || (dd && cep_dyn(e, dd, r)));
    if (cep_x[cep_np].dyn)
        q->flags |= RFM1_EPF_DYN;
    if (expr) {                        /* (the amounts: the number, as the editor; the destinations: its words) */
        q->min = d->min, q->max = d->max;
    }
    cep_format(q, t, r, d);
    return (int)(cep_np++);
}

/* ------------------------------------------------------------------------------------------- the pages --- */
typedef struct {
    ce_ref_t r[4];
    cep_at_t at[4];
    uint8_t type, pg;                  /* the screen's type, the deep page (its title) */
    uint8_t expr;
} cep_lane_t;
static uint32_t cep_npg;               /* (while an engine's pages are made) */
static uint8_t cep_ecur;
static int cep_page(const track_t *t, const char *name, uint8_t group, const cep_lane_t *a, const cep_lane_t *b,
                    const char *ia, const char *ib)
{
    rfm1_ep_page_t *g;
    uint32_t k, any = 0;
    if (cep_npg >= CEP_MAXPG)
        return 0;
    g = &cep_pg[cep_ecur][cep_npg];
    memset(g, 0, sizeof *g);
    memset(cep_at[cep_ecur][cep_npg], 0xFF, sizeof cep_at[0][0]);
    for (k = 0; k < 8u; k++) {
        const cep_lane_t *l = k < 4u ? a : b;
        int x;
        if (!l || l->r[k & 3u].k == CE_R_NONE)
            continue;
        x = cep_param(t, cep_ecur, l->r[k & 3u], l->expr, k < 4u ? ia : ib);
        if (x < 0)
            continue;
        g->pid[k] = cep_par[x].pid;
        cep_at[cep_ecur][cep_npg][k] = l->at[k & 3u];
        any = 1;
    }
    if (!any)
        return 0;
    cep_fit(cep_pgname[cep_ecur][cep_npg], name);
    g->name = cep_pgname[cep_ecur][cep_npg];
    g->id = (uint8_t)cep_npg;
    g->group = group;
    cep_npg++;
    return 1;
}
static ce_ref_t cep_none(void) { return ce_r(CE_R_NONE, 0, 0); }
/* lane ln of screen k of group g: its refs and the editor's places */
static void cep_lane(const track_t *t, uint32_t g, uint32_t k, uint32_t ln, const ce_scr_t *sc, cep_lane_t *o)
{
    const eng_deep_t *d = ce_deep(t);
    uint32_t c;
    memset(o, 0, sizeof *o);
    o->type = sc->type;
    for (c = 0; c < 4u; c++) {
        o->r[c] = cep_none();
        o->at[c].g = (uint8_t)g, o->at[c].scr = (uint8_t)k, o->at[c].ln = (uint8_t)ln, o->at[c].col = (uint8_t)c;
    }
    switch (sc->type) {
    case CE_S_STACK:
    case CE_S_EDIT:
        o->pg = sc->pg[ln];
        for (c = 0; c < 4u; c++)
            if (d->pages[sc->pg[ln]].col[c].label)
                o->r[c] = ce_r(CE_R_DEEP, sc->pg[ln], c);
        break;
    case CE_S_PAGE:
        o->pg = sc->pg[0];
        for (c = 0; c < 4u; c++)
            if (d->pages[sc->pg[0]].col[c].label)
                o->r[c] = ce_r(CE_R_DEEP, sc->pg[0], c);
        break;
    case CE_S_TRANS:                   /* "LFO SYN": lane i is column i; here one lane of the four */
        o->pg = sc->pg[0];
        for (c = 0; c < 4u; c++)
            if (d->pages[sc->pg[0]].col[c].label) {
                o->r[c] = ce_r(CE_R_DEEP, sc->pg[0], c);
                o->at[c].ln = (uint8_t)c, o->at[c].col = 0;
            }
        break;
    case CE_S_PLAT: {
        static const uint8_t CP[5] = {CP_EDIT1, CP_EDIT2, CP_ENV, CP_LFO, CP_MOD};
        for (c = 0; c < 4u && g <= CE_MOD; c++) {
            o->r[c] = ce_r(CE_R_TRK, CP_PAGES[CP[g]].id[c], 0);
            if (g == CE_MOD)               /* (the routes: lane c, its amount on KNOB 3) */
                o->at[c].ln = (uint8_t)c, o->at[c].col = 2;
        }
        break;
    }
    default:
        break;
    }
}
static void cep_title(const eng_deep_t *d, uint32_t pg, char *o, uint32_t n)   /* "ENV 2+" -> "ENV 2" */
{
    uint32_t l;
    cep_cpy(o, d->pages[pg].title, n);
    l = str_len(o);
    while (l && (o[l - 1u] == '+' || o[l - 1u] == ' '))
        o[--l] = 0;
}
static const uint8_t CEP_GROUP[5] = {RFM1_EPG_OSC, RFM1_EPG_FILTER, RFM1_EPG_ENV, RFM1_EPG_LFO, RFM1_EPG_MOD};
/* the pages of engine e (ENGINES[] index) */
/* a page made of one or two lanes, before the pairing of single lanes (cep_flush) */
typedef struct {
    cep_lane_t a, b;
    uint8_t hasb, group;
    char name[24], base[24], ia[4], ib[4];
} cep_cand_t;
static cep_cand_t cep_cand[48];
static uint32_t cep_ncand;
static void cep_add(const char *name, uint8_t group, const cep_lane_t *a, const cep_lane_t *b, const char *ia,
                    const char *ib)
{
    cep_cand_t *c;
    uint32_t i;
    if (cep_ncand >= NELEM(cep_cand))
        return;
    c = &cep_cand[cep_ncand++];
    memset(c, 0, sizeof *c);
    c->a = *a;
    if (b)
        c->b = *b, c->hasb = 1;
    c->group = group;
    cep_cpy(c->name, name, sizeof c->name);
    cep_cpy(c->base, name, sizeof c->base);   /* "ENV 2 . filter", "ENV 2 +" -> "ENV 2" */
    for (i = 0; c->base[i]; i++)
        if ((c->base[i] == ' ' && (c->base[i + 1] == '.' || c->base[i + 1] == '+')) || c->base[i] == '+') {
            c->base[i] = 0;
            break;
        }
    if (ia)
        cep_cpy(c->ia, ia, sizeof c->ia);
    if (ib)
        cep_cpy(c->ib, ib, sizeof c->ib);
}
/* the name a plan title gives ("ENV 2 \267 filter" -> "FILT ENV", "ENV1 \267 amp" -> "AMP ENV"), else the title */
static void cep_plan_name(const char *title, char *o, uint32_t n)
{
    char b[24];
    uint32_t i;
    cep_cpy(b, title, sizeof b);
    for (i = 0; b[i] && !(b[i] == ' ' && b[i + 1] == '.' && b[i + 2] == ' '); i++)
        ;
    if (b[i] && (b[0] == 'E' && b[1] == 'N' && b[2] == 'V')) {
        char w[8];
        uint32_t k;
        cep_cpy(w, b + i + 3, 5);
        for (k = 0; w[k]; k++)
            w[k] = (char)(w[k] >= 'a' && w[k] <= 'z' ? w[k] - 32 : w[k]);
        if (cu_eq(w, "FILTE"))
            w[4] = 0;
        cep_cpy(o, w, n);
        cu_cat(o, " ENV", n);
        return;
    }
    b[i] = 0;
    cep_cpy(o, b, n);
}
/* the candidates become pages: a lane alone (knobs 1-4) is paired with the next lane alone of its group when that one
 * is its "+" (the same name before " +" or " \267": P5's "ENV 2 \267 filter" and "ENV 2 +"), or when it has a single
 * cell (the VA's VOICE beside LFO SYN) */
static void cep_flush(const track_t *t)
{
    uint32_t i, k, n;
    for (i = 0; i < cep_ncand; i++) {
        cep_cand_t *c = &cep_cand[i], *d = i + 1u < cep_ncand ? &cep_cand[i + 1u] : 0;
        if (!c->hasb && d && !d->hasb && d->group == c->group) {
            for (k = 0, n = 0; k < 4u; k++)
                n += d->a.r[k].k != CE_R_NONE;
            if (cu_eq(c->base, d->base) || n == 1u) {
                cep_page(t, cu_eq(c->base, d->base) && !cu_eq(c->name, c->base) ? c->name
                            : cu_eq(c->base, d->base) && !cu_eq(d->name, d->base) ? d->name : c->name,
                         c->group, &c->a, &d->a, c->ia[0] ? c->ia : 0, d->ia[0] ? d->ia : 0);
                i++;
                continue;
            }
        }
        cep_page(t, c->name, c->group, &c->a, c->hasb ? &c->b : 0, c->ia[0] ? c->ia : 0, c->ib[0] ? c->ib : 0);
    }
    cep_ncand = 0;
}
static void cep_engine(uint32_t ei, uint32_t e)
{
    static track_t tt;                 /* (the walk needs only the engine: ce_scr, ce_label, ce_text are pure) */
    const track_t *t = &tt;
    const eng_deep_t *d;
    uint32_t g, k, ns, i, j;
    ce_scr_t sc, s2;
    cep_lane_t la, lb, plat[5];
    uint8_t has_plat[5] = {0};
    char nm[24], ia[4], ib[4];
    memset(&tt, 0, sizeof tt);
    tt.eng_req = (uint8_t)e;
    d = ce_deep(t);
    cep_ecur = (uint8_t)ei;
    cep_npg = 0;
    cep_ncand = 0;
    for (g = CE_OSC; g <= CE_MOD; g++) {
        uint8_t used[48] = {0};
        if (!ce_has(t, g))
            continue;
        ns = ce_scr(t, g, 0, &sc);
        for (k = 0; k < ns && k < 48u; k++) {
            ce_scr(t, g, k, &sc);
            if (used[k] || sc.type == CE_S_MIXER || sc.type == CE_S_EXPR)
                continue;
            if (sc.type == CE_S_PLAT) {
                cep_lane(t, g, k, 0, &sc, &plat[g]);
                has_plat[g] = 1;
                continue;
            }
            if (sc.type == CE_S_STACK) {
                uint32_t m = (uint32_t)-1;
                for (j = k + 1u; j < ns && j < 48u && m == (uint32_t)-1; j++) {   /* its "+" screen */
                    ce_scr(t, g, j, &s2);
                    if (s2.type != CE_S_STACK || s2.n != sc.n || g == CE_MOD)
                        continue;
                    for (i = 0; i < sc.n; i++) {
                        char a[16], b[16];
                        cep_cpy(a, d->pages[sc.pg[i]].title, sizeof a);
                        cu_cat(a, "+", sizeof a);
                        cep_cpy(b, d->pages[s2.pg[i]].title, sizeof b);
                        if (!cu_eq(a, b))
                            break;
                    }
                    if (i == sc.n)
                        m = j;
                }
                if (m != (uint32_t)-1) {
                    used[m] = 1;
                    ce_scr(t, g, m, &s2);
                    for (i = 0; i < sc.n; i++) {
                        cep_lane(t, g, k, i, &sc, &la);
                        cep_lane(t, g, m, i, &s2, &lb);
                        cep_title(d, sc.pg[i], nm, sizeof nm);
                        cep_add(nm, CEP_GROUP[g], &la, &lb, 0, 0);
                    }
                    continue;
                }
                for (i = 0; i < sc.n; i += 2u) {   /* two instances side by side: "LFO 1/2", "MOD 3/4" */
                    const char *ta = d->pages[sc.pg[i]].title;
                    uint32_t na = ce_inst(ta), nb;
                    cep_lane(t, g, k, i, &sc, &la);
                    if (i + 1u < sc.n) {
                        cep_lane(t, g, k, i + 1u, &sc, &lb);
                        nb = ce_inst(d->pages[sc.pg[i + 1u]].title);
                        if (g == CE_MOD && !na)
                            na = k * CR_ED_ROWS + i + 1u, nb = na + 1u;
                        cep_title(d, sc.pg[i], nm, sizeof nm);
                        for (j = 0; nm[j] && nm[j] != ' '; j++)
                            ;
                        nm[j] = 0;
                        cu_cat(nm, " ", sizeof nm);
                        ce_cat_num(nm, na, sizeof nm);
                        cu_cat(nm, "/", sizeof nm);
                        ce_cat_num(nm, nb, sizeof nm);
                        ia[0] = ib[0] = 0;
                        ce_cat_num(ia, na, sizeof ia);
                        ce_cat_num(ib, nb, sizeof ib);
                        cep_add(nm, CEP_GROUP[g], &la, &lb, ia, ib);
                    } else {
                        cep_title(d, sc.pg[i], nm, sizeof nm);
                        cep_add(nm, CEP_GROUP[g], &la, 0, 0, 0);
                    }
                }
                continue;
            }
            if (sc.type == CE_S_EDIT) {
                const eng_screen_t *h = ce_plan(d, sc.hs);
                uint32_t c0, cn;
                cep_lane(t, g, k, 0, &sc, &la);
                if (sc.n > 1u)
                    cep_lane(t, g, k, 1, &sc, &lb);
                if (h && h->title)
                    cep_plan_name(h->title, nm, sizeof nm);
                else
                    cep_title(d, sc.pg[0], nm, sizeof nm);
                if (g == CE_ENV && ce_is_cz(d, sc.pg[0], &c0, &cn)) {   /* CZ-1's step envelopes: "DCW 1 5-8" */
                    static const char *const PART[3] = {" 1-4", " 5-8", " END"};
                    const uint32_t pi = (sc.pg[0] - c0) / 2u;
                    cep_cpy(nm, d->pages[c0].title, sizeof nm);
                    cu_cat(nm, PART[pi > 2u ? 2u : pi], sizeof nm);
                }
                cep_add(nm, CEP_GROUP[g], &la, sc.n > 1u ? &lb : 0, 0, 0);
                if (h && h->title && cep_ncand) {   /* (the pairing by the plan's own title: "ENV 2 \267 filter") */
                    cep_cand_t *c = &cep_cand[cep_ncand - 1u];
                    cep_cpy(c->base, h->title, sizeof c->base);
                    for (i = 0; c->base[i]; i++)
                        if ((c->base[i] == ' ' && (c->base[i + 1] == '.' || c->base[i + 1] == '+')) || c->base[i] == '+') {
                            c->base[i] = 0;
                            break;
                        }
                }
                continue;
            }
            if (sc.type == CE_S_PAGE || sc.type == CE_S_TRANS) {
                cep_lane(t, g, k, 0, &sc, &la);
                cep_title(d, sc.pg[0], nm, sizeof nm);
                cep_add(nm, CEP_GROUP[g], &la, 0, 0, 0);
            }
        }
        cep_flush(t);
    }
    {   /* the platform's pages (an engine without deep pages: WHEEL; FM TONE's MOD routes) */
        const engine_t *en = ENGINES[e];
        if (has_plat[CE_OSC] || has_plat[CE_FILT]) {
            cep_cpy(nm, en->page_title[0] ? en->page_title[0] : "EDIT", sizeof nm);
            cep_page(t, nm, RFM1_EPG_OSC, has_plat[CE_OSC] ? &plat[CE_OSC] : 0, has_plat[CE_FILT] ? &plat[CE_FILT] : 0,
                     0, 0);
        }
        if (has_plat[CE_ENV] || has_plat[CE_LFO])
            cep_page(t, has_plat[CE_ENV] && has_plat[CE_LFO] ? "ENV LFO" : has_plat[CE_ENV] ? "ENV" : "LFO",
                     RFM1_EPG_ENV, has_plat[CE_ENV] ? &plat[CE_ENV] : 0, has_plat[CE_LFO] ? &plat[CE_LFO] : 0, 0, 0);
        if (has_plat[CE_MOD])
            cep_page(t, "MOD", RFM1_EPG_MOD, &plat[CE_MOD], 0, 0, 0);
    }
    {   /* the shared pages: EXPR (MOD's last screen), FX, MIX */
        uint32_t ex;
        ns = ce_scr(t, CE_MOD, 0, &sc);
        for (ex = 0; ex < ns; ex++) {
            ce_scr(t, CE_MOD, ex, &sc);
            if (sc.type == CE_S_EXPR)
                break;
        }
        memset(&la, 0, sizeof la);
        la.expr = 1;
        for (i = 0; i < 4u; i++) {      /* VEL dest, VEL amt, AT dest, AT amt */
            const uint32_t src = i < 2u ? P_MVEL_SRC : P_MAT_SRC;
            la.r[i] = ce_r(CE_R_TRK, src + 1u + (i & 1u), 0);
            la.at[i].g = CE_MOD, la.at[i].scr = (uint8_t)ex, la.at[i].ln = (uint8_t)(i / 2u), la.at[i].col = (uint8_t)(1u + (i & 1u));
        }
        cep_page(t, "EXPR", RFM1_EPG_MOD, &la, 0, 0, 0);
        ce_scr(t, CE_FX, 0, &sc);
        cep_lane(t, CE_FX, 0, 0, &sc, &la);
        for (i = 0; i < 4u; i++)
            la.r[i] = ce_r(CE_R_TRK, CP_PAGES[CP_FX].id[i], 0);
        cep_page(t, "FX", RFM1_EPG_FX, &la, 0, 0, 0);
        ce_scr(t, CE_MIX, 0, &sc);
        cep_lane(t, CE_MIX, 0, 0, &sc, &la);
        cep_lane(t, CE_MIX, 0, 1, &sc, &lb);
        for (i = 0; i < 4u; i++) {
            la.r[i] = ce_r(CE_R_TRK, CP_PAGES[CP_MIX].id[i], 0);
            lb.r[i] = ce_r(CE_R_TRK, CP_PAGES[CP_MIX2].id[i], 0);
        }
        cep_page(t, "MIX", RFM1_EPG_MIX, &la, &lb, 0, 0);
    }
    cep_eng[ei].id = (uint16_t)e;
    cep_eng[ei].name = ENGINES[e]->name;
    cep_eng[ei].pages = cep_pg[ei];
    cep_eng[ei].npages = (uint8_t)cep_npg;
}

/* ------------------------------------------------------------------------------------------- the callbacks --- */
static int cep_engi(uint32_t e)        /* ENGINES[] index -> the description's engine, -1 none */
{
    uint32_t i;
    for (i = 0; i < CEP_NENG; i++)
        if (ENGINE_ORDER[i] == e)
            return (int)i;
    return -1;
}
static void cep_layer(int layer, int *engine, int *ch)
{
    const track_t *t = cep_trk(layer);
    const uint32_t c = cs.ch[layer ? CR_STREAM_BASS : CR_STREAM_MAIN];
    *engine = layer && !cs.bass_on ? -1 : cep_engi(t->eng_req);
    *ch = c >= 1u && c <= 16u ? (int)c - 1 : -1;
}
static int cep_find(uint16_t pid)
{
    int lo = 0, hi = (int)cep_np - 1;
    static uint16_t ord[CEP_MAXP];
    static int sorted;
    if (!sorted) {                     /* (an index by pid, made once) */
        uint32_t i, j;
        for (i = 0; i < cep_np; i++) {
            for (j = i; j > 0 && cep_par[ord[j - 1]].pid > cep_par[i].pid; j--)
                ord[j] = ord[j - 1];
            ord[j] = (uint16_t)i;
        }
        sorted = 1;
    }
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
        if (cep_par[ord[m]].pid == pid)
            return ord[m];
        if (cep_par[ord[m]].pid < pid)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return -1;
}
/* the cell of pid on layer's track now: 0 when its engine is another */
static int cep_cell(int layer, uint16_t pid, track_t **t, ce_ref_t *r, int *x)
{
    *x = cep_find(pid);
    if (*x < 0 || layer < 0 || layer > 1)
        return 0;
    *t = cep_trk(layer);
    *r = cep_x[*x].r;
    if (cep_x[*x].eng != 0xFFu && ENGINE_ORDER[cep_x[*x].eng] != eng_idx((*t)->eng_req))
        return 0;
    return 1;
}
static int32_t cep_get(int layer, uint16_t pid)
{
    track_t *t;
    ce_ref_t r;
    int x;
    int32_t v = 0;
    if (!cep_cell(layer, pid, &t, &r, &x))
        return RFM1_EP_NONE;
    if (cep_x[x].expr) {               /* an EXPR slot not set: the defaults its knob starts from */
        const uint32_t src = r.a <= P_MVEL_SRC + 2u ? P_MVEL_SRC : P_MAT_SRC;
        if (!t->p[src])
            return r.a == src + 1u ? (src == P_MAT_SRC ? MD_CUT : MD_AMP)
                                   : (src == P_MAT_SRC ? cr_touch_at_amt() : CR_TOUCH_VEL_DEF_AMT);
        return t->p[r.a];
    }
    if (!ce_param(t, r, &v))
        return RFM1_EP_NONE;
    return v;
}
/* ce_knob's write for the cell, the value given (its three ways; the knob's alias skipping for an F_ENUM) */
static void cep_set(int layer, uint16_t pid, int32_t v)
{
    track_t *t;
    ce_ref_t r;
    int x;
    int32_t v0 = 0, sid;
    const param_desc_t *d;
    const uint32_t part = (uint32_t)layer & 1u;
    if (!cep_cell(layer, pid, &t, &r, &x))
        return;
    if (cep_x[x].expr) {
        const uint32_t src = r.a <= P_MVEL_SRC + 2u ? P_MVEL_SRC : P_MAT_SRC;
        if (!t->p[src]) {              /* (ce_knob: an unset slot's defaults first, then the turn) */
            fm1_irq_off();
            t->p[src] = (int16_t)(src == P_MAT_SRC ? MS_AT : MS_VEL);
            t->p[src + 1u] = (int16_t)(src == P_MAT_SRC ? MD_CUT : MD_AMP);
            t->p[src + 2u] = (int16_t)(src == P_MAT_SRC ? cr_touch_at_amt() : CR_TOUCH_VEL_DEF_AMT);
            fm1_irq_on();
        }
    }
    if (!(d = ce_param(t, r, &v0)))
        return;
    v = clamp(v, d->min, d->max);
    if (d->fmt == F_ENUM && v != v0)
        v = enum_step(d, v0, v);
    if ((sid = ce_send_ref(t, r)) >= 0) {   /* a send: the part's one value (cu_send_set, as ce_knob) */
        cu_send_set(part, (uint32_t)sid, v);
        return;
    }
    if (r.k == CE_R_DEEP) {
        const eng_deep_t *dd = cp_deep(t);
        if (v != v0) {
            dd->set(t, r.a, r.b, v);
            cu_edited(part);
        }
        return;
    }
    fm1_irq_off();
    t->p[r.a] = (int16_t)v;
    fm1_irq_on();
    if (v != v0)
        cu_edited(part);
}
/* the cell's name and words as the editor shows them now (its mode-dependent descriptor) */
static int cep_wordsf(int layer, uint16_t pid, char name[11], char text[11])
{
    track_t *t;
    ce_ref_t r;
    int x;
    int32_t v = 0;
    const param_desc_t *d;
    char b[24];
    if (!cep_cell(layer, pid, &t, &r, &x))
        return 0;
    if (cep_x[x].expr) {
        const uint32_t src = r.a <= P_MVEL_SRC + 2u ? P_MVEL_SRC : P_MAT_SRC;
        if (!t->p[src]) {              /* (the editor's "AMP auto" / "CUT auto") */
            cep_cpy(name, cep_par[x].name, 11);
            cep_cpy(text, r.a == src + 1u ? CE_TXT[src == P_MAT_SRC ? CE_T_CUT : CE_T_AMP] : CE_TXT[CE_T_AUTO], 11);
            return 1;
        }
    }
    if (!(d = ce_param(t, r, &v)))
        return 0;
    if (cep_x[x].dyn && r.k == CE_R_DEEP) {
        ce_label(t, r, d, b, sizeof b);
        cep_cpy(name, b, 11);
    } else
        cep_cpy(name, cep_par[x].name, 11);
    ce_text(t, r, d, v, b, sizeof b);
    cep_cpy(text, b, 11);
    return 1;
}
static void cep_make(void)
{
    uint32_t i;
    for (i = 0; i < CEP_NENG; i++)
        cep_engine(i, ENGINE_ORDER[i]);
    cep_src.layer_names = CEP_LAYERS;
    cep_src.nlayers = 2;
    cep_src.layer_flags = RFM1_EPL_BY_CHANNEL;
    cep_src.engines = cep_eng;
    cep_src.nengines = (uint8_t)CEP_NENG;
    cep_src.params = cep_par;
    cep_src.nparams = cep_np;
    cep_src.epoch = 0;
    cep_src.layer = cep_layer;
    cep_src.get = cep_get;
    cep_src.set = cep_set;
    cep_src.words = cep_wordsf;
    cep_made = 1;
}
const rfm1_ep_src_t *fm1core_ep(void)
{
    if (!cep_made)
        cep_make();
    return cep_np ? &cep_src : 0;
}
