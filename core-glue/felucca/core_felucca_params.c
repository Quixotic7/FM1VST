/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca core's Tier 2 parameter map (FM1-VST-PLAN.md 4.5; core-api/fm1core.h fm1param_t).
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_felucca.c includes this file after the firmware unit (felucca_fw.c), so
 * the firmware's statics (params.c's TP / GP tables, param_format, track_desc, the tracks, the song, ui.c's
 * track_select, motion.c's motion_capture) are in scope. Nothing in the submodule is modified.
 *
 * WHAT IS MAPPED: the selected part's parameters, as the device's pages show them (params.c PAGES): the level and
 * sends, ENV, ENV DEST, the engine's EDIT 1 / EDIT 2, LFO, LFO DEST, VOICE, ARP, SCL / CHORD,
 * PATTERN, two MOD slots; the part selection itself; the song's GLOBAL / FX bus parameters (tempo, swing, tune,
 * delay, reverb, chorus). "The selected part" follows the device: select another part (the ALGORITHM knob, or the
 * "Part" entry) and every part entry reads and writes that part.
 *
 * HOW A HOST WRITE REACHES THE FIRMWARE: the knob's own write (ui_input.c edit_param / the HOME knobs / tracks_edit,
 * and editor.c's ED_SET, which is the same three lines): the value clamped to the descriptor's range, stored in the
 * part's (or the song's) parameter array, an alias of a retired enum value resolved (params.c enum_orig), and
 * motion_capture (motion.c: a live edit becomes the motion sequencer's new base value; recorded as a motion event
 * while the part records). The audio ISR reads the arrays as they are (it is what the knob does). Pattern LEN / DIV
 * / SWING / GATE refuse while a song chain plays ("STOP TO EDIT", as the knob). Nothing here is in the settings
 * record (no FM1P_PERSIST): parts and song are the project, saved by SAVE > PROJECT on the device.
 *
 * THE META ENTRIES (FM1P_META): "E1".."E8", the selected part's engine parameters (EDIT 1 / EDIT 2). Their range,
 *   names and label depend on the engine (FM6's ALG is 0..32, ANALOG's WAVE an enum of 5), so these entries target
 *   THEMSELVES and their descriptor (name "E1 WAVE", min, max, def, names, FM1P_ENUM) is rewritten from the engine's
 *   track_desc before the epoch moves; the host re-reads it then (Tier2Parameter::retarget).
 *
 * THE KNOBS (knob_target, ABI 4): what each physical knob turns on the screen showing now, ui_input's own dispatch
 * read without turning (fe_knob_compute). "turn": no value there, the host turns it as a relative control. MASTER is
 * the pot everywhere.
 *   screen                     SELECT     PRESETS        ALGORITHM   KNOB1..4
 *   HOME                       Tempo      turn (sounds)  Part        the engine's four HOME knobs (home_param: ANALOG E5 CUT,
 *                                                                        E6 RES, Attack, Release; FM6 E3 MLVL ..)
 *   a page (ENV, ENV DEST, LFO, LFO DEST, FX, DLY, REVERB, CHORUS, SCL, CHORD, EDIT 1 / 2, VOICE, VOICE 2, GLOBAL,
 *   SYSTEM, ARP, ARP 2, PATTERN, SLICER, OP ENV ..)
 *                              (as HOME)  turn           Part        the page's four parameters: their entries, else
 *                                                                    the hidden "Knob 1..4" cells (rewritten from
 *                                                                    page_desc: "SLICER SLCR"); an empty column: turn
 *   MOD                                                              KNOB1 turn (the slot), 2..4 the slot's SRC DST AMT
 *   MIXER                                                            Level, Pan, Reverb Send, Mute
 *   PRESETS, USER, PHRASES, SONG, STEP, CHANCE, MOTION, TOOLS, PROJECT's actions: turn (lists, cursors, actions)
 *   FX held                    (as HOME)  turn           turn        FX Filter, FX Crush, FX Throw, FX Depth (H, perf_k)
 *   GLO held                                                         T1 Level .. T4 Level (H)
 *   SCL held                                                         Scale Root, Scale, Chord, Chord Voicing
 *   EDIT held                                                        turn (ENG, No., FAV)
 *   the menu, a dialog, NAME   turn       turn           turn        turn
 *   (H): FM1P_HIDDEN, a knob target only.
 *   param_epoch moves when the selected part, its engine, one of its engine descriptors (a mode-dependent label,
 *   engine_t.desc), or any knob's target (or a rewritten cell) changed: the targets are computed afresh at every call.
 *
 * THREADS: get / set / target / param_epoch run on the clock's thread between device milliseconds; text() formats
 * from the constant tables (and the E entries' descriptor snapshot, double-buffered) and may run on any thread.
 * (param_format writes params.c's fmt_named, a flag only ui_draw.c's next card reads after its own param_format.) */

#define FE_MELODEE 0
#define FE_SLICE FELUCCA_SLICE
#define FE_SELECT_TARGET fe_index_of(FEK_GP, G_BPM)   /* SELECT: the global tempo, everywhere but the menu, a dialog, NAME */

static int32_t fe_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the firmware's value text: param_format's value and unit ("355 Hz", "1/8", "+12 st", "OFF") */
static void fe_fmt(const param_desc_t *d, int32_t v, char *b, uint32_t n)
{
    char val[24];
    const char *unit = "";
    val[0] = 0;
    param_format(d, v, val, &unit);
    snprintf(b, n, unit && unit[0] ? "%s %s" : "%s", val, unit ? unit : "");
}

/* ---- a parameter of the selected part (TP id, or P_E0.. through the engine's descriptor) */
static int32_t fe_tget(uint32_t id) { return TSEL->p[id % P_COUNT]; }
static void fe_tset(uint32_t id, int32_t v)
{
    track_t *t = TSEL;
    const param_desc_t *d;
    id %= P_COUNT;
    d = track_desc(t, id);
    if (d->max <= d->min)
        return;                                   /* (an empty column: "-") */
    if (chain_busy() && id >= P_SLEN && id <= P_SGATE)
        return;                                   /* the knob: "STOP TO EDIT" */
    t->p[id] = (int16_t)enum_orig(d, fe_clamp(v, d->min, d->max));
    motion_capture(t, id, t->p[id]);
}
/* ---- a song parameter (GP id) */
static int32_t fe_gget(uint32_t id) { return song.g[id % G_COUNT]; }
static void fe_gset(uint32_t id, int32_t v)
{
    const param_desc_t *d = &GP[id % G_COUNT];
    if (d->max > d->min)
        song.g[id % G_COUNT] = (int16_t)fe_clamp(v, d->min, d->max);
}

#define FE_TP_FN(id) \
    static int32_t fe_g_##id(void) { return fe_tget(id); } \
    static void fe_s_##id(int32_t v) { fe_tset(id, v); } \
    static void fe_t_##id(int32_t v, char *b, uint32_t n) { fe_fmt(&TP[id], v, b, n); }
#define FE_GP_FN(id) \
    static int32_t fe_g_##id(void) { return fe_gget(id); } \
    static void fe_s_##id(int32_t v) { fe_gset(id, v); } \
    static void fe_t_##id(int32_t v, char *b, uint32_t n) { fe_fmt(&GP[id], v, b, n); }
FE_TP_FN(P_LEVEL) FE_TP_FN(P_ATK) FE_TP_FN(P_DEC) FE_TP_FN(P_SUS) FE_TP_FN(P_REL)
FE_TP_FN(P_ED_FLT) FE_TP_FN(P_ED_PIT) FE_TP_FN(P_ED_SHP)
FE_TP_FN(P_LRATE) FE_TP_FN(P_LWAVE) FE_TP_FN(P_LPHASE) FE_TP_FN(P_LFADE)
FE_TP_FN(P_LD_PIT) FE_TP_FN(P_LD_FLT) FE_TP_FN(P_LD_SHP) FE_TP_FN(P_LD_AMP)
FE_TP_FN(P_AMODE) FE_TP_FN(P_ARATE) FE_TP_FN(P_AOCT) FE_TP_FN(P_AGATE)
FE_TP_FN(P_ASWING) FE_TP_FN(P_APROB) FE_TP_FN(P_AHOLD) FE_TP_FN(P_AORDER)
FE_TP_FN(P_ROOT) FE_TP_FN(P_SCALE) FE_TP_FN(P_QUANT) FE_TP_FN(P_TRANS)
FE_TP_FN(P_SLEN) FE_TP_FN(P_SDIV) FE_TP_FN(P_SSWING) FE_TP_FN(P_SGATE)
FE_TP_FN(P_DIST) FE_TP_FN(P_CHOR) FE_TP_FN(P_DLY) FE_TP_FN(P_REV)
FE_TP_FN(P_VOICE) FE_TP_FN(P_GLIDE) FE_TP_FN(P_PAN) FE_TP_FN(P_MUTE)
FE_TP_FN(P_GLMODE) FE_TP_FN(P_PRIO) FE_TP_FN(P_ALLOC) FE_TP_FN(P_DETUNE)
FE_TP_FN(P_M1SRC) FE_TP_FN(P_M1DST) FE_TP_FN(P_M1AMT) FE_TP_FN(P_M2SRC) FE_TP_FN(P_M2DST) FE_TP_FN(P_M2AMT)
FE_TP_FN(P_CHRD) FE_TP_FN(P_VOIC)
FE_GP_FN(G_BPM) FE_GP_FN(G_SWING) FE_GP_FN(G_TUNE)
FE_GP_FN(G_DTIME) FE_GP_FN(G_DFDBK) FE_GP_FN(G_DCOLOR) FE_GP_FN(G_DMIX)
FE_GP_FN(G_RTYPE) FE_GP_FN(G_RSIZE) FE_GP_FN(G_RDAMP) FE_GP_FN(G_CRATE) FE_GP_FN(G_CDEPTH)

/* ---- the part selection (ui.c track_select: the ALGORITHM knob's, the editor's) */
static const char *const FE_PARTS[NTRK] = {"1", "2", "3", "4"};
static int32_t fe_g_part(void) { return song.sel; }
static void fe_s_part(int32_t v) { track_select((uint32_t)fe_clamp(v, 0, NTRK - 1)); }
static void fe_t_part(int32_t v, char *b, uint32_t n) { snprintf(b, n, "Part %d", (int)fe_clamp(v, 0, NTRK - 1) + 1); }

/* ---- the engine entries E1..E8: a snapshot of the selected part's engine descriptors (double-buffered for text) */
static param_desc_t fe_ed[2][8];
static char fe_ednm[2][8][24];
static volatile uint32_t fe_edcur;
static void fe_ed_snap(uint32_t b)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = TSEL ? track_desc(TSEL, P_E0 + k) : &ENGINES[0]->edit[k];
        fe_ed[b][k] = *d;                         /* (a desc hook may hand out one static: copied at once) */
        snprintf(fe_ednm[b][k], sizeof fe_ednm[b][k], "E%u %s", (unsigned)k + 1u, d->label ? d->label : "-");
    }
}
#define FE_ED_FN(k) \
    static int32_t fe_g_e##k(void) { return fe_tget(P_E0 + k); } \
    static void fe_s_e##k(int32_t v) { fe_tset(P_E0 + k, v); } \
    static void fe_t_e##k(int32_t v, char *b, uint32_t n) { fe_fmt(&fe_ed[fe_edcur & 1u][k], v, b, n); } \
    static int32_t fe_mt_e##k(void);
