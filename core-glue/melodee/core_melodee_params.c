/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core's Tier 2 parameter map (FM1-VST-PLAN.md 4.5; core-api/fm1core.h fm1param_t).
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_melodee.c includes this file after the firmware unit (melodee_fw.c), so
 * the firmware's statics (params.c's TP / GP tables, param_format, track_desc, the tracks, the song, ui.c's
 * track_select, motion.c's motion_capture) are in scope. Nothing in the submodule is modified.
 *
 * WHAT IS MAPPED (rebuilt for 1.4.3's parameter tables, core.h P_* / G_*; the names of 0.11's entries kept where the
 * parameter still exists): the selected part's parameters, as the device's pages show them (params.c PAGES): the
 * level and its three FX sends, ENV, ENV DEST, the engine's EDIT 1 / EDIT 2, LFO, LFO DEST, the stereo spread,
 * VOICE / VOICE 2, ARP / ARP 2, SCL / CHORD, its FX rack's three slots (effect, mix), PATTERN, two MOD slots; the
 * part selection itself; the song's TEMPO and GLOBAL (tempo, swing, tune) and the shared FX rack (its routing, its
 * return, its three slots' effect and mix). 1.4 reworked the effects: the per-part DIST / CHOR / DLY / REV sends and
 * the song's delay, reverb and chorus parameters (0.11's "Drive", "Delay Send", "Reverb Send", "Chorus Send",
 * "Delay Time" .. "Chorus Depth") are gone; an FX rack per part (three slots, P_FXTYPE ..) and a shared one
 * (G_MFXTYPE ..) took their place. Every other value a page shows is reached by the knobs (the "Knob 1..4" cells).
 * "The selected part" follows the device: select another part (the ALGORITHM knob, or the "Part" entry) and every
 * part entry reads and writes that part.
 *
 * HOW A HOST WRITE REACHES THE FIRMWARE: the knob's own write (ui_input.c edit_param's tail, tracks_edit,
 * ui_layer.c layer_knob): the value clamped to the descriptor's range, past a retired enum alias in the direction
 * of travel (params.c enum_step), stored in the part's (or the song's) parameter array, then motion_capture
 * (motion.c: a live edit becomes the motion sequencer's base value; recorded while the part records; not while LFO
 * held makes an edit momentary, nor in the designer) with its "PLAY TO RECORD MOTION" hint, and scale_share (SCALE,
 * QUANT, the MPC degree are set on every part at once). The native engines' pages (Prophet, CZ-1, FM6) write back
 * into the patch as edit_param does (p5_edit_value, cz_ed_put, fm6_page_put); GLO's device settings (BOOT, A4,
 * BUDGET, the WEIGHTS, DRUM) are saved at once. The audio ISR reads the arrays as they are (it is what the knob
 * does). Pattern LEN / DIV / SWING / GATE refuse while a song chain plays ("STOP TO EDIT", as the knob). Nothing
 * named here is in the settings record but Tune (FM1P_PERSIST: Melodee keeps CLK TUNE MIDI ROUT as last used,
 * project.c glo_poll); parts and song are the project, saved by SAVE > PROJECT on the device.
 *
 * THE META ENTRIES (FM1P_META): "E1".."E8", the selected part's engine parameters (EDIT 1 / EDIT 2). Their range,
 *   names and label depend on the engine, so these entries target THEMSELVES and their descriptor (name "E1 CUT",
 *   min, max, def, names, FM1P_ENUM) is rewritten from the engine's track_desc before the epoch moves; the host
 *   re-reads it then (Tier2Parameter::retarget).
 *
 * THE KNOBS (knob_target, ABI 4): what each physical knob turns on the screen showing now, ui_input's own dispatch
 * read without turning (me_knob_compute, the same tests in the same order). -1, a relative turn: no value there, or
 * one the firmware edits its own way. MASTER is the pot everywhere.
 *   SELECT       pages, steps, the menu: always a relative turn
 *   PRESETS      the sounds, the scale list, the zoom, the operator: relative
 *   ALGORITHM    Part (ui_input.c: the selected track on every page), but under a held layer (swallowed)
 *   KNOB 1..4    HOME (Stage): the selected engine's four (ui.c stage_page: the engine's .knob, Prophet's filter
 *                and envelope, CZ-1's wave, DCW, detune, vibrato); a page: its four (page_desc), as named entries
 *                where one is that value, else the hidden "Knob 1..4" cells (rewritten from page_desc: "LFO 2 SYNC");
 *                a list page (SCL, VOICE, FM LFO, FM BEND: ui_list.c): KNOB 1 the row's value, KNOB 2 the row
 *                (relative); MIXER: Level, Pan, -, Mute; GLO held: T1..T4 Level (H); SCL held: Scale Root, Scale,
 *                Chord, Chord Voicing. Relative: MOD's and the FX racks' KNOB 2 (the route, the row), STEP / NOTES,
 *                the DRUM pages, PATTERNS, SONG, MOTION, the slot pages (PROJECT, USER, the STOREs), the browser,
 *                SCALES, CZ TOOLS, LFO held (a momentary edit), the menu, a dialog, a sheet, NAME, NEW SONG, the
 *                section map, the designer, the boot maintenance screen
 *   (H): FM1P_HIDDEN, a knob target only. param_epoch moves when the selected part, its engine, one of its engine
 *   descriptors (a mode-dependent label), or any knob's target (or a rewritten cell) changed: the targets are
 *   computed afresh at every call.
 *
 * THREADS: get / set / target / param_epoch run on the clock's thread between device milliseconds; text() formats
 * from the constant tables (and the E entries' descriptor snapshot, double-buffered) and may run on any thread.
 * (param_format writes params.c's fmt_named, a flag only ui_draw.c's next card reads after its own param_format.) */

#define ME_SELECT_TARGET -1   /* SELECT: pages, steps, the menu: relative */

static int32_t me_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the firmware's value text: param_format's value and unit ("355 Hz", "1/8", "+12 st", "OFF"). A name param_format
 * cuts to the card's five letters ("MT-1/" for MT-1/4 and MT-1/3) is given whole; a retired enum value (an empty
 * name: MOD's old effect destinations 6..9) says what it plays as (enum_orig) and that it is the old value */
static void me_fmt(const param_desc_t *d, int32_t v, char *b, uint32_t n)
{
    char val[24];
    const char *unit = "";
    val[0] = 0;
    if (d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max && !d->names[v][0]) {
        param_format(d, enum_orig(d, v), val, &unit);
        snprintf(b, n, "%s (old %d)", val, (int)v);
        return;
    }
    param_format(d, v, val, &unit);
    if (d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max && strlen(d->names[v]) > strlen(val) &&
        !strncmp(d->names[v], val, strlen(val)))
        snprintf(val, sizeof val, "%s", d->names[v]);
    snprintf(b, n, unit && unit[0] ? "%s %s" : "%s", val, unit ? unit : "");
}

static int me_in_trk(const int16_t *vp) { return vp >= TSEL->p && vp < TSEL->p + P_COUNT; }
/* edit_param's tail after the value is stored, for a part's value (vp in TSEL->p) on a page of scope sc */
static void me_track_written(const int16_t *vp, uint32_t sc)
{
    const uint32_t id = (uint32_t)(vp - TSEL->p);
    if (!momentary.active && !designer_on()) {
        motion_capture(TSEL, id, *vp);
        if (motion_param(id))
            motion_unplayed();
    }
    if (sc == SC_TRACK && scale_shared(id))
        scale_share(TSEL);
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
        return;                                   /* the knob: "STOP TO EDIT" (PATTERN, GR_STEPS) */
    t->p[id] = (int16_t)enum_step(d, t->p[id], me_clamp(v, d->min, d->max));
    me_track_written(&t->p[id], id >= P_E0 ? SC_ENGINE : SC_TRACK);
}
/* ---- a song parameter (GP id: the TEMPO / GLOBAL pages, the shared FX rack; none of GLO's device settings) */
static int32_t me_gget(uint32_t id) { return song.g[id % G_COUNT]; }
static void me_gset(uint32_t id, int32_t v)
{
    const param_desc_t *d = global_desc(id % G_COUNT);
    if (d->max > d->min)
        song.g[id % G_COUNT] = (int16_t)enum_step(d, song.g[id % G_COUNT], me_clamp(v, d->min, d->max));
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
ME_TP_FN(P_FXSEND) ME_TP_FN(P_FXSEND2) ME_TP_FN(P_FXSEND3) ME_TP_FN(P_SPRD)
ME_TP_FN(P_FXTYPE) ME_TP_FN(P_FXMIX) ME_TP_FN(P_FX2TYPE) ME_TP_FN(P_FX2MIX) ME_TP_FN(P_FX3TYPE) ME_TP_FN(P_FX3MIX)
ME_TP_FN(P_VOICE) ME_TP_FN(P_GLIDE) ME_TP_FN(P_PAN) ME_TP_FN(P_MUTE)
ME_TP_FN(P_GLMODE) ME_TP_FN(P_PRIO) ME_TP_FN(P_ALLOC) ME_TP_FN(P_DETUNE)
ME_TP_FN(P_M1SRC) ME_TP_FN(P_M1DST) ME_TP_FN(P_M1AMT) ME_TP_FN(P_M2SRC) ME_TP_FN(P_M2DST) ME_TP_FN(P_M2AMT)
ME_TP_FN(P_CHRD) ME_TP_FN(P_VOIC)
ME_GP_FN(G_BPM) ME_GP_FN(G_SWING) ME_GP_FN(G_TUNE)
ME_GP_FN(G_FXMODE) ME_GP_FN(G_FXRETURN)
ME_GP_FN(G_MFXTYPE) ME_GP_FN(G_MFXMIX) ME_GP_FN(G_MFX2TYPE) ME_GP_FN(G_MFX2MIX) ME_GP_FN(G_MFX3TYPE) ME_GP_FN(G_MFX3MIX)

/* ---- the part selection (ui.c track_select: the ALGORITHM knob's, the editor's; the tracks in use) */
static const char *const ME_PARTS[NTRK] = {"1", "2", "3", "4"};
static int32_t me_g_part(void) { return song.sel; }
static void me_s_part(int32_t v) { track_select((uint32_t)me_clamp(v, 0, (int32_t)trk_n() - 1)); }
static void me_t_part(int32_t v, char *b, uint32_t n) { snprintf(b, n, "Part %d", (int)me_clamp(v, 0, NTRK - 1) + 1); }

/* ---- the engine entries E1..E8: a snapshot of the selected part's engine descriptors (double-buffered for text) */
static param_desc_t me_ed[2][8];
static char me_ednm[2][8][24];
static volatile uint32_t me_edcur;
static void me_ed_snap(uint32_t b)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = track_desc(TSEL, P_E0 + k);
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
/* GLO held: T1..T4 LEVEL (ui_layer.c layer_knob: the part's level, recorded as on MIXER; the tracks in use) */
#define ME_TLV_FN(k) \
    static int32_t me_g_tlv##k(void) { return trk[k].p[P_LEVEL]; } \
    static void me_s_tlv##k(int32_t v) \
    { \
        if (k >= trk_n()) \
            return; \
        trk[k].p[P_LEVEL] = (int16_t)me_clamp(v, TP[P_LEVEL].min, TP[P_LEVEL].max); \
        motion_capture(&trk[k], P_LEVEL, trk[k].p[P_LEVEL]); \
    }
ME_TLV_FN(0) ME_TLV_FN(1) ME_TLV_FN(2) ME_TLV_FN(3)
/* Knob 1..4: the page cell under KNOB k when no named entry is that value (FX 1's RATE, LFO 2, MOD's slots 3 / 4,
 * the native engines' panels, GLO's A4 and WEIGHTS ..): the cell (page, column; home: Stage's page, rebuilt at each
 * use) is the epoch's, its descriptor double-buffered for text(), written as edit_param writes it (the value given
 * instead of the detents) */
typedef struct { const page_t *pg; uint8_t slot, home; } me_cell_t;
static me_cell_t me_kc[4];
static param_desc_t me_kd[2][4];
static char me_knm[2][4][24];
static volatile uint32_t me_kcur;
static const page_t *me_cell_page(const me_cell_t *c) { return c->home ? stage_page() : c->pg; }
static const param_desc_t *me_cell_desc(const me_cell_t *c, int16_t **vp)
{
    const page_t *pg = me_cell_page(c);
    *vp = 0;
    return pg ? page_desc(pg, c->slot, vp) : 0;
}
static int32_t me_kc_get(uint32_t k)
{
    int16_t *vp;
    me_cell_desc(&me_kc[k & 3u], &vp);
    return vp ? *vp : 0;
}
/* ui_input.c edit_param from `d = page_desc(..)` on, with the value v in place of *vp + the knob's steps */
static void me_cell_write(const page_t *pg, uint32_t slot, int32_t v)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    if (chain_busy() && (pg->scope == SC_STEP || pg->graph == GR_STEPS))
        return;                                   /* the knob: "STOP TO EDIT" */
    v = enum_step(d, *vp, me_clamp(v, d->min, d->max));
    *vp = (int16_t)v;
    if (pg->scope == SC_P5) {
        uint8_t old[MO_NATIVE_N];
        uint32_t f = motion_guard();
        motion_native_peek(TSEL, old);
        p5_edit_value(TSEL, pg->id[slot], p5_panel_raw(TSEL, pg->id[slot], v));
        native_motion(old);
        motion_unguard(f);
        return;
    }
    if (pg->scope == SC_P5STORE) {
        p5_store_slot = (int16_t)v;
        return;
    }
    if (pg->scope == SC_CZ1) {                    /* (a copy of the tone's value: written back there) */
        uint8_t raw[CZ_BYTES], old[MO_NATIVE_N];
        uint32_t tr = song.sel % NTRK, f = motion_guard();
        motion_native_peek(TSEL, old);
        if (cz_ed_put(tr, pg->id[slot], (uint32_t)v, raw)) {
            if (!momentary.active)
                cz_compare_take(tr);
            memcpy(cz_patch[tr].raw, raw, 128u);
            native_motion(old);
        }
        motion_unguard(f);
        return;
    }
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP) {   /* (a copy of FM6's value: written back there) */
        uint8_t old[MO_NATIVE_N];
        uint32_t f = motion_guard();
        motion_native_peek(TSEL, old);
        fm6_page_put(pg, slot, v);
        native_motion(old);
        motion_unguard(f);
        return;
    }
    if (pg->scope == SC_GLOBAL && pg->id[slot] == G_BOOT) {   /* BOOT: the device's (settings), saved */
        settings_boot = (uint8_t)v;
        settings_save();
        return;
    }
    if (pg->scope == SC_GLOBAL && (pg->id[slot] == G_A4 || pg->id[slot] == G_BUDGET ||
                                   (pg->id[slot] >= G_WP5 && pg->id[slot] <= G_SAFETY))) {
        settings_save();
        return;
    }
    if (pg->scope == SC_GLOBAL && pg->id[slot] == G_DRUMCH) { /* DRUM: the device's too */
        settings_drumch = (uint8_t)v;
        settings_save();
        return;
    }
    if (me_in_trk(vp))
        me_track_written(vp, pg->scope);
}
static void me_kc_set(uint32_t k, int32_t v)
{
    const me_cell_t *c = &me_kc[k & 3u];
    const page_t *pg = me_cell_page(c);
    if (pg)
        me_cell_write(pg, c->slot, v);
}
#define ME_KC_FN(k) \
    static int32_t me_g_kc##k(void) { return me_kc_get(k); } \
    static void me_s_kc##k(int32_t v) { me_kc_set(k, v); } \
    static void me_t_kc##k(int32_t v, char *b, uint32_t n) { me_fmt(&me_kd[me_kcur & 1u][k], v, b, n); }
