/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core's Tier 2 parameter map (FM1-VST-PLAN.md 4.5; core-api/fm1core.h fm1param_t).
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_melodee.c includes this file after the firmware unit (melodee_fw.c), so
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
 * while the part records), and Melodee's scale_share (SCALE and QUANT are set on every part at once). The audio ISR reads the arrays as they are (it is what the knob does). Pattern LEN / DIV
 * / SWING / GATE refuse while a song chain plays ("STOP TO EDIT", as the knob). Nothing here is in the settings
 * record but Tune (FM1P_PERSIST: Melodee keeps CLK TUNE MIDI ROUT as last used, project.c glo_poll); parts and song
 * are the project, saved by SAVE > PROJECT on the device.
 *
 * THE META ENTRIES (FM1P_META): "E1".."E8", the selected part's engine parameters (EDIT 1 / EDIT 2). Their range,
 *   names and label depend on the engine (FM6's ALG is 0..32, ANALOG's WAVE an enum of 5), so these entries target
 *   THEMSELVES and their descriptor (name "E1 WAVE", min, max, def, names, FM1P_ENUM) is rewritten from the engine's
 *   track_desc before the epoch moves; the host re-reads it then (Tier2Parameter::retarget).
 *
 * THE KNOBS (knob_target, ABI 4): what each physical knob turns on the screen showing now, ui_input's own dispatch
 * read without turning (me_knob_compute). "turn": no value there, the host turns it as a relative control. MASTER is
 * the pot everywhere.
 *   screen                     SELECT     PRESETS        ALGORITHM   KNOB1..4
 *   HOME                       turn       turn (sounds)  Part        the engine's four HOME knobs (home_param: ANALOG E5 CUT,
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
 *   (H): FM1P_HIDDEN, a knob target only. Melodee: SELECT turns pages and steps everywhere; the
 *   FM6 / CZ-1 pages' cells are the hidden Knob cells, written back as edit_param does (fm6_page_put, cz_ed_put).
 *   param_epoch moves when the selected part, its engine, one of its engine descriptors (a mode-dependent label,
 *   engine_t.desc), or any knob's target (or a rewritten cell) changed: the targets are computed afresh at every call.
 *
 * THREADS: get / set / target / param_epoch run on the clock's thread between device milliseconds; text() formats
 * from the constant tables (and the E entries' descriptor snapshot, double-buffered) and may run on any thread.
 * (param_format writes params.c's fmt_named, a flag only ui_draw.c's next card reads after its own param_format.) */

#define ME_MELODEE 1
#define ME_SLICE MELODEE_SLICE
#define ME_SELECT_TARGET -1   /* SELECT: pages and steps, relative (Melodee: BPM is SEQ > TEMPO) */

static int32_t me_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the firmware's value text: param_format's value and unit ("355 Hz", "1/8", "+12 st", "OFF") */
static void me_fmt(const param_desc_t *d, int32_t v, char *b, uint32_t n)
{
    char val[24];
    const char *unit = "";
    val[0] = 0;
    param_format(d, v, val, &unit);
    snprintf(b, n, unit && unit[0] ? "%s %s" : "%s", val, unit ? unit : "");
}

/* ---- a parameter of the selected part (TP id, or P_E0.. through the engine's descriptor) */
static int32_t me_tget(uint32_t id) { return TSEL->p[id % P_COUNT]; }
static void me_tset(uint32_t id, int32_t v)
{
    track_t *t = TSEL;
    const param_desc_t *d;
    id %= P_COUNT;
    d = track_desc(t, id);
    if (d->max <= d->min)
        return;                                   /* (an empty column: "-") */
    if (chain_busy() && id >= P_SLEN && id <= P_SGATE)
        return;                                   /* the knob: "STOP TO EDIT" */
    t->p[id] = (int16_t)enum_orig(d, me_clamp(v, d->min, d->max));
    motion_capture(t, id, t->p[id]);
    if (scale_shared(id))                         /* Melodee: SCALE / QUANT are the song's, every part's */
        scale_share(t);
}
/* ---- a song parameter (GP id) */
static int32_t me_gget(uint32_t id) { return song.g[id % G_COUNT]; }
static void me_gset(uint32_t id, int32_t v)
{
    const param_desc_t *d = &GP[id % G_COUNT];
    if (d->max > d->min)
        song.g[id % G_COUNT] = (int16_t)me_clamp(v, d->min, d->max);
}

#define ME_TP_FN(id) \
    static int32_t me_g_##id(void) { return me_tget(id); } \
    static void me_s_##id(int32_t v) { me_tset(id, v); } \
    static void me_t_##id(int32_t v, char *b, uint32_t n) { me_fmt(&TP[id], v, b, n); }
#define ME_GP_FN(id) \
    static int32_t me_g_##id(void) { return me_gget(id); } \
    static void me_s_##id(int32_t v) { me_gset(id, v); } \
    static void me_t_##id(int32_t v, char *b, uint32_t n) { me_fmt(&GP[id], v, b, n); }
ME_TP_FN(P_LEVEL) ME_TP_FN(P_ATK) ME_TP_FN(P_DEC) ME_TP_FN(P_SUS) ME_TP_FN(P_REL)
ME_TP_FN(P_ED_FLT) ME_TP_FN(P_ED_PIT) ME_TP_FN(P_ED_SHP)
ME_TP_FN(P_LRATE) ME_TP_FN(P_LWAVE) ME_TP_FN(P_LPHASE) ME_TP_FN(P_LFADE)
ME_TP_FN(P_LD_PIT) ME_TP_FN(P_LD_FLT) ME_TP_FN(P_LD_SHP) ME_TP_FN(P_LD_AMP)
ME_TP_FN(P_AMODE) ME_TP_FN(P_ARATE) ME_TP_FN(P_AOCT) ME_TP_FN(P_AGATE)
ME_TP_FN(P_ASWING) ME_TP_FN(P_APROB) ME_TP_FN(P_AHOLD) ME_TP_FN(P_AORDER)
ME_TP_FN(P_ROOT) ME_TP_FN(P_SCALE) ME_TP_FN(P_QUANT) ME_TP_FN(P_TRANS)
ME_TP_FN(P_SLEN) ME_TP_FN(P_SDIV) ME_TP_FN(P_SSWING) ME_TP_FN(P_SGATE)
ME_TP_FN(P_DIST) ME_TP_FN(P_CHOR) ME_TP_FN(P_DLY) ME_TP_FN(P_REV)
ME_TP_FN(P_VOICE) ME_TP_FN(P_GLIDE) ME_TP_FN(P_PAN) ME_TP_FN(P_MUTE)
ME_TP_FN(P_GLMODE) ME_TP_FN(P_PRIO) ME_TP_FN(P_ALLOC) ME_TP_FN(P_DETUNE)
ME_TP_FN(P_M1SRC) ME_TP_FN(P_M1DST) ME_TP_FN(P_M1AMT) ME_TP_FN(P_M2SRC) ME_TP_FN(P_M2DST) ME_TP_FN(P_M2AMT)
ME_TP_FN(P_CHRD) ME_TP_FN(P_VOIC)
ME_GP_FN(G_BPM) ME_GP_FN(G_SWING) ME_GP_FN(G_TUNE)
ME_GP_FN(G_DTIME) ME_GP_FN(G_DFDBK) ME_GP_FN(G_DCOLOR) ME_GP_FN(G_DMIX)
ME_GP_FN(G_RTYPE) ME_GP_FN(G_RSIZE) ME_GP_FN(G_RDAMP) ME_GP_FN(G_CRATE) ME_GP_FN(G_CDEPTH)

/* ---- the part selection (ui.c track_select: the ALGORITHM knob's, the editor's) */
static const char *const ME_PARTS[NTRK] = {"1", "2", "3", "4"};
static int32_t me_g_part(void) { return song.sel; }
static void me_s_part(int32_t v) { track_select((uint32_t)me_clamp(v, 0, NTRK - 1)); }
static void me_t_part(int32_t v, char *b, uint32_t n) { snprintf(b, n, "Part %d", (int)me_clamp(v, 0, NTRK - 1) + 1); }

/* ---- the engine entries E1..E8: a snapshot of the selected part's engine descriptors (double-buffered for text) */
static param_desc_t me_ed[2][8];
static char me_ednm[2][8][24];
static volatile uint32_t me_edcur;
static void me_ed_snap(uint32_t b)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = TSEL ? track_desc(TSEL, P_E0 + k) : &ENGINES[0]->edit[k];
        me_ed[b][k] = *d;                         /* (a desc hook may hand out one static: copied at once) */
        snprintf(me_ednm[b][k], sizeof me_ednm[b][k], "E%u %s", (unsigned)k + 1u, d->label ? d->label : "-");
    }
}
#define ME_ED_FN(k) \
    static int32_t me_g_e##k(void) { return me_tget(P_E0 + k); } \
    static void me_s_e##k(int32_t v) { me_tset(P_E0 + k, v); } \
    static void me_t_e##k(int32_t v, char *b, uint32_t n) { me_fmt(&me_ed[me_edcur & 1u][k], v, b, n); } \
    static int32_t me_mt_e##k(void);