FE_ED_FN(0) FE_ED_FN(1) FE_ED_FN(2) FE_ED_FN(3) FE_ED_FN(4) FE_ED_FN(5) FE_ED_FN(6) FE_ED_FN(7)

/* ---- the knobs' own targets (FM1P_HIDDEN: no host slot of their own; knob_target names them) */
/* FX held: the performance effects' knob macros (perform.c perf_k: FILTER -100..100, CRUSH THROW DEPTH 0..100; not
 * recorded, as the knob) */
#define FE_FXM_FN(k) \
    static int32_t fe_g_fxm##k(void) { return perf_k[k]; } \
    static void fe_s_fxm##k(int32_t v) { perf_k[k] = (int8_t)fe_clamp(v, k ? 0 : -100, 100); }
FE_FXM_FN(0) FE_FXM_FN(1) FE_FXM_FN(2) FE_FXM_FN(3)
static void fe_t_fxm(int32_t v, char *b, uint32_t n) { snprintf(b, n, "%d", (int)v); }
/* GLO held: T1..T4 LEVEL (ui_layer.c layer_knob: the part's level, recorded as on MIXER) */
#define FE_TLV_FN(k) \
    static int32_t fe_g_tlv##k(void) { return trk[k].p[P_LEVEL]; } \
    static void fe_s_tlv##k(int32_t v) \
    { \
        trk[k].p[P_LEVEL] = (int16_t)fe_clamp(v, TP[P_LEVEL].min, TP[P_LEVEL].max); \
        motion_capture(&trk[k], P_LEVEL, trk[k].p[P_LEVEL]); \
    }
