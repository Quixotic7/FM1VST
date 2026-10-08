/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core's Tier 2 parameter map (FM1-VST-PLAN.md 4.5; core-api/fm1core.h fm1param_t).
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_melodee.c includes this file after the firmware unit (melodee_fw.c), so
 * the firmware's statics (params.c's TP / GP tables, param_format, track_desc, the tracks, the song, ui.c's
 * track_select, motion.c's motion_capture) are in scope. Nothing in the submodule is modified.
 *
 * WHAT IS MAPPED: the selected part's parameters, as the device's pages show them (params.c PAGES): the HOME knobs,
 * the level and sends, ENV, ENV DEST, the engine's EDIT 1 / EDIT 2, LFO, LFO DEST, VOICE, ARP, SCL / CHORD,
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
 * THE META ENTRIES (FM1P_META):
 *   "Home Knob 1..4": the device's HOME knobs (the engine's knob[] of params.c home_param: ANALOG CUT RES ATK REL);
 *     target() names the entry of that parameter (an "E5 CUT" engine entry, "Attack", ..): the host shows
 *     "K1 E5 CUT", its range and texts;
 *   "E1".."E8": the selected part's engine parameters (EDIT 1 / EDIT 2). Their range, names and label depend on the
 *     engine (FM6's ALG is 0..32, ANALOG's WAVE an enum of 5), so these entries target THEMSELVES and their
 *     descriptor (name "E1 WAVE", min, max, def, names, FM1P_ENUM) is rewritten from the engine's track_desc before
 *     the epoch moves; the host re-reads it then (Tier2Parameter::retarget).
 *   param_epoch moves when the selected part, its engine, or one of its engine descriptors changed (a mode-dependent
 *   label, engine_t.desc).
 *
 * THREADS: get / set / target / param_epoch run on the clock's thread between device milliseconds; text() formats
 * from the constant tables (and the E entries' descriptor snapshot, double-buffered) and may run on any thread.
 * (param_format writes params.c's fmt_named, a flag only ui_draw.c's next card reads after its own param_format.) */

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

/* ---- the HOME knobs: get / set act on the engine's knob[k] parameter of the selected part */
static uint32_t me_home_id(uint32_t k) { return ENGINES[TSEL->eng_req % NENGINES]->knob[k & 3u]; }
static int32_t me_hcache[4] = {-1, -1, -1, -1};   /* target() as of the last epoch (text() of the entry itself) */
static const fm1param_t *me_entry(int32_t i);
#define ME_HOME_FN(k) \
    static int32_t me_g_h##k(void) { return me_tget(me_home_id(k)); } \
    static void me_s_h##k(int32_t v) { me_tset(me_home_id(k), v); } \
    static int32_t me_mt_h##k(void); \
    static void me_t_h##k(int32_t v, char *b, uint32_t n) \
    { \
        const fm1param_t *e = me_entry(me_hcache[k]); \
        if (e && e->text) \
            e->text(v, b, n); \
        else \
            snprintf(b, n, "%d", (int)v); \
    }
ME_HOME_FN(0) ME_HOME_FN(1) ME_HOME_FN(2) ME_HOME_FN(3)

enum { MEK_TP, MEK_GP, MEK_ED, MEK_HOME, MEK_LIT };
#define S_ FM1P_SOUND
#define ME_TP(X, nm, id, fl, pa) X(nm, MEK_TP, id, me_g_##id, me_s_##id, me_t_##id, 0, fl, pa)
#define ME_GPF(X, nm, id, fl, pa) X(nm, MEK_GP, id, me_g_##id, me_s_##id, me_t_##id, 0, fl, pa)
#define ME_GP(X, nm, id, pa) ME_GPF(X, nm, id, 0, pa)
#define ME_ED(X, k) X("E" #k, MEK_ED, k, me_g_e##k, me_s_e##k, me_t_e##k, me_mt_e##k, FM1P_META | S_, \
                      "edit: the part's P_E" #k " (EDIT page knob write, engine range)")
#define ME_HOME(X, k, nm) X(nm, MEK_HOME, k, me_g_h##k, me_s_h##k, me_t_h##k, me_mt_h##k, FM1P_META | S_, \
                            "home: the HOME knob write (home_param: the engine's knob[" #k "])")
#define ME_KNOB "page: edit_param's knob write + motion_capture"
#define ME_GKNOB "page: edit_param's knob write (the song's)"

#define ME_LIST(X) \
    /* ---- page 1: the live page: the HOME screen's four knobs, the part's level and sends */ \
    ME_HOME(X, 0, "Home Knob 1") ME_HOME(X, 1, "Home Knob 2") ME_HOME(X, 2, "Home Knob 3") \
    ME_HOME(X, 3, "Home Knob 4") \
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
    ME_TP(X, "Mod 2 Dest", P_M2DST, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 2 Amount", P_M2AMT, S_, ME_KNOB " (MOD)")

#define ME_AS_PARAM(nm, kind, a, g, s, t, mt, fl, pa) {nm, 0, 0, 0, 0, "", g, s, t, mt, fl, pa},
#define ME_AS_SRC(nm, kind, a, ...) {kind, a},
static fm1param_t ME_PARAMS[] = {ME_LIST(ME_AS_PARAM)};
static const struct { uint8_t kind; int16_t a; } ME_SRC[] = {ME_LIST(ME_AS_SRC)};
#define ME_NPARAMS ((uint32_t)(sizeof ME_PARAMS / sizeof ME_PARAMS[0]))
static const fm1param_t *me_entry(int32_t i) { return i >= 0 && (uint32_t)i < ME_NPARAMS ? &ME_PARAMS[i] : 0; }

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
#define ME_MT_H(k) static int32_t me_mt_h##k(void) { return me_entry_of(me_home_id(k)); }
ME_MT_H(0) ME_MT_H(1) ME_MT_H(2) ME_MT_H(3)

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
static void me_home_apply(void)                  /* a HOME entry's own fields: its target's (for a host without
                                                  * retarget, and the param_test's ranges) */
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        fm1param_t *p = &ME_PARAMS[me_index_of(MEK_HOME, (int32_t)k)];
        const fm1param_t *t = me_entry(me_hcache[k]);
        if (!t)
            continue;
        p->min = t->min;
        p->max = t->max;
        p->def = t->def;
        p->names = t->names;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (t->flags & FM1P_ENUM);
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
    for (k = 0; k < 4u; k++) {
        uint32_t id = ENGINES[0]->knob[k];
        me_hcache[k] = me_entry_of(id);
    }
    me_home_apply();
    me_params_ready = 1;
}

/* the epoch: the selected part, its engine, or its engine descriptors changed -> the E entries rewritten, the HOME
 * targets re-read, the counter moved (the host relabels) */
static uint32_t me_param_epoch(void)
{
    uint32_t k, b, sig, moved = 0;
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
    for (k = 0; k < 4u; k++) {
        int32_t t = me_entry_of(me_home_id(k));
        if (t != me_hcache[k]) {
            me_hcache[k] = t;
            moved = 1;
        }
    }
    if (moved) {
        me_home_apply();
        me_epoch++;
    }
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
#undef S_