ME_ED_FN(0) ME_ED_FN(1) ME_ED_FN(2) ME_ED_FN(3) ME_ED_FN(4) ME_ED_FN(5) ME_ED_FN(6) ME_ED_FN(7)

/* ---- the knobs' own targets (FM1P_HIDDEN: no host slot of their own; knob_target names them) */
/* FX held: the performance effects' knob macros (perform.c perf_k: FILTER -100..100, CRUSH THROW DEPTH 0..100; not
 * recorded, as the knob) */
#define ME_FXM_FN(k) \
    static int32_t me_g_fxm##k(void) { return perf_k[k]; } \
    static void me_s_fxm##k(int32_t v) { perf_k[k] = (int8_t)me_clamp(v, k ? 0 : -100, 100); }
ME_FXM_FN(0) ME_FXM_FN(1) ME_FXM_FN(2) ME_FXM_FN(3)
static void me_t_fxm(int32_t v, char *b, uint32_t n) { snprintf(b, n, "%d", (int)v); }
/* GLO held: T1..T4 LEVEL (ui_layer.c layer_knob: the part's level, recorded as on MIXER) */
#define ME_TLV_FN(k) \
    static int32_t me_g_tlv##k(void) { return trk[k].p[P_LEVEL]; } \
    static void me_s_tlv##k(int32_t v) \
    { \
        trk[k].p[P_LEVEL] = (int16_t)me_clamp(v, TP[P_LEVEL].min, TP[P_LEVEL].max); \
        motion_capture(&trk[k], P_LEVEL, trk[k].p[P_LEVEL]); \
    }