FE_TLV_FN(0) FE_TLV_FN(1) FE_TLV_FN(2) FE_TLV_FN(3)
/* Knob 1..4: the page cell under KNOB k when no named entry is that parameter (an OP ENV page, SLICER, GLOBAL's
 * CLOCK, SYSTEM, the MOD matrix's slots 3 / 4 ..): the cell (page, column; page 0: HOME) is the epoch's, its
 * descriptor double-buffered for text(), written as edit_param writes it (the value given instead of the detents) */
typedef struct { const page_t *pg; uint8_t slot; } fe_cell_t;
static fe_cell_t fe_kc[4];
static param_desc_t fe_kd[2][4];
static char fe_knm[2][4][24];
static volatile uint32_t fe_kcur;
static const param_desc_t *fe_cell_desc(const fe_cell_t *c, int16_t **vp)
{
    *vp = 0;
    return c->pg ? page_desc(c->pg, c->slot, vp) : home_param(c->slot, vp);
}
static int fe_in_trk(const int16_t *vp) { return vp >= TSEL->p && vp < TSEL->p + P_COUNT; }
static int32_t fe_kc_get(uint32_t k)
{
    int16_t *vp;
    fe_cell_desc(&fe_kc[k & 3u], &vp);
    return vp ? *vp : 0;
}
static void fe_kc_set(uint32_t k, int32_t v)
{
    const fe_cell_t *c = &fe_kc[k & 3u];
    const page_t *pg = c->pg;
    int16_t *vp;
    const param_desc_t *d = fe_cell_desc(c, &vp);
    if (!d || !vp || d->max <= d->min)
        return;
    if (pg && chain_busy() && pg->graph == GR_STEPS)
        return;                                   /* the knob: "STOP TO EDIT" */
    v = enum_orig(d, fe_clamp(v, d->min, d->max));
    *vp = (int16_t)v;
#if FE_MELODEE                                    /* edit_param's write-back of the copies (Melodee) */
    if (pg && pg->scope == SC_CZ1) {
        uint8_t raw[CZ_BYTES];
        uint32_t tr = song.sel % NTRK;
        if (cz_ed_put(tr, pg->id[c->slot], (uint32_t)v, raw)) {
            cz_compare_take(tr);
            fm1_irq_off();
            memcpy(cz_patch[tr].raw, raw, 128u);
            fm1_irq_on();
        }
        return;
    }
    if (pg && (pg->scope == SC_FM6 || pg->scope == SC_FMOP)) {
        fm6_page_put(pg, c->slot, v);
        return;
    }
    if (pg && pg->scope == SC_GLOBAL && pg->id[c->slot] == G_BOOT) {
        settings_boot = (uint8_t)v;
        settings_save();
        return;
    }
    if (pg && pg->scope == SC_GLOBAL && pg->id[c->slot] == G_DRUMCH) {
        settings_drumch = (uint8_t)v;
        settings_save();
        return;
    }
#endif
    if ((!pg || pg->scope != SC_GLOBAL) && fe_in_trk(vp))
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
#if FE_MELODEE
    if (pg && pg->scope == SC_TRACK && fe_in_trk(vp) && scale_shared((uint32_t)(vp - TSEL->p)))
        scale_share(TSEL);
#endif
}
#define FE_KC_FN(k) \
    static int32_t fe_g_kc##k(void) { return fe_kc_get(k); } \
    static void fe_s_kc##k(int32_t v) { fe_kc_set(k, v); } \
    static void fe_t_kc##k(int32_t v, char *b, uint32_t n) { fe_fmt(&fe_kd[fe_kcur & 1u][k], v, b, n); }