ME_KC_FN(0) ME_KC_FN(1) ME_KC_FN(2) ME_KC_FN(3)

enum { MEK_TP, MEK_GP, MEK_ED, MEK_LIT, MEK_TLV, MEK_KNOB };
#define S_ FM1P_SOUND
#define ME_TP(X, nm, id, fl, pa) X(nm, MEK_TP, id, me_g_##id, me_s_##id, me_t_##id, 0, fl, pa)
#define ME_GPF(X, nm, id, fl, pa) X(nm, MEK_GP, id, me_g_##id, me_s_##id, me_t_##id, 0, fl, pa)
#define ME_GP(X, nm, id, pa) ME_GPF(X, nm, id, 0, pa)
#define ME_ED(X, k) X("E" #k, MEK_ED, k, me_g_e##k, me_s_e##k, me_t_e##k, me_mt_e##k, FM1P_META | S_, \
                      "edit: the part's P_E" #k " (EDIT page knob write, engine range)")
#define H_ FM1P_HIDDEN
#define ME_TLV(X, k, nm) X(nm, MEK_TLV, k, me_g_tlv##k, me_s_tlv##k, me_t_P_LEVEL, 0, H_, \
                           "layer: GLO held, that part's level + motion_capture")
#define ME_KC(X, k, nm) X(nm, MEK_KNOB, k, me_g_kc##k, me_s_kc##k, me_t_kc##k, 0, S_ | H_, \
                          "knob: the page cell under that knob (edit_param's write)")