ME_TLV_FN(0) ME_TLV_FN(1) ME_TLV_FN(2) ME_TLV_FN(3)
/* Knob 1..4: the page cell under KNOB k when no named entry is that parameter (an OP ENV page, SLICER, GLOBAL's
 * CLOCK, SYSTEM, the MOD matrix's slots 3 / 4 ..): the cell (page, column; page 0: HOME) is the epoch's, its
 * descriptor double-buffered for text(), written as edit_param writes it (the value given instead of the detents) */
typedef struct { const page_t *pg; uint8_t slot; } me_cell_t;
static me_cell_t me_kc[4];
static param_desc_t me_kd[2][4];
static char me_knm[2][4][24];
static volatile uint32_t me_kcur;
static const param_desc_t *me_cell_desc(const me_cell_t *c, int16_t **vp)
{
    *vp = 0;
    return c->pg ? page_desc(c->pg, c->slot, vp) : home_param(c->slot, vp);
}
static int me_in_trk(const int16_t *vp) { return vp >= TSEL->p && vp < TSEL->p + P_COUNT; }
static int32_t me_kc_get(uint32_t k)
{
    int16_t *vp;
    me_cell_desc(&me_kc[k & 3u], &vp);
    return vp ? *vp : 0;
}
static void me_kc_set(uint32_t k, int32_t v)
{
    const me_cell_t *c = &me_kc[k & 3u];
    const page_t *pg = c->pg;
    int16_t *vp;
    const param_desc_t *d = me_cell_desc(c, &vp);
    if (!d || !vp || d->max <= d->min)
        return;
    if (pg && chain_busy() && pg->graph == GR_STEPS)
        return;                                   /* the knob: "STOP TO EDIT" */
    v = enum_orig(d, me_clamp(v, d->min, d->max));
    *vp = (int16_t)v;
#if ME_MELODEE                                    /* edit_param's write-back of the copies (Melodee) */
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
    if ((!pg || pg->scope != SC_GLOBAL) && me_in_trk(vp))
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
#if ME_MELODEE
    if (pg && pg->scope == SC_TRACK && me_in_trk(vp) && scale_shared((uint32_t)(vp - TSEL->p)))
        scale_share(TSEL);
#endif
}
#define ME_KC_FN(k) \
    static int32_t me_g_kc##k(void) { return me_kc_get(k); } \
    static void me_s_kc##k(int32_t v) { me_kc_set(k, v); } \
    static void me_t_kc##k(int32_t v, char *b, uint32_t n) { me_fmt(&me_kd[me_kcur & 1u][k], v, b, n); }
ME_KC_FN(0) ME_KC_FN(1) ME_KC_FN(2) ME_KC_FN(3)