FE_KC_FN(0) FE_KC_FN(1) FE_KC_FN(2) FE_KC_FN(3)

enum { FEK_TP, FEK_GP, FEK_ED, FEK_LIT, FEK_FXM, FEK_TLV, FEK_KNOB };
#define S_ FM1P_SOUND
#define FE_TP(X, nm, id, fl, pa) X(nm, FEK_TP, id, fe_g_##id, fe_s_##id, fe_t_##id, 0, fl, pa)
#define FE_GPF(X, nm, id, fl, pa) X(nm, FEK_GP, id, fe_g_##id, fe_s_##id, fe_t_##id, 0, fl, pa)
#define FE_GP(X, nm, id, pa) FE_GPF(X, nm, id, 0, pa)
#define FE_ED(X, k) X("E" #k, FEK_ED, k, fe_g_e##k, fe_s_e##k, fe_t_e##k, fe_mt_e##k, FM1P_META | S_, \
                      "edit: the part's P_E" #k " (EDIT page knob write, engine range)")
#define H_ FM1P_HIDDEN
#define FE_FXM(X, k, nm) X(nm, FEK_FXM, k, fe_g_fxm##k, fe_s_fxm##k, fe_t_fxm, 0, H_, \
                       "layer: FX held, its knob macro (perform.c perf_k)")
#define FE_TLV(X, k, nm) X(nm, FEK_TLV, k, fe_g_tlv##k, fe_s_tlv##k, fe_t_P_LEVEL, 0, H_, \
                           "layer: GLO held, that part's level + motion_capture")
#define FE_KC(X, k, nm) X(nm, FEK_KNOB, k, fe_g_kc##k, fe_s_kc##k, fe_t_kc##k, 0, S_ | H_, \
                          "knob: the page cell under that knob (edit_param's write)")
#define FE_KNOB "page: edit_param's knob write + motion_capture"
#define FE_GKNOB "page: edit_param's knob write (the song's)"