#define ME_KNOB "page: edit_param's knob write + motion_capture"
#define ME_GKNOB "page: edit_param's knob write (the song's)"

/* the order of 0.11's map where an entry still exists; the reworked effects in the places of the retired ones */
#define ME_LIST(X) \
    /* ---- page 1: the part's level and FX sends, ENV */ \
    ME_TP(X, "Level", P_LEVEL, 0, "mixer: tracks_edit's LEVEL write") \
    ME_TP(X, "FX Send", P_FXSEND, S_, ME_KNOB " (FX rack)") ME_TP(X, "FX Send 2", P_FXSEND2, S_, ME_KNOB " (FX rack)") \
    ME_TP(X, "FX Send 3", P_FXSEND3, S_, ME_KNOB " (FX rack)") \
    ME_TP(X, "Attack", P_ATK, S_, ME_KNOB " (ENV)") ME_TP(X, "Decay", P_DEC, S_, ME_KNOB " (ENV)") \
    ME_TP(X, "Sustain", P_SUS, S_, ME_KNOB " (ENV)") ME_TP(X, "Release", P_REL, S_, ME_KNOB " (ENV)") \
    /* ---- page 2: ENV DEST, pan */ \
    ME_TP(X, "Env>Filter", P_ED_FLT, S_, ME_KNOB " (ENV DEST)") ME_TP(X, "Env>Pitch", P_ED_PIT, S_, ME_KNOB " (ENV DEST)") \
    ME_TP(X, "Env>Shape", P_ED_SHP, S_, ME_KNOB " (ENV DEST)") ME_TP(X, "Pan", P_PAN, 0, "mixer: tracks_edit's PAN write") \
    /* ---- page 3: the engine (EDIT 1, EDIT 2) */ \
    ME_ED(X, 0) ME_ED(X, 1) ME_ED(X, 2) ME_ED(X, 3) ME_ED(X, 4) ME_ED(X, 5) ME_ED(X, 6) ME_ED(X, 7) \
    /* ---- page 4: LFO, LFO DEST */ \
    ME_TP(X, "LFO Rate", P_LRATE, S_, ME_KNOB " (LFO)") ME_TP(X, "LFO Wave", P_LWAVE, S_, ME_KNOB " (LFO)") \
    ME_TP(X, "LFO Phase", P_LPHASE, S_, ME_KNOB " (LFO)") ME_TP(X, "LFO Fade", P_LFADE, S_, ME_KNOB " (LFO)") \
    ME_TP(X, "LFO>Pitch", P_LD_PIT, S_, ME_KNOB " (LFO DEST)") ME_TP(X, "LFO>Filter", P_LD_FLT, S_, ME_KNOB " (LFO DEST)") \
    ME_TP(X, "LFO>Shape", P_LD_SHP, S_, ME_KNOB " (LFO DEST)") ME_TP(X, "LFO>Amp", P_LD_AMP, S_, ME_KNOB " (LFO DEST)") \
    /* ---- page 5: VOICE, VOICE 2, VOICE 3, mute */ \
    ME_TP(X, "Spread", P_SPRD, S_, ME_KNOB " (VOICE 3)") ME_TP(X, "Voice Mode", P_VOICE, S_, ME_KNOB " (VOICE)") \
    ME_TP(X, "Glide", P_GLIDE, S_, ME_KNOB " (VOICE)") ME_TP(X, "Glide Mode", P_GLMODE, S_, ME_KNOB " (VOICE)") \
    ME_TP(X, "Priority", P_PRIO, S_, ME_KNOB " (VOICE)") ME_TP(X, "Allocation", P_ALLOC, S_, ME_KNOB " (VOICE 2)") \
    ME_TP(X, "Detune", P_DETUNE, S_, ME_KNOB " (VOICE 2)") ME_TP(X, "Mute", P_MUTE, 0, "mixer: the MUTE write") \
    /* ---- page 6: ARP, ARP 2 */ \
    ME_TP(X, "Arp Mode", P_AMODE, 0, ME_KNOB " (ARP)") ME_TP(X, "Arp Rate", P_ARATE, 0, ME_KNOB " (ARP)") \
    ME_TP(X, "Arp Octaves", P_AOCT, 0, ME_KNOB " (ARP)") ME_TP(X, "Arp Gate", P_AGATE, 0, ME_KNOB " (ARP)") \
    ME_TP(X, "Arp Swing", P_ASWING, 0, ME_KNOB " (ARP 2)") ME_TP(X, "Arp Chance", P_APROB, 0, ME_KNOB " (ARP 2)") \
    ME_TP(X, "Arp Hold", P_AHOLD, 0, ME_KNOB " (ARP 2)") ME_TP(X, "Arp Order", P_AORDER, 0, ME_KNOB " (ARP 2)") \
    /* ---- page 7: SCL, CHORD, the part, the tempo */ \
    ME_TP(X, "Scale Root", P_ROOT, 0, ME_KNOB " (SCL)") ME_TP(X, "Scale", P_SCALE, 0, ME_KNOB " (SCL), every part's") \
    ME_TP(X, "Quantize", P_QUANT, 0, ME_KNOB " (SCL), every part's") ME_TP(X, "Transpose", P_TRANS, 0, ME_KNOB " (SCL)") \
    ME_TP(X, "Chord", P_CHRD, 0, ME_KNOB " (CHORD)") ME_TP(X, "Chord Voicing", P_VOIC, 0, ME_KNOB " (CHORD)") \
    X("Part", MEK_LIT, 0, me_g_part, me_s_part, me_t_part, 0, 0, "track_select (the ALGORITHM knob's)") \
    ME_GP(X, "Tempo", G_BPM, ME_GKNOB " (SEQ > TEMPO)") \
    /* ---- page 8 / 9: TEMPO, GLOBAL, the part's FX rack, the shared FX rack, PATTERN */ \
    ME_GP(X, "Swing", G_SWING, ME_GKNOB " (SEQ > TEMPO)") \
    ME_GPF(X, "Tune", G_TUNE, FM1P_PERSIST, ME_GKNOB " (GLOBAL), kept by glo_poll") \
    ME_TP(X, "FX 1 Type", P_FXTYPE, S_, ME_KNOB " (FX rack, slot 1)") ME_TP(X, "FX 1 Mix", P_FXMIX, S_, ME_KNOB " (FX 1)") \
    ME_TP(X, "FX 2 Type", P_FX2TYPE, S_, ME_KNOB " (FX rack, slot 2)") ME_TP(X, "FX 2 Mix", P_FX2MIX, S_, ME_KNOB " (FX 2)") \
    ME_TP(X, "FX 3 Type", P_FX3TYPE, S_, ME_KNOB " (FX rack, slot 3)") ME_TP(X, "FX 3 Mix", P_FX3MIX, S_, ME_KNOB " (FX 3)") \
    ME_GP(X, "Shared FX Route", G_FXMODE, ME_GKNOB " (SHARED: CHAIN or SPLIT)") \
    ME_GP(X, "Shared FX Return", G_FXRETURN, ME_GKNOB " (SHARED, its return)") \
    ME_GP(X, "Send FX 1 Type", G_MFXTYPE, ME_GKNOB " (SHARED, slot 1)") ME_GP(X, "Send FX 1 Mix", G_MFXMIX, ME_GKNOB " (SEND FX 1)") \
    ME_GP(X, "Send FX 2 Type", G_MFX2TYPE, ME_GKNOB " (SHARED, slot 2)") ME_GP(X, "Send FX 2 Mix", G_MFX2MIX, ME_GKNOB " (SEND FX 2)") \
    ME_GP(X, "Send FX 3 Type", G_MFX3TYPE, ME_GKNOB " (SHARED, slot 3)") ME_GP(X, "Send FX 3 Mix", G_MFX3MIX, ME_GKNOB " (SEND FX 3)") \
    ME_TP(X, "Pattern Length", P_SLEN, 0, ME_KNOB " (PATTERN), not while a song plays") \
    ME_TP(X, "Pattern Div", P_SDIV, 0, ME_KNOB " (PATTERN), not while a song plays") \
    ME_TP(X, "Pattern Swing", P_SSWING, 0, ME_KNOB " (PATTERN), not while a song plays") \
    ME_TP(X, "Pattern Gate", P_SGATE, 0, ME_KNOB " (PATTERN), not while a song plays") \
    /* ---- the modulation matrix's first two slots (MOD) */ \
    ME_TP(X, "Mod 1 Source", P_M1SRC, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 1 Dest", P_M1DST, S_, ME_KNOB " (MOD)") \
    ME_TP(X, "Mod 1 Amount", P_M1AMT, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 2 Source", P_M2SRC, S_, ME_KNOB " (MOD)") \
    ME_TP(X, "Mod 2 Dest", P_M2DST, S_, ME_KNOB " (MOD)") ME_TP(X, "Mod 2 Amount", P_M2AMT, S_, ME_KNOB " (MOD)") \
    /* ---- the knobs' own targets (FM1P_HIDDEN) */ \
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
        p->max = d->max > d->min ? d->max : d->min + 1;
        p->def = me_clamp(d->def, p->min, p->max);
        p->names = d->fmt == F_ENUM ? d->names : 0;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (d->fmt == F_ENUM ? FM1P_ENUM : 0u);
    }
}
/* the ranges, defaults and names from the firmware's tables (once, when the descriptor is first asked for: before
 * the power-on, so the engine entries start as the power-on's part 1 engine, TRK_DEF's; the first epoch corrects
 * them) */
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
        case MEK_TLV:
            d = &TP[P_LEVEL];
            break;
        case MEK_KNOB:                            /* (the knob cells: before the first epoch, ENV's) */
            d = &TP[a == 0 ? P_ATK : a == 1 ? P_DEC : a == 2 ? P_SUS : P_REL];
            for (k = 0; k < NPAGES && !(PAGES[k].fam == FAM_ENV && PAGES[k].graph == GR_ADSR); k++)
                ;
            me_kc[a].pg = &PAGES[k < NPAGES ? k : 0];
            me_kc[a].slot = (uint8_t)a;
            me_kc[a].home = 0;
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
    for (k = 0; k < 8u; k++) {                    /* part 1's power-on engine (engines.c TRK_DEF) */
        me_ed[0][k] = ENGINES[TRK_DEF[0][0]]->edit[k];
        snprintf(me_ednm[0][k], sizeof me_ednm[0][k], "E%u %s", (unsigned)k + 1u,
                 me_ed[0][k].label ? me_ed[0][k].label : "-");
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
static int32_t me_cell_new(const page_t *pg, uint32_t k, uint32_t slot, int home, const param_desc_t *d)
{
    k &= 3u;
    me_kc_new[k].pg = home ? 0 : pg;
    me_kc_new[k].slot = (uint8_t)slot;
    me_kc_new[k].home = (uint8_t)(home != 0);
    me_kd_new[k] = *d;                            /* (a desc hook may hand out one static: copied at once) */
    snprintf(me_knm_new[k], 17, "%s %s", home ? "HOME" : pg->title, d->label ? d->label : "-");
    return me_index_of(MEK_KNOB, (int32_t)k);
}
/* a named entry for what vp points at (the selected part's parameter or the song's), else knob k's own cell. A
 * named entry's write is the plain one: a value that edit_param writes its own way (a native panel's copy, a
 * device setting) is never in the arrays, so it lands on the cell */
static int32_t me_cell_target(const page_t *pg, uint32_t k, uint32_t slot, int home, const param_desc_t *d,
                              int16_t *vp)
{
    int32_t e = -1;
    if (me_in_trk(vp))
        e = me_entry_of((uint32_t)(vp - TSEL->p));
    else if (vp >= song.g && vp < song.g + G_COUNT)
        e = me_index_of(MEK_GP, (int32_t)(vp - song.g));
    if (e >= 0 && (ME_PARAMS[e].min != d->min || ME_PARAMS[e].max != d->max) && !(ME_PARAMS[e].flags & FM1P_META))
        e = -1;                                   /* (the page shows it with another range: its own cell) */
    return e >= 0 ? e : me_cell_new(pg, k, slot, home, d);
}
/* edit_param(slot, ..) on page pg for knob k (home: Stage's page) */
static int32_t me_page_target(const page_t *pg, uint32_t k, uint32_t slot, int home)
{
    int16_t *vp;
    const param_desc_t *d;
    slot &= 3u;
    if (pg->graph == GR_PATGRID || pg->graph == GR_SONG || pg->scope == SC_DRUM || pg->scope == SC_DRUMHIT ||
        pg->scope == SC_STEP || pg->graph == GR_SCALE_PICKER || (scale_settings_page(pg) && slot == 1u) ||
        pg->graph == GR_BROWSE || pg->graph == GR_SLOTS || pg->graph == GR_USER || pg->scope == SC_P5STORE ||
        pg->graph == GR_FMSTORE || (pg->graph == GR_SLICES && slot < 2u))
        return -1;                                /* a cursor, a row, a slot, a lane, a list: turned */
    if (pg->scope == SC_TRK) {                    /* MIXER (tracks_edit): LEVEL PAN - MUTE */
        static const uint8_t MIXER[4] = {P_LEVEL, P_PAN, 0xFF, P_MUTE};
        return MIXER[slot] == 0xFF ? -1 : me_entry_of(MIXER[slot]);
    }
    if (!home && ((act_cols() >> slot) & 1u))
        return -1;                                /* an action: picked, OCT+ does it */
    if ((pg->graph == GR_MOD || pg->graph == GR_FXRACK) && slot == 1u)
        return -1;                                /* the route, the rack's row */
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return -1;
    if (pg->scope == SC_CZ1 && pg->graph == GR_CZTOOLS)
        return -1;
    return me_cell_target(pg, k, slot, home, d, vp);
}
static int32_t me_knob_compute(uint32_t role)
{
    uint32_t k;
    const list_t *l;
    if (role >= NE || !emu_hal.ready || boot_work.state || ui.menu || ui.confirm || ui.uboot || name_on() ||
        new_on() || smap.on || pop.on || designer_on() || sx_import.count)
        return -1;                                /* a modal screen: its own (turned) */
    if (ui.ly && layer_allowed()) {               /* a layer armed: its knobs (held), else swallowed */
        if (role == EN_SELECT || role < EN_K1 || !layer_held())
            return -1;
        k = role - EN_K1;
        if (ui.ly == LAYER_GLO)
            return k < trk_n() ? me_index_of(MEK_TLV, (int32_t)k) : -1;
        if (ui.ly == LAYER_SCL)
            return me_page_target(&LY_SCL, k, k, 0);
        return -1;
    }
    switch (role) {
    case EN_SELECT: return ME_SELECT_TARGET;
    case EN_ALGO: return me_index_of(MEK_LIT, 0);   /* the selected part, on every page */
    case EN_PRESET: return -1;                    /* the sounds, a list, the zoom, the operator */
    default: break;
    }
    k = role - EN_K1;
    if ((fm1_in.buttons >> panel.btn[B_LFO]) & 1u)
        return -1;                                /* LFO held: a momentary edit (ui.c momentary_take) */
    if ((l = list_on()) != 0) {                   /* a list page: KNOB 1 the row's value, KNOB 2 the row */
        const lrow_t *rw;
        uint32_t p;
        if (k != 0u)
            return -1;
        rw = list_at(l, lst.row);
        if ((p = page_titled(rw->page)) >= NPAGES)
            return -1;
        return me_page_target(&PAGES[p], k, rw->slot, 0);
    }
    if (slot_kind() || motion_page())
        return -1;                                /* a slot page, MOTION: their own */
    if (ui.home)
        return me_page_target(stage_page(), k, k, 1);
    return me_page_target(cur_page(), k, k, 0);
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
            h = me_fnv(me_fnv(h, (uint32_t)(uintptr_t)me_kc_new[k].pg), me_kc_new[k].slot | me_kc_new[k].home << 8);
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