enum { MEK_TP, MEK_GP, MEK_ED, MEK_LIT, MEK_FXM, MEK_TLV, MEK_KNOB };
#define S_ FM1P_SOUND
#define ME_TP(X, nm, id, fl, pa) X(nm, MEK_TP, id, me_g_##id, me_s_##id, me_t_##id, 0, fl, pa)
#define ME_GPF(X, nm, id, fl, pa) X(nm, MEK_GP, id, me_g_##id, me_s_##id, me_t_##id, 0, fl, pa)
#define ME_GP(X, nm, id, pa) ME_GPF(X, nm, id, 0, pa)
#define ME_ED(X, k) X("E" #k, MEK_ED, k, me_g_e##k, me_s_e##k, me_t_e##k, me_mt_e##k, FM1P_META | S_, \
                      "edit: the part's P_E" #k " (EDIT page knob write, engine range)")
#define H_ FM1P_HIDDEN
#define ME_FXM(X, k, nm) X(nm, MEK_FXM, k, me_g_fxm##k, me_s_fxm##k, me_t_fxm, 0, H_, \
                       "layer: FX held, its knob macro (perform.c perf_k)")
#define ME_TLV(X, k, nm) X(nm, MEK_TLV, k, me_g_tlv##k, me_s_tlv##k, me_t_P_LEVEL, 0, H_, \
                           "layer: GLO held, that part's level + motion_capture")
#define ME_KC(X, k, nm) X(nm, MEK_KNOB, k, me_g_kc##k, me_s_kc##k, me_t_kc##k, 0, S_ | H_, \
                          "knob: the page cell under that knob (edit_param's write)")
#define ME_KNOB "page: edit_param's knob write + motion_capture"
#define ME_GKNOB "page: edit_param's knob write (the song's)"