#define FE_LIST(X) \
    /* ---- page 1: the part's level and sends (the eight physical knobs come first in the host: knob_target) */ \
    FE_TP(X, "Level", P_LEVEL, 0, "mixer: tracks_edit's LEVEL write") \
    FE_TP(X, "Drive", P_DIST, S_, FE_KNOB " (FX)") FE_TP(X, "Delay Send", P_DLY, S_, FE_KNOB " (FX)") \
    FE_TP(X, "Reverb Send", P_REV, S_, FE_KNOB " (FX)") \
    /* ---- page 2: ENV, ENV DEST, pan */ \
    FE_TP(X, "Attack", P_ATK, S_, FE_KNOB " (ENV)") FE_TP(X, "Decay", P_DEC, S_, FE_KNOB " (ENV)") \
    FE_TP(X, "Sustain", P_SUS, S_, FE_KNOB " (ENV)") FE_TP(X, "Release", P_REL, S_, FE_KNOB " (ENV)") \
    FE_TP(X, "Env>Filter", P_ED_FLT, S_, FE_KNOB " (ENV DEST)") FE_TP(X, "Env>Pitch", P_ED_PIT, S_, FE_KNOB " (ENV DEST)") \
    FE_TP(X, "Env>Shape", P_ED_SHP, S_, FE_KNOB " (ENV DEST)") FE_TP(X, "Pan", P_PAN, 0, "mixer: tracks_edit's PAN write") \
    /* ---- page 3: the engine (EDIT 1, EDIT 2) */ \
    FE_ED(X, 0) FE_ED(X, 1) FE_ED(X, 2) FE_ED(X, 3) FE_ED(X, 4) FE_ED(X, 5) FE_ED(X, 6) FE_ED(X, 7) \
    /* ---- page 4: LFO, LFO DEST */ \
    FE_TP(X, "LFO Rate", P_LRATE, S_, FE_KNOB " (LFO)") FE_TP(X, "LFO Wave", P_LWAVE, S_, FE_KNOB " (LFO)") \
    FE_TP(X, "LFO Phase", P_LPHASE, S_, FE_KNOB " (LFO)") FE_TP(X, "LFO Fade", P_LFADE, S_, FE_KNOB " (LFO)") \
    FE_TP(X, "LFO>Pitch", P_LD_PIT, S_, FE_KNOB " (LFO DEST)") FE_TP(X, "LFO>Filter", P_LD_FLT, S_, FE_KNOB " (LFO DEST)") \
    FE_TP(X, "LFO>Shape", P_LD_SHP, S_, FE_KNOB " (LFO DEST)") FE_TP(X, "LFO>Amp", P_LD_AMP, S_, FE_KNOB " (LFO DEST)") \
    /* ---- page 5: chorus send, VOICE, VOICE 2, mute */ \
    FE_TP(X, "Chorus Send", P_CHOR, S_, FE_KNOB " (FX)") FE_TP(X, "Voice Mode", P_VOICE, S_, FE_KNOB " (VOICE)") \
    FE_TP(X, "Glide", P_GLIDE, S_, FE_KNOB " (VOICE)") FE_TP(X, "Glide Mode", P_GLMODE, S_, FE_KNOB " (VOICE)") \
    FE_TP(X, "Priority", P_PRIO, S_, FE_KNOB " (VOICE)") FE_TP(X, "Allocation", P_ALLOC, S_, FE_KNOB " (VOICE 2)") \
    FE_TP(X, "Detune", P_DETUNE, S_, FE_KNOB " (VOICE 2)") FE_TP(X, "Mute", P_MUTE, 0, "mixer: tracks_edit's MUTE write") \
    /* ---- page 6: ARP, ARP 2 */ \
    FE_TP(X, "Arp Mode", P_AMODE, 0, FE_KNOB " (ARP)") FE_TP(X, "Arp Rate", P_ARATE, 0, FE_KNOB " (ARP)") \
    FE_TP(X, "Arp Octaves", P_AOCT, 0, FE_KNOB " (ARP)") FE_TP(X, "Arp Gate", P_AGATE, 0, FE_KNOB " (ARP)") \
    FE_TP(X, "Arp Swing", P_ASWING, 0, FE_KNOB " (ARP 2)") FE_TP(X, "Arp Chance", P_APROB, 0, FE_KNOB " (ARP 2)") \
    FE_TP(X, "Arp Hold", P_AHOLD, 0, FE_KNOB " (ARP 2)") FE_TP(X, "Arp Order", P_AORDER, 0, FE_KNOB " (ARP 2)") \
    /* ---- page 7: SCL, CHORD, the part, the tempo */ \
    FE_TP(X, "Scale Root", P_ROOT, 0, FE_KNOB " (SCL)") FE_TP(X, "Scale", P_SCALE, 0, FE_KNOB " (SCL)") \
    FE_TP(X, "Quantize", P_QUANT, 0, FE_KNOB " (SCL)") FE_TP(X, "Transpose", P_TRANS, 0, FE_KNOB " (SCL)") \
    FE_TP(X, "Chord", P_CHRD, 0, FE_KNOB " (CHORD)") FE_TP(X, "Chord Voicing", P_VOIC, 0, FE_KNOB " (CHORD)") \
    X("Part", FEK_LIT, 0, fe_g_part, fe_s_part, fe_t_part, 0, 0, "track_select (the ALGORITHM knob's)") \
    FE_GP(X, "Tempo", G_BPM, FE_GKNOB " (GLOBAL)") \
    /* ---- page 8 / 9: GLOBAL, the FX buses (DLY, REVERB, CHORUS), PATTERN */ \
    FE_GP(X, "Swing", G_SWING, FE_GKNOB " (GLOBAL)") FE_GP(X, "Tune", G_TUNE, FE_GKNOB " (GLOBAL)") \
    FE_GP(X, "Delay Time", G_DTIME, FE_GKNOB " (DLY)") FE_GP(X, "Delay Feedback", G_DFDBK, FE_GKNOB " (DLY)") \
    FE_GP(X, "Delay Colour", G_DCOLOR, FE_GKNOB " (DLY)") FE_GP(X, "Delay Mix", G_DMIX, FE_GKNOB " (DLY)") \
    FE_GP(X, "Reverb Type", G_RTYPE, FE_GKNOB " (REVERB)") FE_GP(X, "Reverb Size", G_RSIZE, FE_GKNOB " (REVERB)") \
    FE_GP(X, "Reverb Damp", G_RDAMP, FE_GKNOB " (REVERB)") FE_GP(X, "Chorus Rate", G_CRATE, FE_GKNOB " (CHORUS)") \
    FE_GP(X, "Chorus Depth", G_CDEPTH, FE_GKNOB " (CHORUS)") \
    FE_TP(X, "Pattern Length", P_SLEN, 0, FE_KNOB " (PATTERN), not while a song plays") \
    FE_TP(X, "Pattern Div", P_SDIV, 0, FE_KNOB " (PATTERN), not while a song plays") \
    FE_TP(X, "Pattern Swing", P_SSWING, 0, FE_KNOB " (PATTERN), not while a song plays") \
    FE_TP(X, "Pattern Gate", P_SGATE, 0, FE_KNOB " (PATTERN), not while a song plays") \
    /* ---- the modulation matrix's first two slots (MOD) */ \
    FE_TP(X, "Mod 1 Source", P_M1SRC, S_, FE_KNOB " (MOD)") FE_TP(X, "Mod 1 Dest", P_M1DST, S_, FE_KNOB " (MOD)") \
    FE_TP(X, "Mod 1 Amount", P_M1AMT, S_, FE_KNOB " (MOD)") FE_TP(X, "Mod 2 Source", P_M2SRC, S_, FE_KNOB " (MOD)") \
    FE_TP(X, "Mod 2 Dest", P_M2DST, S_, FE_KNOB " (MOD)") FE_TP(X, "Mod 2 Amount", P_M2AMT, S_, FE_KNOB " (MOD)") \
    /* ---- the knobs' own targets (FM1P_HIDDEN) */ \
    FE_FXM(X, 0, "FX Filter") FE_FXM(X, 1, "FX Crush") FE_FXM(X, 2, "FX Throw") FE_FXM(X, 3, "FX Depth") \
    FE_TLV(X, 0, "T1 Level") FE_TLV(X, 1, "T2 Level") FE_TLV(X, 2, "T3 Level") FE_TLV(X, 3, "T4 Level") \
    FE_KC(X, 0, "Knob 1") FE_KC(X, 1, "Knob 2") FE_KC(X, 2, "Knob 3") FE_KC(X, 3, "Knob 4")

#define FE_AS_PARAM(nm, kind, a, g, s, t, mt, fl, pa) {nm, 0, 0, 0, 0, "", g, s, t, mt, fl, pa},
#define FE_AS_SRC(nm, kind, a, ...) {kind, a},
static fm1param_t FE_PARAMS[] = {FE_LIST(FE_AS_PARAM)};
static const struct { uint8_t kind; int16_t a; } FE_SRC[] = {FE_LIST(FE_AS_SRC)};
#define FE_NPARAMS ((uint32_t)(sizeof FE_PARAMS / sizeof FE_PARAMS[0]))

static int32_t fe_index_of(uint32_t kind, int32_t a)
{
    uint32_t i;
    for (i = 0; i < FE_NPARAMS; i++)
        if (FE_SRC[i].kind == kind && FE_SRC[i].a == a)
            return (int32_t)i;
    return -1;
}
/* the map entry of a part parameter id: an engine entry for P_E0.., else the TP entry, -1 none */
static int32_t fe_entry_of(uint32_t id)
{
    return id >= P_E0 ? fe_index_of(FEK_ED, (int32_t)(id - P_E0)) : fe_index_of(FEK_TP, (int32_t)id);
}
#define FE_MT_E(k) static int32_t fe_mt_e##k(void) { return fe_index_of(FEK_ED, k); }   /* itself */
FE_MT_E(0) FE_MT_E(1) FE_MT_E(2) FE_MT_E(3) FE_MT_E(4) FE_MT_E(5) FE_MT_E(6) FE_MT_E(7)

/* an engine entry's descriptor from snapshot buffer b */
static void fe_ed_apply(uint32_t b)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) {
        fm1param_t *p = &FE_PARAMS[fe_index_of(FEK_ED, (int32_t)k)];
        const param_desc_t *d = &fe_ed[b][k];
        p->name = fe_ednm[b][k];
        p->min = d->min;
        p->max = d->max;
        p->def = fe_clamp(d->def, d->min, d->max);
        p->names = d->fmt == F_ENUM ? d->names : 0;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (d->fmt == F_ENUM ? FM1P_ENUM : 0u);
    }
}
/* the ranges, defaults and names from the firmware's tables (once, when the descriptor is first asked for: before
 * the power-on, so the engine entries start as the power-on's part 1 engine, ANALOG; the first epoch corrects them) */
static int fe_params_ready;
static uint32_t fe_epoch = 1, fe_sig_sel = 0xFFFFFFFFu, fe_sig_eng = 0xFFFFFFFFu;
static uint32_t fe_ed_sig(uint32_t b)            /* a signature of snapshot b (labels, ranges, names) */
{
    uint32_t k, h = 2166136261u;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = &fe_ed[b][k];
        const char *s;
        for (s = d->label ? d->label : ""; *s; s++)
            h = (h ^ (uint8_t)*s) * 16777619u;
        h = (h ^ (uint32_t)(uint16_t)d->min) * 16777619u;
        h = (h ^ (uint32_t)(uint16_t)d->max) * 16777619u;
        h = (h ^ (uint32_t)d->fmt) * 16777619u;
        h = (h ^ (uint32_t)(uintptr_t)d->names) * 16777619u;
    }
    return h;
}
static uint32_t fe_cur_sig;
static void fe_params_init(void)
{
    uint32_t i, k;
    if (fe_params_ready)
        return;
    for (i = 0; i < FE_NPARAMS; i++) {
        fm1param_t *p = &FE_PARAMS[i];
        const param_desc_t *d = 0;
        int32_t a = FE_SRC[i].a;
        switch (FE_SRC[i].kind) {
        case FEK_TP: d = &TP[a]; break;
        case FEK_GP: d = &GP[a]; break;
        case FEK_LIT:                             /* Part */
            p->min = 0;
            p->max = NTRK - 1;
            p->def = 0;
            p->names = FE_PARTS;
            p->flags |= FM1P_ENUM;
            break;
        case FEK_FXM:
            p->min = a ? 0 : -100;
            p->max = 100;
            p->def = 0;
            break;
        case FEK_TLV:
            d = &TP[P_LEVEL];
            break;
        case FEK_KNOB:                            /* (the knob cells: before the first epoch, ENV's) */
            d = &TP[a == 0 ? P_ATK : a == 1 ? P_DEC : a == 2 ? P_SUS : P_REL];
            fe_kc[a].pg = &PAGES[0];
            fe_kc[a].slot = (uint8_t)a;
            fe_kd[0][a] = fe_kd[1][a] = *d;
            snprintf(fe_knm[0][a], sizeof fe_knm[0][a], "Knob %d", (int)a + 1);
            memcpy(fe_knm[1][a], fe_knm[0][a], sizeof fe_knm[1][a]);
            p->name = fe_knm[0][a];
            break;
        default: break;
        }
        if (d) {
            p->min = d->min;
            p->max = d->max;
            p->def = d->def;
            if (d->fmt == F_ENUM) {
                p->names = d->names;
                p->flags |= FM1P_ENUM;
            }
        }
    }
    for (k = 0; k < 8u; k++) {                    /* ENGINES[0] (ANALOG): a zeroed track's engine */
        fe_ed[0][k] = ENGINES[0]->edit[k];
        snprintf(fe_ednm[0][k], sizeof fe_ednm[0][k], "E%u %s", (unsigned)k + 1u, fe_ed[0][k].label);
    }
    fe_edcur = 0;
    fe_cur_sig = fe_ed_sig(0);
    fe_ed_apply(0);
    fe_params_ready = 1;
}