#define ME_LIST(X) \
    /* ---- page 1: the part's level and sends (the eight physical knobs come first in the host: knob_target) */ \
    ME_TP(X, "Level", P_LEVEL, 0, "mixer: tracks_edit's LEVEL write") \
    ME_TP(X, "Drive", P_DIST, S_, ME_KNOB " (FX)") ME_TP(X, "Delay Send", P_DLY, S_, ME_KNOB " (FX)") \
    ME_TP(X, "Reverb Send", P_REV, S_, ME_KNOB " (FX)") \
    /* ---- page 2: ENV, ENV DEST, pan */ \
    ME_TP(X, "Attack", P_ATK, S_, ME_KNOB " (ENV)") ME_TP(X, "Decay", P_DEC, S_, ME_KNOB " (ENV)") \
    ME_TP(X, "Sustain", P_SUS, S_, ME_KNOB " (ENV)") ME_TP(X, "Release", P_REL, S_, ME_KNOB " (ENV)") \
    ME_TP(X, "Env>Filter", P_ED_FLT, S_, ME_KNOB " (ENV DEST)") ME_TP(X, "Env>Pitch", P_ED_PIT, S_, ME_KNOB " (ENV DEST)") \
    ME_TP(X, "Env>Shape", P_ED_SHP, S_, ME_KNOB " (ENV DEST)") ME_TP(X, "Pan", P_PAN, 0, "mixer: tracks_edit's PAN write") \
    /* ---- page 3: the engine (EDIT 1, EDIT 2) */ \
    ME_ED(X, 0) ME_ED(X, 1) ME_ED(X, 2) ME_ED(X, 3) ME_ED(X, 4) ME_ED(X, 5) ME_ED(X, 6) ME_ED(X, 7) \
    /* ---- page 4: LFO, LFO DEST */ \
    ME_TP(X, "LFO Rate", P_LRATE, S_, ME_KNOB " (LFO)") ME_TP(X, "LFO Wave", P_LWAVE, S_, ME_KNOB " (LFO)") \
    ME_TP(X, "LFO Phase", P_LPHASE, S_, ME_KNOB " (LFO)") ME_TP(X, "LFO Fade", P_LFADE, S_, ME_KNOB " (LFO)") \
    ME_TP(X, "LFO>Pitch", P_LD_PIT, S_, ME_KNOB " (LFO DEST)") ME_TP(X, "LFO>Filter", P_LD_FLT, S_, ME_KNOB " (LFO DEST)") \
    ME_TP(X, "LFO>Shape", P_LD_SHP, S_, ME_KNOB " (LFO DEST)") ME_TP(X, "LFO>Amp", P_LD_AMP, S_, ME_KNOB " (LFO DEST)") \
    /* ---- page 5: chorus send, VOICE, VOICE 2, mute */ \
    ME_TP(X, "Chorus Send", P_CHOR, S_, ME_KNOB " (FX)") ME_TP(X, "Voice Mode", P_VOICE, S_, ME_KNOB " (VOICE)") \
    ME_TP(X, "Glide", P_GLIDE, S_, ME_KNOB " (VOICE)") ME_TP(X, "Glide Mode", P_GLMODE, S_, ME_KNOB " (VOICE)") \
    ME_TP(X, "Priority", P_PRIO, S_, ME_KNOB " (VOICE)") ME_TP(X, "Allocation", P_ALLOC, S_, ME_KNOB " (VOICE 2)") \
    ME_TP(X, "Detune", P_DETUNE, S_, ME_KNOB " (VOICE 2)") ME_TP(X, "Mute", P_MUTE, 0, "mixer: tracks_edit's MUTE write") \
    /* ---- page 6: ARP, ARP 2 */ \
    ME_TP(X, "Arp Mode", P_AMODE, 0, ME_KNOB " (ARP)") ME_TP(X, "Arp Rate", P_ARATE, 0, ME_KNOB " (ARP)") \
    ME_TP(X, "Arp Octaves", P_AOCT, 0, ME_KNOB " (ARP)") ME_TP(X, "Arp Gate", P_AGATE, 0, ME_KNOB " (ARP)") \
    ME_TP(X, "Arp Swing", P_ASWING, 0, ME_KNOB " (ARP 2)") ME_TP(X, "Arp Chance", P_APROB, 0, ME_KNOB " (ARP 2)") \
    ME_TP(X, "Arp Hold", P_AHOLD, 0, ME_KNOB " (ARP 2)") ME_TP(X, "Arp Order", P_AORDER, 0, ME_KNOB " (ARP 2)") \
    /* ---- page 7: SCL, CHORD, the part, the tempo */ \
    ME_TP(X, "Scale Root", P_ROOT, 0, ME_KNOB " (SCL)") ME_TP(X, "Scale", P_SCALE, 0, ME_KNOB " (SCL)") \
    ME_TP(X, "Quantize", P_QUANT, 0, ME_KNOB " (SCL)") ME_TP(X, "Transpose", P_TRANS, 0, ME_KNOB " (SCL)") \
    ME_TP(X, "Chord", P_CHRD, 0, ME_KNOB " (CHORD)") ME_TP(X, "Chord Voicing", P_VOIC, 0, ME_KNOB " (CHORD)") \
    X("Part", MEK_LIT, 0, me_g_part, me_s_part, me_t_part, 0, 0, "track_select (the ALGORITHM knob's)") \
    ME_GP(X, "Tempo", G_BPM, ME_GKNOB " (GLOBAL)") \
    /* ---- page 8 / 9: GLOBAL, the FX buses (DLY, REVERB, CHORUS), PATTERN */ \
    ME_GP(X, "Swing", G_SWING, ME_GKNOB " (GLOBAL)") ME_GPF(X, "Tune", G_TUNE, FM1P_PERSIST, ME_GKNOB " (GLOBAL), kept by glo_poll") \
    ME_GP(X, "Delay Time", G_DTIME, ME_GKNOB " (DLY)") ME_GP(X, "Delay Feedback", G_DFDBK, ME_GKNOB " (DLY)") \
    ME_GP(X, "Delay Colour", G_DCOLOR, ME_GKNOB " (DLY)") ME_GP(X, "Delay Mix", G_DMIX, ME_GKNOB " (DLY)") \
    ME_GP(X, "Reverb Type", G_RTYPE, ME_GKNOB " (REVERB)") ME_GP(X, "Reverb Size", G_RSIZE, ME_GKNOB " (REVERB)") \
    ME_GP(X, "Reverb Damp", G_RDAMP, ME_GKNOB " (REVERB)") ME_GP(X, "Chorus Rate", G_CRATE, ME_GKNOB " (CHORUS)") \
    ME_GP(X, "Chorus Depth", G_CDEPTH, ME_GKNOB " (CHORUS)") \
    ME_TP(X, "Pattern Length", P_SLEN, 0, ME_KNOB " (PATTERN), not while a song plays") \
    ME_TP(X, "Pattern Div", P_SDIV, 0, ME_KNOB " (PATTERN), not while a song plays") \
    ME_TP(X, "Pattern Swing", P_SSWING, 0, ME_KNOB " (PATTERN), not while a song plays") \
    ME_TP(X, "Pattern Gate", P_SGATE, 0, ME_KNOB " (PATTERN), not while a song plays") \
    /* ---- the modulation matrix's first two slots (MOD) */ \
    ME_TP(X, "Mod 1 Source", P_M1SRC, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 1 Dest", P_M1DST, S_, ME_KNOB " (MOD)") \
    ME_TP(X, "Mod 1 Amount", P_M1AMT, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 2 Source", P_M2SRC, S_, ME_KNOB " (MOD)") \
    ME_TP(X, "Mod 2 Dest", P_M2DST, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 2 Amount", P_M2AMT, S_, ME_KNOB " (MOD)") \
    /* ---- the knobs' own targets (FM1P_HIDDEN) */ \
    ME_FXM(X, 0, "FX Filter") ME_FXM(X, 1, "FX Crush") ME_FXM(X, 2, "FX Throw") ME_FXM(X, 3, "FX Depth") \
    ME_TLV(X, 0, "T1 Level") ME_TLV(X, 1, "T2 Level") ME_TLV(X, 2, "T3 Level") ME_TLV(X, 3, "T4 Level") \
    ME_KC(X, 0, "Knob 1") ME_KC(X, 1, "Knob 2") ME_KC(X, 2, "Knob 3") ME_KC(X, 3, "Knob 4")

#define ME_AS_PARAM(nm, kind, a, g, s, t, mt, fl, pa) {nm, 0, 0, 0, 0, "", g, s, t, mt, fl, pa},
#define ME_AS_SRC(nm, kind, a, ...) {kind, a},
static fm1param_t ME_PARAMS[] = {ME_LIST(ME_AS_PARAM)};
static const struct { uint8_t kind; int16_t a; } ME_SRC[] = {ME_LIST(ME_AS_SRC)};
#define ME_NPARAMS ((uint32_t)(sizeof ME_PARAMS / sizeof ME_PARAMS[0]))

static int32_t me_index_of(uint32_t kind, int32_t a)
{
    uint32_t i;
    for (i = 0; i < ME_NPARAMS; i++)
        if (ME_SRC[i].kind == kind && ME_SRC[i].a == a)
            return (int32_t)i;
    return -1;
}
/* the map entry of a part parameter id: an engine entry for P_E0.., else the TP entry, -1 none */
static int32_t me_entry_of(uint32_t id)
{
    return id >= P_E0 ? me_index_of(MEK_ED, (int32_t)(id - P_E0)) : me_index_of(MEK_TP, (int32_t)id);
}
#define ME_MT_E(k) static int32_t me_mt_e##k(void) { return me_index_of(MEK_ED, k); }   /* itself */
ME_MT_E(0) ME_MT_E(1) ME_MT_E(2) ME_MT_E(3) ME_MT_E(4) ME_MT_E(5) ME_MT_E(6) ME_MT_E(7)

/* an engine entry's descriptor from snapshot buffer b */
static void me_ed_apply(uint32_t b)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) {
        fm1param_t *p = &ME_PARAMS[me_index_of(MEK_ED, (int32_t)k)];
        const param_desc_t *d = &me_ed[b][k];
        p->name = me_ednm[b][k];
        p->min = d->min;
        p->max = d->max;
        p->def = me_clamp(d->def, d->min, d->max);
        p->names = d->fmt == F_ENUM ? d->names : 0;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (d->fmt == F_ENUM ? FM1P_ENUM : 0u);
    }
}
/* the ranges, defaults and names from the firmware's tables (once, when the descriptor is first asked for: before
 * the power-on, so the engine entries start as the power-on's part 1 engine, ANALOG; the first epoch corrects them) */
static int me_params_ready;
static uint32_t me_epoch = 1, me_sig_sel = 0xFFFFFFFFu, me_sig_eng = 0xFFFFFFFFu;
static uint32_t me_ed_sig(uint32_t b)            /* a signature of snapshot b (labels, ranges, names) */
{
    uint32_t k, h = 2166136261u;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = &me_ed[b][k];
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
static uint32_t me_cur_sig;
static void me_params_init(void)
{
    uint32_t i, k;
    if (me_params_ready)
        return;
    for (i = 0; i < ME_NPARAMS; i++) {
        fm1param_t *p = &ME_PARAMS[i];
        const param_desc_t *d = 0;
        int32_t a = ME_SRC[i].a;
        switch (ME_SRC[i].kind) {
        case MEK_TP: d = &TP[a]; break;
        case MEK_GP: d = &GP[a]; break;
        case MEK_LIT:                             /* Part */
            p->min = 0;
            p->max = NTRK - 1;
            p->def = 0;
            p->names = ME_PARTS;
            p->flags |= FM1P_ENUM;
            break;
        case MEK_FXM:
            p->min = a ? 0 : -100;
            p->max = 100;
            p->def = 0;
            break;
        case MEK_TLV:
            d = &TP[P_LEVEL];
            break;
        case MEK_KNOB:                            /* (the knob cells: before the first epoch, ENV's) */
            d = &TP[a == 0 ? P_ATK : a == 1 ? P_DEC : a == 2 ? P_SUS : P_REL];
            me_kc[a].pg = &PAGES[0];
            me_kc[a].slot = (uint8_t)a;
            me_kd[0][a] = me_kd[1][a] = *d;
            snprintf(me_knm[0][a], sizeof me_knm[0][a], "Knob %d", (int)a + 1);
            memcpy(me_knm[1][a], me_knm[0][a], sizeof me_knm[1][a]);
            p->name = me_knm[0][a];
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
        me_ed[0][k] = ENGINES[0]->edit[k];
        snprintf(me_ednm[0][k], sizeof me_ednm[0][k], "E%u %s", (unsigned)k + 1u, me_ed[0][k].label);
    }
    me_edcur = 0;
    me_cur_sig = me_ed_sig(0);
    me_ed_apply(0);
    me_params_ready = 1;
}

/* ============================================================ the knobs == */
/* knob_target(role): what each physical knob turns on the screen showing now, ui_input's dispatch read without
 * turning (the same tests in the same order; the table in the file's header). Evaluated by param_epoch(). */
static me_cell_t me_kc_new[4];
static param_desc_t me_kd_new[4];
static char me_knm_new[4][24];
static int32_t me_cell_new(const page_t *pg, uint32_t k, const param_desc_t *d)
{
    k &= 3u;
    me_kc_new[k].pg = pg;
    me_kc_new[k].slot = (uint8_t)k;
    me_kd_new[k] = *d;                            /* (a desc hook may hand out one static: copied at once) */
    snprintf(me_knm_new[k], 17, "%s %s", pg ? pg->title : "HOME", d->label ? d->label : "-");
    return me_index_of(MEK_KNOB, (int32_t)k);
}
/* a named entry for what vp points at (the selected part's parameter or the song's), else the knob's own cell */
static int32_t me_cell_target(const page_t *pg, uint32_t k, const param_desc_t *d, int16_t *vp)
{
    int32_t e = -1;
    if (me_in_trk(vp))
        e = me_entry_of((uint32_t)(vp - TSEL->p));
    else if (vp >= song.g && vp < song.g + G_COUNT)
        e = me_index_of(MEK_GP, (int32_t)(vp - song.g));
    return e >= 0 ? e : me_cell_new(pg, k, d);
}
static int32_t me_page_target(const page_t *pg, uint32_t k, int layer)   /* edit_param(k, ..) on page pg */
{
    static const uint8_t MIXER[4] = {P_LEVEL, P_PAN, P_REV, P_MUTE};   /* tracks_edit */
    int16_t *vp;
    const param_desc_t *d;
    k &= 3u;
    if (pg->graph == GR_CHANCE || pg->graph == GR_MOTION || pg->graph == GR_SONG || pg->scope == SC_STEP ||
        pg->graph == GR_BROWSE || pg->graph == GR_USER || pg->graph == GR_PATS || (pg->graph == GR_MOD && !k))
        return -1;                                /* a cursor, a row, a slot, a preset list: turned */
#if ME_SLICE
    if (pg->graph == GR_SLICES && k < 2u)
        return -1;
#endif
#if ME_MELODEE
    if (pg->graph == GR_CZTOOLS)
        return -1;
#endif
    if (pg->scope == SC_TRK)
        return me_entry_of(MIXER[k]);
    if (!layer && ((act_cols() >> k) & 1u))
        return -1;                                /* an action: picked, OCT+ does it */
    d = page_desc(pg, k, &vp);
    if (!d || !vp || d->max == d->min)
        return -1;
    return me_cell_target(pg, k, d, vp);
}
static int32_t me_knob_compute(uint32_t role)
{
    int16_t *vp;
    const param_desc_t *d;
    uint32_t l;
    if (role >= NE || ui.menu || ui.confirm || name_on())
        return -1;                                /* the menu, a dialog, NAME: their own (turned) */
    if (layer_held()) {                           /* a layer's button held: KNOB 1..4 are the layer's */
        l = ui.ly % LAYER_N;
        if (role == EN_SELECT)
            return ME_SELECT_TARGET;
        if (role < EN_K1)
            return -1;                            /* (PRESETS / ALGORITHM: swallowed) */
        if (l == LAYER_FX)
            return me_index_of(MEK_FXM, (int32_t)(role - EN_K1));
        if (l == LAYER_GLO)
            return me_index_of(MEK_TLV, (int32_t)(role - EN_K1));
        if (l == LAYER_EDIT)
            return -1;                            /* ENG, No., FAV: lists */
        return me_page_target(l == LAYER_SCL ? &LY_SCL : &PAGES[page_first(LAYERS[l].fam)], role - EN_K1, 1);
    }
    switch (role) {
    case EN_SELECT: return ME_SELECT_TARGET;
    case EN_ALGO: return me_index_of(MEK_LIT, 0);   /* the selected part, on every page */
    case EN_PRESET: return -1;                    /* the sounds (HOME, PRESETS): a list */
    default: break;
    }
    if (ui.home) {
        d = home_param(role - EN_K1, &vp);
        return d && vp && d->max > d->min ? me_cell_target(0, role - EN_K1, d, vp) : -1;
    }
    return me_page_target(cur_page(), role - EN_K1, 0);
}

static int32_t me_kt[EMU_NE] = {-1, -1, -1, -1, -1, -1, -1, -1};
static uint32_t me_ksig;
static int me_ksig_valid;
static uint32_t me_fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
/* the knobs' targets now; 1: they (or a rewritten cell) changed, committed */
static int me_knobs_update(void)
{
    int32_t t[EMU_NE], kc0 = me_index_of(MEK_KNOB, 0);
    uint32_t r, k, b, h = 2166136261u;
    for (r = 0; r < EMU_NE; r++) {
        t[r] = me_knob_compute(r);
        h = me_fnv(h, (uint32_t)t[r]);
        if (t[r] >= kc0 && t[r] < kc0 + 4) {
            const char *s;
            k = (uint32_t)(t[r] - kc0);
            h = me_fnv(me_fnv(h, (uint32_t)(uintptr_t)me_kc_new[k].pg), me_kc_new[k].slot);
            h = me_fnv(me_fnv(h, (uint32_t)(uint16_t)me_kd_new[k].min), (uint32_t)(uint16_t)me_kd_new[k].max);
            h = me_fnv(me_fnv(h, me_kd_new[k].fmt), (uint32_t)(uintptr_t)me_kd_new[k].names);
            for (s = me_knm_new[k]; *s; s++)
                h = me_fnv(h, (uint8_t)*s);
        }
    }
    if (me_ksig_valid && h == me_ksig)
        return 0;
    b = (me_kcur & 1u) ^ 1u;                      /* the other buffer: the current one, then what changed */
    for (k = 0; k < 4u; k++) {
        me_kd[b][k] = me_kd[b ^ 1u][k];
        memcpy(me_knm[b][k], me_knm[b ^ 1u][k], sizeof me_knm[b][k]);
    }
    for (r = 0; r < EMU_NE; r++)
        if (t[r] >= kc0 && t[r] < kc0 + 4) {
            k = (uint32_t)(t[r] - kc0);
            me_kc[k] = me_kc_new[k];
            me_kd[b][k] = me_kd_new[k];
            memcpy(me_knm[b][k], me_knm_new[k], sizeof me_knm[b][k]);
        }
    for (k = 0; k < 4u; k++) {                    /* the entries' own fields: the buffer's */
        fm1param_t *p = &ME_PARAMS[kc0 + (int32_t)k];
        const param_desc_t *d = &me_kd[b][k];
        p->name = me_knm[b][k];
        p->min = d->min;
        p->max = d->max > d->min ? d->max : d->min + 1;
        p->def = me_clamp(d->def, p->min, p->max);
        p->names = d->fmt == F_ENUM ? d->names : 0;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (d->fmt == F_ENUM ? FM1P_ENUM : 0u);
    }
    me_kcur = b;
    for (r = 0; r < EMU_NE; r++)
        me_kt[r] = t[r];
    me_ksig = h;
    me_ksig_valid = 1;
    return 1;
}

/* the epoch: the selected part, its engine, or its engine descriptors changed -> the E entries rewritten; any
 * knob's target (or the cell a Knob entry stands for) changed -> knob_target re-read; the counter moved (the host
 * relabels) */
static uint32_t me_param_epoch(void)
{
    uint32_t b, sig, moved = 0;
    if (emu_hal.ready != 1u)
        return me_epoch;                          /* (before the power-on: the tables as initialised) */
    b = (me_edcur & 1u) ^ 1u;
    me_ed_snap(b);
    sig = me_ed_sig(b);
    if (sig != me_cur_sig || song.sel != me_sig_sel || TSEL->eng_req != me_sig_eng) {
        me_edcur = b;                             /* (text() now reads the new snapshot) */
        me_cur_sig = sig;
        me_sig_sel = song.sel;
        me_sig_eng = TSEL->eng_req;
        me_ed_apply(b);
        moved = 1;
    }
    if (me_knobs_update())
        moved = 1;
    if (moved)
        me_epoch++;
    return me_epoch;
}

/* for the descriptor (core_melodee_desc.c) */
const fm1param_t *fm1core_me_params(uint32_t *n)
{
    me_params_init();
    *n = ME_NPARAMS;
    return ME_PARAMS;
}
uint32_t fm1core_me_param_epoch(void) { return me_param_epoch(); }
int32_t fm1core_me_knob_target(int role) { return role >= 0 && role < EMU_NE ? me_kt[role] : -1; }
#undef S_
#undef H_