/* ============================================================ the knobs == */
/* knob_target(role): what each physical knob turns on the screen showing now, ui_input's dispatch read without
 * turning (the same tests in the same order; the table in the file's header). Evaluated by param_epoch(). */
static fe_cell_t fe_kc_new[4];
static param_desc_t fe_kd_new[4];
static char fe_knm_new[4][24];
static int32_t fe_cell_new(const page_t *pg, uint32_t k, const param_desc_t *d)
{
    k &= 3u;
    fe_kc_new[k].pg = pg;
    fe_kc_new[k].slot = (uint8_t)k;
    fe_kd_new[k] = *d;                            /* (a desc hook may hand out one static: copied at once) */
    snprintf(fe_knm_new[k], 17, "%s %s", pg ? pg->title : "HOME", d->label ? d->label : "-");
    return fe_index_of(FEK_KNOB, (int32_t)k);
}
/* a named entry for what vp points at (the selected part's parameter or the song's), else the knob's own cell */
static int32_t fe_cell_target(const page_t *pg, uint32_t k, const param_desc_t *d, int16_t *vp)
{
    int32_t e = -1;
    if (fe_in_trk(vp))
        e = fe_entry_of((uint32_t)(vp - TSEL->p));
    else if (vp >= song.g && vp < song.g + G_COUNT)
        e = fe_index_of(FEK_GP, (int32_t)(vp - song.g));
    return e >= 0 ? e : fe_cell_new(pg, k, d);
}
static int32_t fe_page_target(const page_t *pg, uint32_t k, int layer)   /* edit_param(k, ..) on page pg */
{
    static const uint8_t MIXER[4] = {P_LEVEL, P_PAN, P_REV, P_MUTE};   /* tracks_edit */
    int16_t *vp;
    const param_desc_t *d;
    k &= 3u;
    if (pg->graph == GR_CHANCE || pg->graph == GR_MOTION || pg->graph == GR_SONG || pg->scope == SC_STEP ||
        pg->graph == GR_BROWSE || pg->graph == GR_USER || pg->graph == GR_PATS || (pg->graph == GR_MOD && !k))
        return -1;                                /* a cursor, a row, a slot, a preset list: turned */
#if FE_SLICE
    if (pg->graph == GR_SLICES && k < 2u)
        return -1;
#endif
#if FE_MELODEE
    if (pg->graph == GR_CZTOOLS)
        return -1;
#endif
    if (pg->scope == SC_TRK)
        return fe_entry_of(MIXER[k]);
    if (!layer && ((act_cols() >> k) & 1u))
        return -1;                                /* an action: picked, OCT+ does it */
    d = page_desc(pg, k, &vp);
    if (!d || !vp || d->max == d->min)
        return -1;
    return fe_cell_target(pg, k, d, vp);
}
static int32_t fe_knob_compute(uint32_t role)
{
    int16_t *vp;
    const param_desc_t *d;
    uint32_t l;
    if (role >= NE || ui.menu || ui.confirm || name_on())
        return -1;                                /* the menu, a dialog, NAME: their own (turned) */
    if (layer_held()) {                           /* a layer's button held: KNOB 1..4 are the layer's */
        l = ui.ly % LAYER_N;
        if (role == EN_SELECT)
            return FE_SELECT_TARGET;
        if (role < EN_K1)
            return -1;                            /* (PRESETS / ALGORITHM: swallowed) */
        if (l == LAYER_FX)
            return fe_index_of(FEK_FXM, (int32_t)(role - EN_K1));
        if (l == LAYER_GLO)
            return fe_index_of(FEK_TLV, (int32_t)(role - EN_K1));
        if (l == LAYER_EDIT)
            return -1;                            /* ENG, No., FAV: lists */
        return fe_page_target(l == LAYER_SCL ? &LY_SCL : &PAGES[page_first(LAYERS[l].fam)], role - EN_K1, 1);
    }
    switch (role) {
    case EN_SELECT: return FE_SELECT_TARGET;
    case EN_ALGO: return fe_index_of(FEK_LIT, 0);   /* the selected part, on every page */
    case EN_PRESET: return -1;                    /* the sounds (HOME, PRESETS): a list */
    default: break;
    }
    if (ui.home) {
        d = home_param(role - EN_K1, &vp);
        return d && vp && d->max > d->min ? fe_cell_target(0, role - EN_K1, d, vp) : -1;
    }
    return fe_page_target(cur_page(), role - EN_K1, 0);
}

static int32_t fe_kt[EMU_NE] = {-1, -1, -1, -1, -1, -1, -1, -1};
static uint32_t fe_ksig;
static int fe_ksig_valid;
static uint32_t fe_fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
/* the knobs' targets now; 1: they (or a rewritten cell) changed, committed */
static int fe_knobs_update(void)
{
    int32_t t[EMU_NE], kc0 = fe_index_of(FEK_KNOB, 0);
    uint32_t r, k, b, h = 2166136261u;
    for (r = 0; r < EMU_NE; r++) {
        t[r] = fe_knob_compute(r);
        h = fe_fnv(h, (uint32_t)t[r]);
        if (t[r] >= kc0 && t[r] < kc0 + 4) {
            const char *s;
            k = (uint32_t)(t[r] - kc0);
            h = fe_fnv(fe_fnv(h, (uint32_t)(uintptr_t)fe_kc_new[k].pg), fe_kc_new[k].slot);
            h = fe_fnv(fe_fnv(h, (uint32_t)(uint16_t)fe_kd_new[k].min), (uint32_t)(uint16_t)fe_kd_new[k].max);
            h = fe_fnv(fe_fnv(h, fe_kd_new[k].fmt), (uint32_t)(uintptr_t)fe_kd_new[k].names);
            for (s = fe_knm_new[k]; *s; s++)
                h = fe_fnv(h, (uint8_t)*s);
        }
    }
    if (fe_ksig_valid && h == fe_ksig)
        return 0;
    b = (fe_kcur & 1u) ^ 1u;                      /* the other buffer: the current one, then what changed */
    for (k = 0; k < 4u; k++) {
        fe_kd[b][k] = fe_kd[b ^ 1u][k];
        memcpy(fe_knm[b][k], fe_knm[b ^ 1u][k], sizeof fe_knm[b][k]);
    }
    for (r = 0; r < EMU_NE; r++)
        if (t[r] >= kc0 && t[r] < kc0 + 4) {
            k = (uint32_t)(t[r] - kc0);
            fe_kc[k] = fe_kc_new[k];
            fe_kd[b][k] = fe_kd_new[k];
            memcpy(fe_knm[b][k], fe_knm_new[k], sizeof fe_knm[b][k]);
        }
    for (k = 0; k < 4u; k++) {                    /* the entries' own fields: the buffer's */
        fm1param_t *p = &FE_PARAMS[kc0 + (int32_t)k];
        const param_desc_t *d = &fe_kd[b][k];
        p->name = fe_knm[b][k];
        p->min = d->min;
        p->max = d->max > d->min ? d->max : d->min + 1;
        p->def = fe_clamp(d->def, p->min, p->max);
        p->names = d->fmt == F_ENUM ? d->names : 0;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (d->fmt == F_ENUM ? FM1P_ENUM : 0u);
    }
    fe_kcur = b;
    for (r = 0; r < EMU_NE; r++)
        fe_kt[r] = t[r];
    fe_ksig = h;
    fe_ksig_valid = 1;
    return 1;
}

/* the epoch: the selected part, its engine, or its engine descriptors changed -> the E entries rewritten; any
 * knob's target (or the cell a Knob entry stands for) changed -> knob_target re-read; the counter moved (the host
 * relabels) */
static uint32_t fe_param_epoch(void)
{
    uint32_t b, sig, moved = 0;
    if (emu_hal.ready != 1u)
        return fe_epoch;                          /* (before the power-on: the tables as initialised) */
    b = (fe_edcur & 1u) ^ 1u;
    fe_ed_snap(b);
    sig = fe_ed_sig(b);
    if (sig != fe_cur_sig || song.sel != fe_sig_sel || TSEL->eng_req != fe_sig_eng) {
        fe_edcur = b;                             /* (text() now reads the new snapshot) */
        fe_cur_sig = sig;
        fe_sig_sel = song.sel;
        fe_sig_eng = TSEL->eng_req;
        fe_ed_apply(b);
        moved = 1;
    }
    if (fe_knobs_update())
        moved = 1;
    if (moved)
        fe_epoch++;
    return fe_epoch;
}

/* for the descriptor (core_felucca_desc.c) */
const fm1param_t *fm1core_fe_params(uint32_t *n)
{
    fe_params_init();
    *n = FE_NPARAMS;
    return FE_PARAMS;
}
uint32_t fm1core_fe_param_epoch(void) { return fe_param_epoch(); }
int32_t fm1core_fe_knob_target(int role) { return role >= 0 && role < EMU_NE ? fe_kt[role] : -1; }
#undef S_
#undef H_
