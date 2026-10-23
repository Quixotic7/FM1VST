/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot core's Tier 2 parameter map (FM1-VST-PLAN.md 4.5; core-api/fm1core.h fm1param_t), for ChoralRoot
 * 0.25.1 and 0.26.1 (0.26 changed no parameter, only DSP and the two cores' sharing; repaired from the 44453d0 map:
 * the FX layer's pages and the reworked delay / reverb, the master filter, the KEY layer's two pages, the PLAY menu's
 * two pages, the LOCK and VIEW layers, the knobs' acceleration, the Options of 0.19 .. 0.25.1 and the manual saving
 * of 0.18).
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_choralroot.c includes this file after the firmware unit (emu_fw.c), so
 * the firmware's statics (cr_ui.c's mirror `cs`, its knob code, cr_out.c's engine `cr` and event queue, cr_fx.c's
 * xfp[], params.c's tables and formatter) are in scope here. Nothing in the submodule is modified.
 *
 * HOW A HOST WRITE REACHES THE FIRMWARE (each entry's `path` says which, the param_test prints them)
 *   - The panel's own code wherever one exists: cu_layer_knob (a layer's KNOB 1..4: the value, the posted engine
 *     event, the knob row's hot cell, the CR_TRACE line), cu_layer_pick (a picker), opt_set (an Options entry),
 *     cu_set_tempo + the SELECT meter (the view's tempo knob), the view's KNOB 1 body (voicing), the tap bodies
 *     (cu_perf_toggle, cu_fx_toggle, cu_latch_toggle, cu_lock_tap, cu_metro_toggle, BASS's), cu_midi_poll's CC
 *     bodies (levels and the bass part's sends: the path a MIDI controller takes).
 *   - A relative knob reaches an absolute target in ONE detent: the value is first placed one step short of the
 *     target (fm1_pre: on whichever side keeps it in range; nothing is posted or drawn for that placement), then the
 *     knob code turns it by +-1 and lands exactly on the target, posting, tracing and lighting the cell as a panel
 *     detent does. Since 0.22 a value knob accelerates (cr_pages.c kn_turn / kn_lin: the speed from the detents'
 *     times; OPT held snaps): the host's detent is made at the finest step, kn forced slow and unsnapped for the
 *     call and put back after it (fm1_kn_fine / fm1_kn_back), so the step is one stored step wherever the knob code
 *     goes through cu_kval (the fixed steps left, the click and loop levels' 5, are placed so). Knob code that is
 *     relative by nature (transpose, tonic, the registers) gets the whole delta.
 *   - A parameter on a page or of a mode that is not the one on screen goes through the same functions with the
 *     page / the PERF selection / the FX page switched for the call and put back afterwards, the hot cell restored:
 *     the row on screen must not light up.
 *   - THE FLASH (0.18, docs/SETTINGS.md "Manual and automatic"): the settings record saves by itself only its
 *     AUTOMATIC fields (what the Options menu alone sets: Play Style, Extension Addition, Secret Chords, Velocity,
 *     the chord layouts, the MIDI channels ..: cr_settings.c cr_settings_auto), and fm1core flash_sync does that
 *     save now. Everything else (tempo, perform, key, bass, loop, FX, the sounds) is MANUAL: written only by the
 *     global save, SAVE held on the panel, as on the device. FM1P_PERSIST marks the automatic ones only.
 *
 * READING: get() reads the UI's mirror (cs), the part / song parameters, xfp[], or the engine; for the two registers
 * the engine owns (voicing, bass register) it adds the events still queued for the audio ISR (cr_evq), so a value
 * just written reads back at once.
 *
 * THREADS: get / set / target / param_epoch / knob_target run on the clock's thread between device milliseconds (the
 * plugin's audio thread); text() only formats its argument from constant tables (and the rewritten entries'
 * double-buffered descriptors) and may run on any thread.
 *
 * THE KNOBS (knob_target, ABI 4): what each physical knob turns on the screen showing now, read from cu_knob's own
 * dispatch (fm1_knob_compute: the same tests in the same order). "turn": no value there, the host turns it as a
 * relative control (detents). MASTER is the pot everywhere (the host's absolute parameter).
 *
 *   screen               SELECT          PRESETS        ALGORITHM       KNOB1          KNOB2          KNOB3          KNOB4
 *   the view             Tempo           turn (chord    turn (bass:     Voicing        Bass Register  the mode's     Chord <effect>
 *                                        sound list)    OFF + list)                                   first knob *   (fx_sel's send)
 *   OPT held (shift)     Click Level     turn (engine)  turn (engine)   Split Point    (as the screen under it)
 *   BASS held            (as the screen) (as the screen) Bass Level    (as the screen)
 *   KEY layer, page 1    turn (page)     turn           turn            Key Tonic      Key Scale      Transpose      Single Notes
 *   KEY layer, page 2    turn (page)     turn           turn            Key Plays (H)  Keyboard (H)   Key Snap (H)   Numerals (H)
 *   LOCK layer           Tempo           turn           turn            turn           turn           turn           turn
 *   PERF layer           Perform Mode    turn           turn            the mode's row (CU_PERF_KNOB) **
 *   FX layer             FX Page (H)     turn           turn            the page's four cells ***
 *   BASS layer           Bass Mode       turn           turn            Bass Mode      Bass Register  turn (sound)   Bass Level
 *   PLAY menu, page 1    turn (page)     turn (slot)    turn (mode)     Loop Length    Loop Rate      Loop Quantize  Count-In
 *   PLAY menu, page 2    turn (page)     turn (slot)    turn (mode)     Loop Level     Metronome      turn           turn
 *   the loop editor      turn            turn           turn            turn           turn           turn           turn
 *   METRO layer          Time Signature  turn           turn            Click Level    turn           turn           turn
 *   EDIT held (engines)  turn            turn (pool)    turn            turn (pool)    turn (INIT)    turn           turn (fav)
 *   HOME held (views)    turn            turn           turn            turn (lock)    (as the view)
 *   Options              turn (cursor)   turn           turn            the row's ****  (as the view)
 *   the sound editor     turn (lanes)    turn (picker)  turn            the active lane's four cells *****
 *   SAVE menu            turn (choice)   turn           turn            turn           turn           turn           turn
 *   SAVE naming          turn           turn            turn            turn           turn           turn           turn
 *   calibrating          turn            turn           turn            turn           turn           turn           turn
 *
 *   *     Strum Rate, Slop Amount, Arp Division, Pattern Type, Harp Rate (the PERF selection's mode)
 *   **    Strum: Rate Dir Range Hold; Slop: Amount Rate Dir Range; Arp: Division Dir Gate Swing; Pattern: Type Div
 *         Gate Swing; Harp: Rate Dir Gate Range
 *   ***   Sends: Chord Drive Chorus Delay Reverb; Reverb 1: Size Damp Decay, Chord Reverb; Reverb 2: Pre-delay Mod
 *         Low Cut Width; Chorus: Rate Depth Width, Chord Chorus; Delay 1: Time (or Division: Sync) Spread (or Ratio)
 *         Feedback, Chord Delay; Delay 2: Sync Mode Tone Wow; Drive: turn turn turn Chord Drive; Filter: Low Cut High
 *         Cut Slope Filter
 *   ****  Play Style, Ext Addition, Secret Chords, Velocity, Bass Mode, Single Notes, Split Point (their entries), the
 *         other rows the hidden "Option" entry (rewritten: Chord Keys .. Output), Version .. Flash Data turn
 *   ***** a part parameter with an entry: the editor entries "Chord Attack" .. "Bass Glide" (FM1P_HIDDEN unless
 *         FM1_EXPOSE_EDITOR), Chord / Bass Level, the sends (Chord <effect> / Bass <effect>); anything else (the
 *         engine's own and deep pages, MIX 2) the hidden "Edit Knob 1..4", rewritten from the cell (ce_view / ce_param:
 *         its label, range, names; written as ce_knob writes: the deep page's set, a send's cu_send_set, or the
 *         part's parameter)
 *   (H): FM1P_HIDDEN. The PRESETS / ALGORITHM sound lists stay relative: their length follows the engine and the
 *   user's presets (no stable range). param_epoch moves whenever any of these targets (or a rewritten cell) changed:
 *   it is computed afresh at every call (a signature of the eight), so no screen change can be missed.
 *
 * FM1_EXPOSE_EDITOR (default 0): the sound editor's ENV, LFO, MOD and MIX entries for the chord and the bass part (30)
 * get host slots of their own; off, they are FM1P_HIDDEN (the knobs reach them in the editor). Off by default: the
 * plan keeps the plugin under Live's 128 parameters, and a sound edit is not saved until SAVE on the device. */
#ifndef FM1_EXPOSE_EDITOR
#define FM1_EXPOSE_EDITOR 0
#endif

enum { FM1K_LIT, FM1K_PERF, FM1K_GP, FM1K_TP, FM1K_AMT, FM1K_BSEND, FM1K_ED, FM1K_OPT, FM1K_EDK, FM1K_XFP };
/* (where init takes min / max / def from; FM1K_OPT / FM1K_EDK: rewritten by the epoch) */

static int32_t fm1_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* the value one detent short of v (in [lo, hi]) and the detent's sign that turns it into v */
static int32_t fm1_pre(int32_t v, int32_t step, int32_t lo, int32_t hi, int32_t *s)
{
    if (v - step >= lo) {
        *s = 1;
        return v - step;
    }
    *s = -1;
    return v + step <= hi ? v + step : hi;
}
/* base plus the deltas of op still queued for the audio ISR, each clamped as the engine's step function does */
static int32_t fm1_queued(uint32_t op, int32_t base, int32_t lo, int32_t hi)
{
    uint32_t i;
    for (i = cr_evq_r; i != cr_evq_w; i++)
        if (cr_evq[i % CR_EVQ].op == op)
            base = fm1_clamp(base + cr_evq[i % CR_EVQ].v, lo, hi);
    return base;
}
static void fm1_txt(char *d, const char *s, uint32_t n) { cu_cpy(d, s, n); }
static void fm1_num(char *d, int32_t v, const char *unit, uint32_t n)
{
    cu_int(d, v, 0, n);
    cu_cat(d, unit, n);
}

/* the knob's speed for a host detent: the finest step, no snap (cr_pages.c kn: what the last panel turn left), put
 * back after the call so the panel's next turn accelerates as it would have */
static __typeof__(kn) fm1_kn_saved;
static void fm1_kn_fine(void)
{
    fm1_kn_saved = kn;
    kn.share = 0;
    kn.first = 0;
    kn.snap = 0;
}
static void fm1_kn_back(void) { kn = fm1_kn_saved; }
/* a layer's knob code for a host write: kn at the finest step; the hot cell put back unless the row written is the
 * one on screen (layer l open, `shown` its page / selection) */
static void fm1_layer_turn(uint32_t l, uint32_t knob, int32_t s, int shown)
{
    __typeof__(cu_hot) hot = cu_hot;
    fm1_kn_fine();
    cu_layer_knob(l, knob, s);
    fm1_kn_back();
    if (!(shown && cu_layer() == l))
        cu_hot = hot;
}

/* ================================================================ perform == */
static uint8_t fm1_sel_of(uint32_t m)              /* the first PERF entry of mode m */
{
    uint32_t k;
    for (k = 0; k < 7u; k++)
        if (CU_PERF[k].mode == m)
            return (uint8_t)k;
    return 0;
}
static int32_t fm1_knob_of(uint32_t m, int32_t p)  /* KNOB 1..4 (0..3) of mode m carrying p, -1 none */
{
    int32_t k;
    for (k = 0; k < 4; k++)
        if (CU_PERF_KNOB[m][k] == p)
            return k;
    return -1;
}
static void fm1_perf_set(uint32_t m, int32_t p, int32_t v)
{
    uint8_t sel = cs.perf_sel;
    __typeof__(cu_hot) hot = cu_hot;
    int other = CU_PERF[sel].mode != m;
    int32_t s, k = fm1_knob_of(m, p);
    v = fm1_clamp(v, CU_PAR[p].min, CU_PAR[p].max);
    if (v == cs.par[m][p])
        return;
    if (other)
        cs.perf_sel = fm1_sel_of(m);
    /* (cu_param_turn: a value at the knob's speed, the finest step here; a choice one a detent) */
    cs.par[m][p] = (int16_t)fm1_pre(v, 1, CU_PAR[p].min, CU_PAR[p].max, &s);
    fm1_kn_fine();
    if (k >= 0)
        cu_layer_knob(L_PERF, (uint32_t)k, s);     /* the PERF layer's KNOB k+1 */
    else
        cu_param_turn(m, p, s, 0);                 /* the same function, no knob carries it */
    fm1_kn_back();
    if (other) {
        cs.perf_sel = sel;
        cu_hot = hot;
    }
}
#define FM1_PERF_FN(m, p) \
    static int32_t fm1_g_##m##_##p(void) { return cs.par[m][p]; } \
    static void fm1_s_##m##_##p(int32_t v) { fm1_perf_set(m, p, v); }
FM1_PERF_FN(CR_PM_STRUM, CR_P_RATE) FM1_PERF_FN(CR_PM_STRUM, CR_P_DIR) FM1_PERF_FN(CR_PM_STRUM, CR_P_RANGE)
FM1_PERF_FN(CR_PM_STRUM, CR_P_HOLD)
FM1_PERF_FN(CR_PM_SLOP, CR_P_AMOUNT) FM1_PERF_FN(CR_PM_SLOP, CR_P_RATE) FM1_PERF_FN(CR_PM_SLOP, CR_P_DIR)
FM1_PERF_FN(CR_PM_SLOP, CR_P_RANGE) FM1_PERF_FN(CR_PM_SLOP, CR_P_HOLD)
FM1_PERF_FN(CR_PM_ARP, CR_P_DIV) FM1_PERF_FN(CR_PM_ARP, CR_P_DIR) FM1_PERF_FN(CR_PM_ARP, CR_P_GATE)
FM1_PERF_FN(CR_PM_ARP, CR_P_SWING) FM1_PERF_FN(CR_PM_ARP, CR_P_RANGE) FM1_PERF_FN(CR_PM_ARP, CR_P_RETRIG)
FM1_PERF_FN(CR_PM_ARP, CR_P_HOLD)
FM1_PERF_FN(CR_PM_PATTERN, CR_P_PATTERN) FM1_PERF_FN(CR_PM_PATTERN, CR_P_DIV) FM1_PERF_FN(CR_PM_PATTERN, CR_P_GATE)
FM1_PERF_FN(CR_PM_PATTERN, CR_P_SWING) FM1_PERF_FN(CR_PM_PATTERN, CR_P_RANGE) FM1_PERF_FN(CR_PM_PATTERN, CR_P_ROTATE)
FM1_PERF_FN(CR_PM_PATTERN, CR_P_RETRIG) FM1_PERF_FN(CR_PM_PATTERN, CR_P_HOLD)
FM1_PERF_FN(CR_PM_HARP, CR_P_RATE) FM1_PERF_FN(CR_PM_HARP, CR_P_DIR) FM1_PERF_FN(CR_PM_HARP, CR_P_GATE)
FM1_PERF_FN(CR_PM_HARP, CR_P_RANGE) FM1_PERF_FN(CR_PM_HARP, CR_P_HOLD)

/* the value texts: the PERF knob row's (cu_perf_cells); the pattern adds the popup's name (cu_param_text) */
static const char *const FM1_DIR[6] = {"Up", "Down", "Up-down", "Down-up", "Played", "Random"};   /* cu_perf_cells */
static const char *const FM1_ONOFF[2] = {"Off", "On"};
static void fm1_t_rate(int32_t v, char *d, uint32_t n) { fm1_num(d, v, " ms", n); }
static void fm1_t_div(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_DIV[(uint32_t)fm1_clamp(v, 0, CR_DIV_COUNT - 1)], n); }
static void fm1_t_dir(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_DIR[fm1_clamp(v, 0, 5)], n); }
static void fm1_t_range(int32_t v, char *d, uint32_t n) { fm1_num(d, v, " oct", n); }
static void fm1_t_pct(int32_t v, char *d, uint32_t n) { fm1_num(d, v, "%", n); }
static void fm1_t_onoff(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_ONOFF[v != 0], n); }
static void fm1_t_int(int32_t v, char *d, uint32_t n) { cu_int(d, v, 0, n); }
static void fm1_t_plus(int32_t v, char *d, uint32_t n) { cu_int(d, v, 1, n); }
static void fm1_t_pattern(int32_t v, char *d, uint32_t n)
{
    v = fm1_clamp(v, 1, CR_NPATTERN);
    cu_2d(d, (uint32_t)v, n);
    cu_cat(d, " ", n);
    cu_cat(d, cr_pattern_name(v), n);
}

/* ================================================================ live page == */
static int32_t fm1_g_voicing(void) { return fm1_queued(CRE_VOICING, cr.voicing, CR_VOICE_MIN, CR_VOICE_MAX); }
static void fm1_s_voicing(int32_t v)               /* the view's KNOB 1 (cu_knob EN_K1) */
{
    int32_t d = fm1_clamp(v, CR_VOICE_MIN, CR_VOICE_MAX) - fm1_g_voicing();
    if (!d)
        return;
    cr_post(CRE_VOICING, 0, 0, d);
    cu.pop.until = cu_now() + CR_POPUP_MS;
    cu.pop.kind = PU_VOICING;
}
static void fm1_t_voicing(int32_t v, char *d, uint32_t n) { cu_int(d, v, 1, n); }

static int32_t fm1_g_tempo(void) { return cs.bpm; }
static void fm1_s_tempo(int32_t v)                 /* the view's SELECT (cu_knob EN_SELECT) */
{
    v = fm1_clamp(v, 20, 300);
    if (v == cs.bpm)
        return;
    cu_set_tempo(v);
    cu_popup_num(cs.bpm, 0, "", "bpm", CR_COL_WHITE, 20, 300, 14);
}
static void fm1_t_bpm(int32_t v, char *d, uint32_t n) { fm1_num(d, v, " BPM", n); }

/* a part's level: cu_midi_poll's CC 7 body (the BASS layer's KNOB 4 for the bass: below) */
static int32_t fm1_g_chord_level(void) { return trk[CR_PART_CHORD].p[P_LEVEL]; }
static void fm1_s_chord_level(int32_t v)
{
    v = fm1_clamp(v, 0, 127);
    if (v == trk[CR_PART_CHORD].p[P_LEVEL])
        return;
    fm1_irq_off();
    trk[CR_PART_CHORD].p[P_LEVEL] = (int16_t)v;
    fm1_irq_on();
    cu_popup_num(v, 0, "", "level", CR_COL_WHITE, 0, 127, 12);
    cu_trace("level: part 0 %u (host)\n", (unsigned)v);
}
static void fm1_t_tp_level(int32_t v, char *d, uint32_t n) { cp_value(&TP[P_LEVEL], v, d, n); }

/* ============================================================ chord, global == */
/* the toggles: the tap bodies (called as such, not through cu_tap: in the sound editor cu_tap hands the section
 * buttons to the editor first; LFO's tap is LOCK or LATCH by the layout) */
static int32_t fm1_g_perform(void) { return cs.perform_on; }
static void fm1_s_perform(int32_t v)
{
    if ((v != 0) != (cs.perform_on != 0))
        cu_perf_toggle();                          /* PERF tap */
}
static int32_t fm1_g_perf_mode(void) { return cs.perf_sel; }
static void fm1_s_perf_mode(int32_t v)             /* the PERF picker (cu_layer_pick L_PERF: also turns PERF on) */
{
    v = fm1_clamp(v, 0, 6);
    if (v != cs.perf_sel)
        cu_layer_pick(L_PERF, v);
}
static const char *const FM1_PERF_NAMES[7] = {"Strum", "Strum 2 Octaves", "Slop", "Arpeggiate", "Arp 2 Octaves",
                                              "Pattern", "Harp"};   /* CU_PERF[].name */
static void fm1_t_perf_mode(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_PERF_NAMES[fm1_clamp(v, 0, 6)], n); }

static int32_t fm1_g_latch(void) { return cs.sticky; }
static void fm1_s_latch(int32_t v)                 /* LATCH (Classic: LFO tap; FM-1: GLO held + LFO) */
{
    if ((v != 0) == (cs.sticky != 0))
        return;
    cu_latch_toggle();
    if ((v != 0) != (cs.sticky != 0))              /* (it turned the mode's Hold off instead: the next tap latches) */
        cu_latch_toggle();
}
static int32_t fm1_g_lock(void) { return cu.clock; }
static void fm1_s_lock(int32_t v)                  /* LOCK (FM-1: LFO tap; Classic: B3) */
{
    if ((v != 0) != (cu.clock != 0))
        cu_lock_tap();
}
static int32_t fm1_g_key_mode(void) { return cs.key_on; }
static void fm1_s_key_mode(int32_t v)
{
    if ((v != 0) == (cs.key_on != 0))
        return;
    cs.key_on ^= 1u;                               /* cu_tap BT_KEY */
    cu_post_key();
}
/* the KEY layer's knobs, its page switched for the call (cu.kpage: 0 Tonic Scale Transpose Single, 1 Key Plays
 * Keyboard Snap Numerals) */
static void fm1_key_turn(uint32_t page, uint32_t knob, int32_t s)
{
    uint8_t pg = cu.kpage;
    int shown = pg == page;
    cu.kpage = (uint8_t)page;
    fm1_layer_turn(L_KEY, knob, s, shown);
    cu.kpage = pg;
}
static int32_t fm1_g_tonic(void) { return cs.tonic; }
static void fm1_s_tonic(int32_t v)                 /* the KEY layer's KNOB 1 (turns Key Mode on, as the knob does) */
{
    v = fm1_clamp(v, 0, 11);
    if (v != cs.tonic)
        fm1_key_turn(0, 0, v - cs.tonic);
}
static void fm1_t_note(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_NOTE[fm1_clamp(v, 0, 11)], n); }
static int32_t fm1_g_scale(void) { return cs.scale; }
static void fm1_s_scale(int32_t v)                 /* the KEY layer's KNOB 2 */
{
    if ((v != 0) != (cs.scale != 0))
        fm1_key_turn(0, 1, v ? 1 : -1);
}
static const char *const FM1_SCALE[2] = {"Major", "Minor"};   /* cu_key_cells */
static void fm1_t_scale(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_SCALE[v != 0], n); }
static int32_t fm1_g_transpose(void) { return cs.transpose; }
static void fm1_s_transpose(int32_t v)             /* the KEY layer's KNOB 3 */
{
    v = fm1_clamp(v, -24, 24);
    if (v != cs.transpose)
        fm1_key_turn(0, 2, v - cs.transpose);
}
static void fm1_t_semi(int32_t v, char *d, uint32_t n)
{
    cu_int(d, v, 1, n);
    cu_cat(d, " st", n);
}
static int32_t fm1_g_single(void) { return cs.single; }
static void fm1_s_single(int32_t v)                /* the KEY layer's KNOB 4: Full Octave / Split Oct / Split Bass */
{
    v = fm1_clamp(v, 0, 2);
    if (v != cs.single)
        fm1_key_turn(0, 3, v - cs.single);
}
static const char *const FM1_SINGLE[3] = {"Full Octave", "Split Oct", "Split Bass"};   /* opt_text O_SINGLE */
static void fm1_t_single(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_SINGLE[fm1_clamp(v, 0, 2)], n); }
/* page 2: the choices (one a detent, clamped by the knob code) */
#define FM1_KEY2_FN(nm, field, knob, hi) \
    static int32_t fm1_g_##nm(void) { return cs.field; } \
    static void fm1_s_##nm(int32_t v) { v = fm1_clamp(v, 0, hi); if (v != cs.field) fm1_key_turn(1, knob, v - cs.field); }
FM1_KEY2_FN(key_plays, key_notes, 0, 1) FM1_KEY2_FN(keyboard, kbd_full, 1, 1) FM1_KEY2_FN(snap, snap, 2, 2)
FM1_KEY2_FN(numerals, numerals, 3, 2)
static const char *const FM1_KEYPLAYS[2] = {"Chords", "Notes"};
static const char *const FM1_KEYBOARD[2] = {"Chords", "Full"};
static const char *const FM1_NUMS[3] = {"Off", "On", "Solfege"};   /* CU_NUMS (its e grave written plainly) */
static void fm1_t_key_plays(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_KEYPLAYS[v != 0], n); }
static void fm1_t_keyboard(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_KEYBOARD[v != 0], n); }
static void fm1_t_snap(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_SNAP[fm1_clamp(v, 0, 2)], n); }
static void fm1_t_numerals(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_NUMS[fm1_clamp(v, 0, 2)], n); }

/* Options entries: opt_set, the Options page's own setter (its knob calls it with opt_get + steps; Velocity at the
 * knob's speed) */
#define FM1_OPT_FN(nm, o) \
    static int32_t fm1_g_##nm(void) { return opt_get(o); } \
    static void fm1_s_##nm(int32_t v) { if (v != opt_get(o)) opt_set(o, v); }
FM1_OPT_FN(split, O_SPLIT) FM1_OPT_FN(style, O_STYLE) FM1_OPT_FN(extadd, O_EXTADD) FM1_OPT_FN(secret, O_SECRET)
FM1_OPT_FN(vel, O_VEL)
static const char *const FM1_STYLE[3] = {"Simple", "Advanced", "Free"};   /* opt_text's tables */
static const char *const FM1_EXTADD[2] = {"Add Note", "Play Chord"};
static const char *const FM1_SECRET[3] = {"Off", "Simple", "All"};
static void fm1_t_style(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_STYLE[fm1_clamp(v, 0, 2)], n); }
static void fm1_t_extadd(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_EXTADD[v != 0], n); }
static void fm1_t_secret(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_SECRET[fm1_clamp(v, 0, 2)], n); }

static int32_t fm1_g_bass(void) { return cs.bass_on; }
static void fm1_s_bass(int32_t v)
{
    if ((v != 0) == (cs.bass_on != 0))
        return;
    if (!cs.bass_on && !cs.bass_sound) {           /* cu_tap BT_BASS */
        cs.bass_sound = (uint16_t)(cu_part_pos(1) + 1u);
        cs.bass_on = 1;
        cr_post(CRE_BASS, 0, 0, 1);
    } else {
        cs.bass_on ^= 1u;
        cr_post(CRE_BASS, 0, 0, cs.bass_on);
    }
    cu_sound_popup(1);
    cu_trace("bass: %s (sound %u, %s)\n", cs.bass_on ? "on" : "off", (unsigned)cs.bass_sound,
             CU_BASSMODE[cs.bass_mode & 3u]);
}
static int32_t fm1_g_bass_mode(void) { return cs.bass_mode; }
static void fm1_s_bass_mode(int32_t v)             /* the BASS picker (its KNOB 1 picks the same way) */
{
    v = fm1_clamp(v, 0, 3);
    if (v != cs.bass_mode)
        cu_layer_pick(L_BASS, v);
}
static void fm1_t_bass_mode(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_BASSMODE[fm1_clamp(v, 0, 3)], n); }
static int32_t fm1_g_bass_reg(void) { return fm1_queued(CRE_BASS_VOICING, cr.bass_voicing, CR_BV_MIN, CR_BV_MAX); }
static void fm1_s_bass_reg(int32_t v)              /* the BASS layer's KNOB 2 */
{
    int32_t d = fm1_clamp(v, CR_BV_MIN, CR_BV_MAX) - fm1_g_bass_reg();
    if (d)
        fm1_layer_turn(L_BASS, 1, d, 1);
}
static void fm1_t_oct(int32_t v, char *d, uint32_t n)
{
    cu_int(d, v, 1, n);
    cu_cat(d, " oct", n);
}
static int32_t fm1_g_bass_level(void) { return trk[CR_PART_BASS].p[P_LEVEL]; }
static void fm1_s_bass_level(int32_t v)            /* the BASS layer's KNOB 4 (at the knob's speed: the finest) */
{
    int32_t s;
    v = fm1_clamp(v, 0, 127);
    if (v == trk[CR_PART_BASS].p[P_LEVEL])
        return;
    trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)fm1_pre(v, 1, 0, 127, &s);
    fm1_layer_turn(L_BASS, 3, s, 1);
}

static int32_t fm1_g_metro(void) { return cs.metro; }
static void fm1_s_metro(int32_t v)
{
    if ((v != 0) != (cs.metro != 0))
        cu_metro_toggle();                         /* SEQ in its METRO layer, GLO + SEQ */
}
static int32_t fm1_g_click(void) { return cs.metro_vol; }
static void fm1_s_click(int32_t v)                 /* the METRO layer's KNOB 1 (steps of 5) */
{
    int32_t s;
    v = fm1_clamp(v, 0, 100);
    if (v == cs.metro_vol)
        return;
    cs.metro_vol = (uint8_t)fm1_pre(v, 5, 0, 100, &s);
    fm1_layer_turn(L_METRO, 0, s, 1);
}
static int32_t fm1_g_sig(void) { return cs.metro_sig; }
static void fm1_s_sig(int32_t v)                   /* the METRO picker */
{
    v = fm1_clamp(v, 0, CRL_NSIG - 1);
    if (v != cs.metro_sig)
        cu_layer_pick(L_METRO, v);
}
static void fm1_t_sig(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_SIG[fm1_clamp(v, 0, CRL_NSIG - 1)], n); }

/* the PLAY menu's knobs, its page switched for the call (cu.lpage: 0 Length Rate Quantize Count-In, 1 Level
 * Metronome); never into the loop editor (its knobs are the steps'): the bodies run as cu_layer_knob's with it shut */
static void fm1_loop_turn(uint32_t page, uint32_t knob, int32_t s)
{
    uint8_t pg = cu.lpage, le = cu.ledit;
    int shown = pg == page && !le;
    cu.lpage = (uint8_t)page;
    cu.ledit = 0;
    fm1_layer_turn(L_LOOP, knob, s, shown);
    cu.ledit = le;
    cu.lpage = pg;
}
static int32_t fm1_g_loop_len(void) { return cs.loop_len; }
static void fm1_s_loop_len(int32_t v)              /* the PLAY menu's KNOB 1 / its picker (while a loop plays: the next
                                                    * take's, with the message) */
{
    v = fm1_clamp(v, 0, (int32_t)CRL_NSYNC - 1);
    if (v != cs.loop_len)
        cu_layer_pick(L_LOOP, v);
}
static void fm1_t_loop_len(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_LOOPLEN[fm1_clamp(v, 0, CRL_NSYNC - 1)], n); }
static int32_t fm1_g_loop_rate(void) { return cs.loop_rate; }
static void fm1_s_loop_rate(int32_t v)             /* page 1 KNOB 2 */
{
    v = fm1_clamp(v, 0, (int32_t)CRL_NRATE - 1);
    if (v != cs.loop_rate)
        fm1_loop_turn(0, 1, v - cs.loop_rate);
}
static void fm1_t_loop_rate(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_RATE[fm1_clamp(v, 0, CRL_NRATE - 1)], n); }
static int32_t fm1_g_quant(void) { return cs.loop_quant; }
static void fm1_s_quant(int32_t v)                 /* page 1 KNOB 3 (Step mode: refused, "Step: always 1/16") */
{
    v = fm1_clamp(v, 0, (int32_t)CRL_NQUANT - 1);
    if (v != cs.loop_quant)
        fm1_loop_turn(0, 2, v - cs.loop_quant);
}
static void fm1_t_quant(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_QUANT[fm1_clamp(v, 0, CRL_NQUANT - 1)], n); }
static int32_t fm1_g_count_in(void) { return cs.loop_count_in; }
static void fm1_s_count_in(int32_t v)              /* page 1 KNOB 4 */
{
    if ((v != 0) != (cs.loop_count_in != 0))
        fm1_loop_turn(0, 3, v ? 1 : -1);
}
static int32_t fm1_g_loop_level(void) { return cs.loop_level; }
static void fm1_s_loop_level(int32_t v)            /* page 2 KNOB 1 (steps of 5) */
{
    int32_t s;
    v = fm1_clamp(v, 0, 100);
    if (v == cs.loop_level)
        return;
    cs.loop_level = (uint8_t)fm1_pre(v, 5, 0, 100, &s);
    fm1_loop_turn(1, 0, s);
}

/* ======================================================================= FX == */
static int32_t fm1_g_fx(void) { return cs.fx_on; }
static void fm1_s_fx(int32_t v)
{
    if ((v != 0) != (cs.fx_on != 0))
        cu_fx_toggle();                            /* FX tap */
}
/* the FX layer on page pg for the call (cs.fx_sel / cs.fx_page as cu_layer_pick(L_FX, pg) sets them, put back) */
static void fm1_fx_turn(uint32_t pg, uint32_t knob, int32_t s)
{
    uint8_t sel = cs.fx_sel, page = cs.fx_page;
    int shown = cu_fx_pg() == pg;
    cs.fx_page = CU_FXP[pg].page;
    if (cs.fx_page != CU_FXPG_SENDS && cs.fx_page != CU_FXPG_FILTER)
        cs.fx_sel = CU_FXP[pg].fx;
    fm1_layer_turn(L_FX, knob, s, shown);
    cs.fx_sel = sel;
    cs.fx_page = page;
}
/* the chord part's sends (cu_send: the FX amounts; FM TONE's Drive the sound's): the effect's first page's KNOB 4,
 * its "Send" cell (cu_send_step at the finest step; turns FX on, as the knob does) */
static uint32_t fm1_first_pg(uint32_t e)
{
    uint32_t i;
    for (i = 1; i + 1u < CU_NFXP; i++)
        if (CU_FXP[i].fx == e && !CU_FXP[i].page)
            return i;
    return 0;
}
static void fm1_amt_set(uint32_t e, int32_t v)
{
    const uint32_t sid = CU_FX[e].send;
    int32_t s, pre;
    v = fm1_clamp(v, 0, 127);
    if (v == cu_send(0, sid))
        return;
    pre = fm1_pre(v, 1, 0, 127, &s);
    if (cu_send_quad(0, sid))
        trk[CR_PART_CHORD].p[sid] = (int16_t)pre;
    else
        cs.fx_amt[e] = (uint8_t)pre;
    fm1_fx_turn(fm1_first_pg(e), 3, s);
}
#define FM1_AMT_FN(e) \
    static int32_t fm1_ga_##e(void) { return cu_send(0, CU_FX[e].send); } \
    static void fm1_sa_##e(int32_t v) { fm1_amt_set(e, v); }
FM1_AMT_FN(0) FM1_AMT_FN(1) FM1_AMT_FN(2) FM1_AMT_FN(3)   /* CU_FX order: Reverb Chorus Delay Drive */
static void fm1_t_amt(int32_t v, char *d, uint32_t n) { cu_send_text(fm1_clamp(v, 0, 127), d, n); }

/* the bass part's sends: cu_midi_poll's CC 91 / 93 / 94 body (Drive has no CC: the same statements) */
static void fm1_bsend_set(uint32_t id, int32_t v)
{
    uint32_t i;
    v = fm1_clamp(v, 0, 127);
    if (v == cu_send(1, id))
        return;
    for (i = 0; i < CU_NFX; i++)
        if (CU_FX[i].send == id)
            break;
    cu_send_set(1, id, v);
    cu_send_popup(v, i < CU_NFX ? CU_FX[i].name : "send", CR_COL_ORANGE);
    cu_trace("send: part 1 %s %u (host)\n", i < CU_NFX ? CU_FX[i].name : "?", (unsigned)v);
}
#define FM1_BSEND_FN(id) \
    static int32_t fm1_gb_##id(void) { return cu_send(1, id); } \
    static void fm1_sb_##id(int32_t v) { fm1_bsend_set(id, v); }
FM1_BSEND_FN(P_DIST) FM1_BSEND_FN(P_CHOR) FM1_BSEND_FN(P_DLY) FM1_BSEND_FN(P_REV)

/* the effects' own parameters (global: cr_fx.c's xfp[], the chorus's song.g), the FX pages' cells: written through
 * the page that shows the cell now (cu_fx_id: the delay's Time and Spread are the synced or the free one by Sync);
 * one no page shows (Division with Sync Free ..) is written as the knob would write it, without the knob */
static int16_t *fm1_fxq(int32_t id, int32_t *lo, int32_t *hi)   /* id: an xfp id or FC_G + a song.g id */
{
    if (id >= FC_G) {
        *lo = GP[id - FC_G].min;
        *hi = GP[id - FC_G].max;
        return &song.g[id - FC_G];
    }
    *lo = XFP[id].min;
    *hi = XFP[id].max;
    return &xfp[id];
}
static int fm1_fx_cell(int32_t id, uint32_t *pg, uint32_t *k)   /* the page and knob showing parameter id now */
{
    uint32_t i, j;
    for (i = 0; i < CU_NFXP; i++)
        for (j = 0; j < 4u; j++) {
            int32_t c0 = CU_FXP[i].c[j];
            if (c0 == FC_NONE || cu_fx_send_of(i, c0) >= 0 || cu_fx_id(c0) != id)
                continue;
            *pg = i;
            *k = j;
            return 1;
        }
    return 0;
}
static void fm1_fxp_set(int32_t id, int32_t v)
{
    int32_t lo, hi, s, pre;
    uint32_t pg, k;
    int16_t *q = fm1_fxq(id, &lo, &hi);
    v = fm1_clamp(v, lo, hi);
    int16_t sync = xfp[XP_DSYNC];
    if (v == *q)
        return;
    if (!fm1_fx_cell(id, &pg, &k)) {               /* the delay's other Time / Spread (Sync picks which the page
                                                    * shows): through the same cell, Sync switched for the call */
        xfp[XP_DSYNC] = (int16_t)!sync;
        if (!fm1_fx_cell(id, &pg, &k)) {
            xfp[XP_DSYNC] = sync;
            *q = (int16_t)v;
            cu_trace("fx: %d (host, no page shows it)\n", (int)v);
            return;
        }
    }
    /* (the knob code: a value at the knob's speed over a range wider than 20, else one a detent; Time's detent
     * turned round, clockwise is faster) */
    pre = fm1_pre(v, 1, lo, hi, &s);
    *q = (int16_t)pre;
    fm1_fx_turn(pg, k, CU_FXP[pg].c[k] == FC_TIME ? -s : s);
    xfp[XP_DSYNC] = id == XP_DSYNC ? xfp[XP_DSYNC] : sync;
}
static void fm1_fxp_text(int32_t id, int32_t v, char *d, uint32_t n)   /* cu_fx_val with v for the parameter */
{
    static const char *const RATIO[4] = {"1:1", "3:4", "2:3", "1:2"};
    int32_t lo, hi;
    (void)fm1_fxq(id, &lo, &hi);
    v = fm1_clamp(v, lo, hi);
    d[0] = 0;
    if (id >= FC_G) {
        const param_desc_t *pd = &GP[id - FC_G];
        char val[12];
        const char *unit;
        if (pd->fmt != F_LFOHZ) {
            cu_int(d, v, 0, n);
            return;
        }
        param_format(pd, v, val, &unit);
        if (val[0] == '.')
            cu_cpy(d, "0", n);
        cu_cat(d, val, n);
        cu_cat(d, " ", n);
        cu_cat(d, unit, n);
        return;
    }
    switch (id) {
    case XP_DTIME: cu_fx_unit(d, xf_ms((uint32_t)v), " ms", n); break;
    case XP_DDIV: cu_cpy(d, CU_XDIV[(uint32_t)v % XF_NDIV], n); break;
    case XP_DSPREAD:
        v = ((254 - v) * 100 + 127) / 254;
        cu_cpy(d, v >= 100 ? "x1." : "x0.", n);
        cu_2d(d + 3, (uint32_t)(v % 100), n - 3u);
        break;
    case XP_DRATIO: cu_cpy(d, RATIO[v & 3], n); break;
    case XP_DSYNC: cu_cpy(d, v ? "Tempo" : "Free", n); break;
    case XP_DMODE: cu_cpy(d, v ? "Ping-pong" : "Stereo", n); break;
    case XP_RDECAY:
        if (v >= 127) {
            cu_cpy(d, "Inf", n);
        } else {
            uint32_t ms = xf_rt_ms((uint32_t)v);
            if (ms < 10000u) {
                cu_int(d, (int32_t)(ms / 1000u), 0, n);
                cu_cat(d, ".", n);
                cu_fx_unit(d + 2, ms % 1000u / 100u, " s", n - 2u);
            } else {
                cu_fx_unit(d, ms / 1000u, " s", n);
            }
        }
        break;
    case XP_RPRE: cu_fx_unit(d, xf_pre_ms((uint32_t)v), " ms", n); break;
    case XP_RLOW: cu_fx_unit(d, xf_lowcut_hz((uint32_t)v), " Hz", n); break;
    case XP_FLOW:
        if (v <= 0)
            cu_cpy(d, "Off", n);
        else
            cu_fx_hz(d, xq_low_hz((uint32_t)v), n);
        break;
    case XP_FHIGH:
        if (v >= 127)
            cu_cpy(d, "Off", n);
        else
            cu_fx_hz(d, xq_high_hz((uint32_t)v), n);
        break;
    case XP_FSLOPE: cu_cpy(d, v ? "12 dB" : "6 dB", n); break;
    case XP_FON: cu_cpy(d, v ? "On" : "Off", n); break;
    default: cu_int(d, v, 0, n); break;
    }
}
#define FM1_FXP_FN(nm, id) \
    static int32_t fm1_gx_##nm(void) { int32_t lo, hi; return *fm1_fxq(id, &lo, &hi); } \
    static void fm1_sx_##nm(int32_t v) { fm1_fxp_set(id, v); } \
    static void fm1_tx_##nm(int32_t v, char *d, uint32_t n) { fm1_fxp_text(id, v, d, n); }
FM1_FXP_FN(rsize, XP_RSIZE) FM1_FXP_FN(rdamp, XP_RDAMP) FM1_FXP_FN(rdecay, XP_RDECAY) FM1_FXP_FN(rpre, XP_RPRE)
FM1_FXP_FN(rmod, XP_RMOD) FM1_FXP_FN(rlow, XP_RLOW) FM1_FXP_FN(rwidth, XP_RWIDTH)
FM1_FXP_FN(crate, FC_G + G_CRATE) FM1_FXP_FN(cdepth, FC_G + G_CDEPTH) FM1_FXP_FN(cwidth, XP_CWIDTH)
FM1_FXP_FN(dtime, XP_DTIME) FM1_FXP_FN(ddiv, XP_DDIV) FM1_FXP_FN(dspread, XP_DSPREAD) FM1_FXP_FN(dratio, XP_DRATIO)
FM1_FXP_FN(dfdbk, XP_DFDBK) FM1_FXP_FN(dsync, XP_DSYNC) FM1_FXP_FN(dmode, XP_DMODE) FM1_FXP_FN(dtone, XP_DTONE)
FM1_FXP_FN(dwow, XP_DWOW) FM1_FXP_FN(flow, XP_FLOW) FM1_FXP_FN(fhigh, XP_FHIGH) FM1_FXP_FN(fslope, XP_FSLOPE)
FM1_FXP_FN(fon, XP_FON)
static const char *const FM1_SYNC[2] = {"Free", "Tempo"};
static const char *const FM1_DMODE[2] = {"Stereo", "Ping-pong"};
static const char *const FM1_RATIO[4] = {"1:1", "3:4", "2:3", "1:2"};
static const char *const FM1_SLOPE[2] = {"6 dB", "12 dB"};

/* FX Page: the FX layer's picker (its SELECT): Sends, Reverb 1, Reverb 2, Chorus, Delay 1, Delay 2, Drive, Filter */
static const char *const FM1_FXPAGES[CU_NFXP] = {"Sends", "Reverb 1", "Reverb 2", "Chorus", "Delay 1", "Delay 2",
                                                 "Drive", "Filter"};   /* CU_FXP's order */
static int32_t fm1_g_fxpage(void) { return (int32_t)cu_fx_pg(); }
static void fm1_s_fxpage(int32_t v)
{
    v = fm1_clamp(v, 0, CU_NFXP - 1);
    if (v != (int32_t)cu_fx_pg())
        cu_layer_pick(L_FX, v);
}
static void fm1_t_fxpage(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_FXPAGES[fm1_clamp(v, 0, CU_NFXP - 1)], n); }

/* ============================================================ sound editor == */
/* ce_knob's write (cr_edit.c) without the page: no UI path reaches a page that is not open, so the part's
 * parameter is written as the editor writes it (IRQ off), the sound marked edited, the editor's trace line */
static void fm1_ed_set(uint32_t part, uint32_t id, int32_t v)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const param_desc_t *d = &TP[id];
    char b[12];
    int32_t v0 = t->p[id];
    v = fm1_clamp(v, d->min, d->max);
    if (v == v0)
        return;
    fm1_irq_off();
    t->p[id] = (int16_t)v;
    fm1_irq_on();
    cu_edited(part);
    cp_value(d, v, b, sizeof b);
    cu_trace("param: part %u %s %d -> %d (%s) (host)\n", (unsigned)part, d->label, (int)v0, (int)v, b);
}
#define FM1_ED_FN(part, id) \
    static int32_t fm1_ge_##part##_##id(void) { return trk[part ? CR_PART_BASS : CR_PART_CHORD].p[id]; } \
    static void fm1_se_##part##_##id(int32_t v) { fm1_ed_set(part, id, v); } \
    static void fm1_te_##part##_##id(int32_t v, char *d, uint32_t n) { cp_value(&TP[id], v, d, n); }
#define FM1_ED_PART(part) \
    FM1_ED_FN(part, P_ATK) FM1_ED_FN(part, P_DEC) FM1_ED_FN(part, P_SUS) FM1_ED_FN(part, P_REL) \
    FM1_ED_FN(part, P_LRATE) FM1_ED_FN(part, P_LWAVE) FM1_ED_FN(part, P_LD_PIT) FM1_ED_FN(part, P_LD_FLT) \
    FM1_ED_FN(part, P_ED_FLT) FM1_ED_FN(part, P_ED_PIT) FM1_ED_FN(part, P_ED_SHP) FM1_ED_FN(part, P_LD_AMP) \
    FM1_ED_FN(part, P_PAN) FM1_ED_FN(part, P_VOICE) FM1_ED_FN(part, P_GLIDE)
FM1_ED_PART(0)
FM1_ED_PART(1)

/* ================================================== the knobs' own cells == */
/* Edit Knob 1..4: the editor's cell under KNOB k when no named entry is that parameter (an engine page, a deep page,
 * MIX 2); the cell is the epoch's (fm1_ek), its descriptor double-buffered for text() (the Felucca glue's engine
 * entries do the same). Before the editor was ever open: part 0's MIX 2 row (TRANS DETUNE PRIO GLMODE). */
typedef struct { uint8_t part; ce_ref_t r; } fm1_cell_t;
static fm1_cell_t fm1_ek[4];
static param_desc_t fm1_ekd[2][4];
static char fm1_eknm[2][4][17];
static volatile uint32_t fm1_ekcur;
static uint8_t fm1_optb[2];                        /* the Options row the Option entry edits (same buffers) */
static track_t *fm1_ptrk(uint32_t part) { return &trk[part ? CR_PART_BASS : CR_PART_CHORD]; }
static int32_t fm1_ek_get(uint32_t k)
{
    int32_t v = 0;
    const fm1_cell_t *c = &fm1_ek[k & 3u];
    if (!ce_param(fm1_ptrk(c->part), c->r, &v))
        return 0;
    return v;
}
static void fm1_ek_set(uint32_t k, int32_t v)      /* ce_knob's write, the value given */
{
    const fm1_cell_t *c = &fm1_ek[k & 3u];
    const param_desc_t *d = &fm1_ekd[fm1_ekcur & 1u][k & 3u];
    track_t *t = fm1_ptrk(c->part);
    int32_t v0 = 0, sid;
    char b[12];
    if (!ce_param(t, c->r, &v0) || d->max <= d->min)
        return;
    v = fm1_clamp(v, d->min, d->max);
    if (d->fmt == F_ENUM)
        v = enum_orig(d, v);
    if (v == v0)
        return;
    if ((sid = ce_send_ref(t, c->r)) >= 0) {       /* a send: the part's one value (cu_send_set, as ce_knob) */
        cu_send_set(c->part, (uint32_t)sid, v);
        cu_send_text(v, b, sizeof b);
        cu_trace("param: part %u %s %d -> %d (%s) (host)\n", (unsigned)c->part, d->label ? d->label : "?", (int)v0,
                 (int)v, b);
        return;
    }
    if (c->r.k == CE_R_DEEP) {
        const eng_deep_t *dd = cp_deep(t);
        if (!dd)
            return;
        dd->set(t, c->r.a, c->r.b, v);
        cu_edited(c->part);
        cu_trace("deep: part %u page %u col %u %d -> %d (host)\n", (unsigned)c->part, (unsigned)c->r.a,
                 (unsigned)c->r.b, (int)v0, (int)dd->get(t, c->r.a, c->r.b));
        return;
    }
    fm1_irq_off();
    t->p[c->r.a] = (int16_t)v;
    fm1_irq_on();
    cu_edited(c->part);
    cp_value(d, v, b, sizeof b);
    cu_trace("param: part %u %s %d -> %d (%s) (host)\n", (unsigned)c->part, d->label ? d->label : "?", (int)v0, (int)v, b);
}
#define FM1_EK_FN(k) \
    static int32_t fm1_gk_##k(void) { return fm1_ek_get(k - 1); } \
    static void fm1_sk_##k(int32_t v) { fm1_ek_set(k - 1, v); } \
    static void fm1_tk_##k(int32_t v, char *d, uint32_t n) { cp_value(&fm1_ekd[fm1_ekcur & 1u][k - 1], v, d, n); }
FM1_EK_FN(1) FM1_EK_FN(2) FM1_EK_FN(3) FM1_EK_FN(4)

/* Option: the Options row KNOB 1 edits (cu.opt_sel) when no named entry is that setting; opt_get / opt_set, the
 * text opt_text's for any value (its live suffixes left out) */
static const char *const FM1_OPTNM[O_N] = {"Play Style", "Ext Addition", "Secret Chords", "Chord Keys", "SUS Window",
    "Full K Shorts", "MIDI Keys", "6th with 7th", "Velocity", "Key Touch", "Velocity Sens", "Velocity Curve",
    "Aftertouch Sens", "Bass Behaviour", "Single Notes", "Split Point", "MIDI Perform", "MIDI Bass", "MIDI Raw Chord",
    "Raw Chord Sound", "MIDI Clock", "View", "Motion", "Chord Names", "LEDs", "Hold Time", "USB Record", "USB Level",
    "USB Rate", "Dual Core", "Output", "Version", "Calibrate", "Reset Settings", "Safe Mode", "Flash Data"};
_Static_assert(sizeof FM1_OPTNM / sizeof FM1_OPTNM[0] == O_N, "an Options entry without a name");
static void fm1_opt_text(uint32_t o, int32_t v, char *d, uint32_t n)
{
    static const char *const MOTION[3] = {"Full", "Calm", "Off"};
    static const char *const LEDS[2] = {"Glow", "Stock"};
    static const char *const CLOCK[3] = {"Off", "Out", "In"};
    static const char *const SENS[3] = {"Mid", "Low", "High"};
    d[0] = 0;
    switch (o) {
    case O_STYLE: fm1_txt(d, FM1_STYLE[fm1_clamp(v, 0, 2)], n); break;
    case O_EXTADD: fm1_txt(d, FM1_EXTADD[v != 0], n); break;
    case O_SECRET: fm1_txt(d, FM1_SECRET[fm1_clamp(v, 0, 2)], n); break;
    case O_LAYOUT: fm1_txt(d, v ? "FM-1" : "Classic", n); break;
    case O_SUSWIN: fm1_num(d, 30 + 5 * v, " ms", n); break;
    case O_SIXTH: fm1_txt(d, v ? "Octave up" : "In place", n); break;
    case O_TOUCH: fm1_txt(d, v == 3 ? "Vel + Poly AT" : v == 2 ? "Vel + Aftertouch" : v ? "Dynamic" : "Fixed", n); break;
    case O_VSENS: case O_ATSENS: fm1_txt(d, SENS[fm1_clamp(v, 0, 2)], n); break;
    case O_VCURVE: cu_int(d, v - 3, 1, n); break;
    case O_BASSMODE: fm1_txt(d, CU_BASSMODE[fm1_clamp(v, 0, 3)], n); break;
    case O_SINGLE: fm1_txt(d, FM1_SINGLE[fm1_clamp(v, 0, 2)], n); break;
    case O_SPLIT: fm1_txt(d, CU_NOTE[fm1_clamp(v, 0, 11)], n); break;
    case O_FULLSHORT: case O_RAW_SOUND: case O_USB_IN: case O_DUALCORE: fm1_txt(d, FM1_ONOFF[v != 0], n); break;
    case O_MIDI_KEYS: case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW:
        if (v <= 0) {
            fm1_txt(d, "Off", n);
        } else {
            fm1_txt(d, "ch ", n);
            cu_int(d + 3, v, 0, n > 3u ? n - 3u : 0u);
        }
        break;
    case O_CLOCK: fm1_txt(d, CLOCK[fm1_clamp(v, 0, 2)], n); break;
    case O_VIEW: fm1_txt(d, CU_VIEW[fm1_clamp(v, 0, V_N - 1)], n); break;
    case O_MOTION: fm1_txt(d, MOTION[fm1_clamp(v, 0, 2)], n); break;
    case O_NAMES: fm1_txt(d, v ? "Standard" : "Orchid", n); break;
    case O_LEDS: fm1_txt(d, LEDS[v != 0], n); break;
    case O_HOLD: fm1_num(d, HOLD_MS[fm1_clamp(v, 0, 3)], " ms", n); break;
    case O_USB_LEVEL: fm1_txt(d, v ? "Fixed" : "Master", n); break;
    case O_USB_RATE: fm1_txt(d, v ? "44.1 + 48 kHz" : "44.1 kHz", n); break;
    case O_OUTPUT: fm1_txt(d, v ? "Clean" : "Classic", n); break;
    default: cu_int(d, v, 0, n); break;
    }
}
static int32_t fm1_g_option(void) { return opt_get(fm1_optb[fm1_ekcur & 1u]); }
static void fm1_s_option(int32_t v)
{
    uint32_t o = fm1_optb[fm1_ekcur & 1u];
    if (O_MAX[o] > 0 && v != opt_get(o))
        opt_set(o, v);
}
static void fm1_t_option(int32_t v, char *d, uint32_t n) { fm1_opt_text(fm1_optb[fm1_ekcur & 1u], v, d, n); }

/* ================================================================ the map == */
/* X(name, kind, a, b, min, max, def, names, unit, get, set, text, flags, path); kind FM1K_PERF: (mode, param), its
 * range from CU_PAR and default from cr_engine.c's CR_PAR_DEFAULT; FM1K_GP / FM1K_TP: (id) from params.c's GP / TP;
 * FM1K_XFP: (an xfp id, or FC_G + a song.g id) from cr_fx.c's XFP / GP; FM1K_LIT: as written */
#define P_ (FM1P_PERSIST)
#define E_ (FM1P_ENUM)
#define FM1_PERF(X, nm, m, p, txt, nms, un) \
    X(nm, FM1K_PERF, m, p, 0, 0, 0, nms, un, fm1_g_##m##_##p, fm1_s_##m##_##p, txt, 0, \
      "perf: cu_layer_knob(L_PERF) / cu_param_turn, mode switched for the call")
/* an FX page's cell; fl 0 or FM1P_HIDDEN (the less played ones: a knob target only, the slots stay under 79) */
#define FM1_FXP(X, nm, nmf, id, nms, fl) \
    X(nm, FM1K_XFP, id, 0, 0, 0, 0, nms, "", fm1_gx_##nmf, fm1_sx_##nmf, fm1_tx_##nmf, fl, \
      "fx: cu_layer_knob(L_FX) on the page that shows it, the page picked for the call")
#define FM1_BSEND(X, nm, id) \
    X(nm, FM1K_BSEND, id, 0, 0, 127, 0, 0, "", fm1_gb_##id, fm1_sb_##id, fm1_t_amt, FM1P_SOUND, \
      "cc: cu_midi_poll's send body (cu_send_set)")
#define FM1_AMT(X, nm, e) \
    X(nm, FM1K_AMT, e, 0, 0, 127, 0, 0, "", fm1_ga_##e, fm1_sa_##e, fm1_t_amt, FM1P_SOUND, \
      "fx: cu_layer_knob(L_FX, KNOB 4: the Send cell), the effect's page picked for the call")
/* the editor's pages: knob targets in the editor (FM1P_HIDDEN: no host slot of their own) unless FM1_EXPOSE_EDITOR */
#define H_ (FM1_EXPOSE_EDITOR ? 0u : FM1P_HIDDEN)
#define HD_ (FM1P_HIDDEN)
#define FM1_ED(X, nm, part, id, nms) \
    X(nm, FM1K_ED, id, part, 0, 0, 0, nms, "", fm1_ge_##part##_##id, fm1_se_##part##_##id, fm1_te_##part##_##id, \
      FM1P_EDITOR | FM1P_SOUND | H_, "direct: ce_knob's write, IRQ off, sound marked edited")
#define FM1_ED_LIST(X, part, pre) \
    FM1_ED(X, pre "Attack", part, P_ATK, 0) FM1_ED(X, pre "Decay", part, P_DEC, 0) \
    FM1_ED(X, pre "Sustain", part, P_SUS, 0) FM1_ED(X, pre "Release", part, P_REL, 0) \
    FM1_ED(X, pre "LFO Rate", part, P_LRATE, 0) FM1_ED(X, pre "LFO Wave", part, P_LWAVE, N_LWAVE) \
    FM1_ED(X, pre "Vibrato", part, P_LD_PIT, 0) FM1_ED(X, pre "Wah", part, P_LD_FLT, 0) \
    FM1_ED(X, pre "Env>Filter", part, P_ED_FLT, 0) FM1_ED(X, pre "Env>Pitch", part, P_ED_PIT, 0) \
    FM1_ED(X, pre "Env>Shape", part, P_ED_SHP, 0) FM1_ED(X, pre "Tremolo", part, P_LD_AMP, 0) \
    FM1_ED(X, pre "Pan", part, P_PAN, 0) FM1_ED(X, pre "Voice", part, P_VOICE, N_VOICE) \
    FM1_ED(X, pre "Glide", part, P_GLIDE, 0)
#define FM1_EDITOR_ENTRIES(X) FM1_ED_LIST(X, 0, "Chord ") FM1_ED_LIST(X, 1, "Bass ")
/* the cells no named entry covers (an engine's own and deep pages, MIX 2, Options rows): one entry per knob (and one
 * for the Options row), its name, range and texts rewritten from the cell on screen whenever it changes */
#define FM1_EDK(X, k) \
    X("Edit Knob " #k, FM1K_EDK, k, 0, 0, 1, 0, 0, "", fm1_gk_##k, fm1_sk_##k, fm1_tk_##k, FM1P_SOUND | FM1P_HIDDEN, \
      "edit: the cell under KNOB " #k " (ce_knob's write: the part's parameter, a send, or the deep page's set)")
#define FM1_HIDDEN_ENTRIES(X) \
    FM1_EDK(X, 1) FM1_EDK(X, 2) FM1_EDK(X, 3) FM1_EDK(X, 4) \
    X("Option", FM1K_OPT, 0, 0, 0, 1, 0, 0, "", fm1_g_option, fm1_s_option, fm1_t_option, P_ | FM1P_HIDDEN, \
      "opt: opt_set(the row KNOB 1 edits)") \
    X("FX Page", FM1K_LIT, 0, 0, 0, CU_NFXP - 1, 0, FM1_FXPAGES, "", fm1_g_fxpage, fm1_s_fxpage, fm1_t_fxpage, \
      E_ | HD_, "pick: cu_layer_pick(L_FX)") \
    X("Key Plays", FM1K_LIT, 0, 0, 0, 1, 0, FM1_KEYPLAYS, "", fm1_g_key_plays, fm1_s_key_plays, fm1_t_key_plays, \
      E_ | HD_, "key: cu_layer_knob(L_KEY, page 2 KNOB 1)") \
    X("Keyboard", FM1K_LIT, 0, 0, 0, 1, 0, FM1_KEYBOARD, "", fm1_g_keyboard, fm1_s_keyboard, fm1_t_keyboard, \
      E_ | HD_, "key: cu_layer_knob(L_KEY, page 2 KNOB 2)") \
    X("Key Snap", FM1K_LIT, 0, 0, 0, 2, 0, CU_SNAP, "", fm1_g_snap, fm1_s_snap, fm1_t_snap, E_ | HD_, \
      "key: cu_layer_knob(L_KEY, page 2 KNOB 3)") \
    X("Numerals", FM1K_LIT, 0, 0, 0, 2, 0, FM1_NUMS, "", fm1_g_numerals, fm1_s_numerals, fm1_t_numerals, E_ | HD_, \
      "key: cu_layer_knob(L_KEY, page 2 KNOB 4)")

#define FM1_LIST(X) \
    /* ---- the live page's values (the eight physical knobs come first in the host: knob_target) */ \
    X("Voicing", FM1K_LIT, 0, 0, CR_VOICE_MIN, CR_VOICE_MAX, 0, 0, "", fm1_g_voicing, fm1_s_voicing, fm1_t_voicing, 0, \
      "view KNOB 1: CRE_VOICING + the voicing meter") \
    X("Tempo", FM1K_LIT, 0, 0, 20, 300, 120, 0, "BPM", fm1_g_tempo, fm1_s_tempo, fm1_t_bpm, 0, \
      "view SELECT: cu_set_tempo + the bpm meter") \
    X("Transpose", FM1K_LIT, 0, 0, -24, 24, 0, 0, "st", fm1_g_transpose, fm1_s_transpose, fm1_t_semi, 0, \
      "key: cu_layer_knob(L_KEY, page 1 KNOB 3)") \
    X("Chord Level", FM1K_TP, P_LEVEL, 0, 0, 0, 0, 0, "", fm1_g_chord_level, fm1_s_chord_level, fm1_t_tp_level, 0, \
      "cc: cu_midi_poll's CC 7 body") \
    /* ---- the perform parameters, mode by mode (the ones the engine uses; knob-row ones first) */ \
    FM1_PERF(X, "Strum Rate", CR_PM_STRUM, CR_P_RATE, fm1_t_rate, 0, "ms") \
    FM1_PERF(X, "Strum Dir", CR_PM_STRUM, CR_P_DIR, fm1_t_dir, FM1_DIR, "") \
    FM1_PERF(X, "Strum Range", CR_PM_STRUM, CR_P_RANGE, fm1_t_range, 0, "oct") \
    FM1_PERF(X, "Strum Hold", CR_PM_STRUM, CR_P_HOLD, fm1_t_onoff, FM1_ONOFF, "") \
    FM1_PERF(X, "Slop Amount", CR_PM_SLOP, CR_P_AMOUNT, fm1_t_pct, 0, "%") \
    FM1_PERF(X, "Slop Rate", CR_PM_SLOP, CR_P_RATE, fm1_t_rate, 0, "ms") \
    FM1_PERF(X, "Slop Dir", CR_PM_SLOP, CR_P_DIR, fm1_t_dir, FM1_DIR, "") \
    FM1_PERF(X, "Slop Range", CR_PM_SLOP, CR_P_RANGE, fm1_t_range, 0, "oct") \
    FM1_PERF(X, "Slop Hold", CR_PM_SLOP, CR_P_HOLD, fm1_t_onoff, FM1_ONOFF, "") \
    FM1_PERF(X, "Arp Division", CR_PM_ARP, CR_P_DIV, fm1_t_div, CU_DIV, "") \
    FM1_PERF(X, "Arp Dir", CR_PM_ARP, CR_P_DIR, fm1_t_dir, FM1_DIR, "") \
    FM1_PERF(X, "Arp Gate", CR_PM_ARP, CR_P_GATE, fm1_t_pct, 0, "%") \
    FM1_PERF(X, "Arp Swing", CR_PM_ARP, CR_P_SWING, fm1_t_pct, 0, "%") \
    FM1_PERF(X, "Arp Range", CR_PM_ARP, CR_P_RANGE, fm1_t_range, 0, "oct") \
    FM1_PERF(X, "Arp Retrig", CR_PM_ARP, CR_P_RETRIG, fm1_t_onoff, FM1_ONOFF, "") \
    FM1_PERF(X, "Arp Hold", CR_PM_ARP, CR_P_HOLD, fm1_t_onoff, FM1_ONOFF, "") \
    FM1_PERF(X, "Pattern Type", CR_PM_PATTERN, CR_P_PATTERN, fm1_t_pattern, 0, "") \
    FM1_PERF(X, "Pattern Div", CR_PM_PATTERN, CR_P_DIV, fm1_t_div, CU_DIV, "") \
    FM1_PERF(X, "Pattern Gate", CR_PM_PATTERN, CR_P_GATE, fm1_t_pct, 0, "%") \
    FM1_PERF(X, "Pattern Swing", CR_PM_PATTERN, CR_P_SWING, fm1_t_pct, 0, "%") \
    FM1_PERF(X, "Pattern Range", CR_PM_PATTERN, CR_P_RANGE, fm1_t_range, 0, "oct") \
    FM1_PERF(X, "Pattern Rotate", CR_PM_PATTERN, CR_P_ROTATE, fm1_t_plus, 0, "") \
    FM1_PERF(X, "Pattern Retrig", CR_PM_PATTERN, CR_P_RETRIG, fm1_t_onoff, FM1_ONOFF, "") \
    FM1_PERF(X, "Pattern Hold", CR_PM_PATTERN, CR_P_HOLD, fm1_t_onoff, FM1_ONOFF, "") \
    FM1_PERF(X, "Harp Rate", CR_PM_HARP, CR_P_RATE, fm1_t_rate, 0, "ms") \
    FM1_PERF(X, "Harp Dir", CR_PM_HARP, CR_P_DIR, fm1_t_dir, FM1_DIR, "") \
    FM1_PERF(X, "Harp Gate", CR_PM_HARP, CR_P_GATE, fm1_t_pct, 0, "%") \
    FM1_PERF(X, "Harp Range", CR_PM_HARP, CR_P_RANGE, fm1_t_range, 0, "oct") \
    FM1_PERF(X, "Harp Hold", CR_PM_HARP, CR_P_HOLD, fm1_t_onoff, FM1_ONOFF, "") \
    /* ---- chord, global (P_: the record's automatic fields, the Options menu's) */ \
    X("Perform", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_perform, fm1_s_perform, fm1_t_onoff, E_, \
      "tap: cu_perf_toggle (PERF)") \
    X("Perform Mode", FM1K_LIT, 0, 0, 0, 6, 0, FM1_PERF_NAMES, "", fm1_g_perf_mode, fm1_s_perf_mode, fm1_t_perf_mode, \
      E_, "pick: cu_layer_pick(L_PERF)") \
    X("Latch", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_latch, fm1_s_latch, fm1_t_onoff, E_, \
      "tap: cu_latch_toggle (Classic: LFO; FM-1: GLO + LFO)") \
    X("Lock", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_lock, fm1_s_lock, fm1_t_onoff, E_, \
      "tap: cu_lock_tap (FM-1: LFO; Classic: B3)") \
    X("Key Mode", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_key_mode, fm1_s_key_mode, fm1_t_onoff, E_, \
      "tap: cu_tap BT_KEY body") \
    X("Key Tonic", FM1K_LIT, 0, 0, 0, 11, 0, CU_NOTE, "", fm1_g_tonic, fm1_s_tonic, fm1_t_note, E_, \
      "key: cu_layer_knob(L_KEY, page 1 KNOB 1)") \
    X("Key Scale", FM1K_LIT, 0, 0, 0, 1, 0, FM1_SCALE, "", fm1_g_scale, fm1_s_scale, fm1_t_scale, E_, \
      "key: cu_layer_knob(L_KEY, page 1 KNOB 2)") \
    X("Single Notes", FM1K_LIT, 0, 0, 0, 2, 0, FM1_SINGLE, "", fm1_g_single, fm1_s_single, fm1_t_single, E_, \
      "key: cu_layer_knob(L_KEY, page 1 KNOB 4)") \
    X("Split Point", FM1K_LIT, 0, 0, 0, 11, 0, CU_NOTE, "", fm1_g_split, fm1_s_split, fm1_t_note, E_, \
      "opt: opt_set(O_SPLIT)") \
    X("Play Style", FM1K_LIT, 0, 0, 0, 2, 0, FM1_STYLE, "", fm1_g_style, fm1_s_style, fm1_t_style, P_ | E_, \
      "opt: opt_set(O_STYLE)") \
    X("Ext Addition", FM1K_LIT, 0, 0, 0, 1, 0, FM1_EXTADD, "", fm1_g_extadd, fm1_s_extadd, fm1_t_extadd, P_ | E_, \
      "opt: opt_set(O_EXTADD)") \
    X("Secret Chords", FM1K_LIT, 0, 0, 0, 2, 0, FM1_SECRET, "", fm1_g_secret, fm1_s_secret, fm1_t_secret, P_ | E_, \
      "opt: opt_set(O_SECRET)") \
    X("Velocity", FM1K_LIT, 0, 0, 1, 127, 100, 0, "", fm1_g_vel, fm1_s_vel, fm1_t_int, P_, "opt: opt_set(O_VEL)") \
    X("Bass", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_bass, fm1_s_bass, fm1_t_onoff, E_, \
      "tap: cu_tap BT_BASS body") \
    X("Bass Mode", FM1K_LIT, 0, 0, 0, 3, 0, CU_BASSMODE, "", fm1_g_bass_mode, fm1_s_bass_mode, fm1_t_bass_mode, \
      E_, "pick: cu_layer_pick(L_BASS)") \
    X("Bass Register", FM1K_LIT, 0, 0, CR_BV_MIN, CR_BV_MAX, 0, 0, "oct", fm1_g_bass_reg, fm1_s_bass_reg, fm1_t_oct, 0, \
      "bass: cu_layer_knob(L_BASS, KNOB 2)") \
    X("Bass Level", FM1K_TP, P_LEVEL, 0, 0, 0, 0, 0, "", fm1_g_bass_level, fm1_s_bass_level, fm1_t_tp_level, 0, \
      "bass: cu_layer_knob(L_BASS, KNOB 4)") \
    X("Metronome", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_metro, fm1_s_metro, fm1_t_onoff, E_, \
      "tap: cu_metro_toggle (SEQ in its layer)") \
    X("Click Level", FM1K_LIT, 0, 0, 0, 100, 0, 0, "%", fm1_g_click, fm1_s_click, fm1_t_pct, 0, \
      "metro: cu_layer_knob(L_METRO, KNOB 1)") \
    X("Time Signature", FM1K_LIT, 0, 0, 0, CRL_NSIG - 1, 0, CU_SIG, "", fm1_g_sig, fm1_s_sig, fm1_t_sig, E_, \
      "pick: cu_layer_pick(L_METRO)") \
    X("Loop Length", FM1K_LIT, 0, 0, 0, CRL_NSYNC - 1, 0, CU_LOOPLEN, "", fm1_g_loop_len, fm1_s_loop_len, \
      fm1_t_loop_len, E_, "pick: cu_layer_pick(L_LOOP)") \
    X("Loop Rate", FM1K_LIT, 0, 0, 0, CRL_NRATE - 1, CRL_RATE_1, CU_RATE, "", fm1_g_loop_rate, fm1_s_loop_rate, \
      fm1_t_loop_rate, E_, "loop: cu_layer_knob(L_LOOP, page 1 KNOB 2)") \
    X("Loop Quantize", FM1K_LIT, 0, 0, 0, CRL_NQUANT - 1, 0, CU_QUANT, "", fm1_g_quant, fm1_s_quant, fm1_t_quant, \
      E_, "loop: cu_layer_knob(L_LOOP, page 1 KNOB 3)") \
    X("Count-In", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_count_in, fm1_s_count_in, fm1_t_onoff, E_, \
      "loop: cu_layer_knob(L_LOOP, page 1 KNOB 4)") \
    X("Loop Level", FM1K_LIT, 0, 0, 0, 100, 0, 0, "%", fm1_g_loop_level, fm1_s_loop_level, fm1_t_pct, 0, \
      "loop: cu_layer_knob(L_LOOP, page 2 KNOB 1)") \
    /* ---- FX: on / off, the chord part's sends (the FX amounts), the bass part's, the effects' own */ \
    X("FX", FM1K_LIT, 0, 0, 0, 1, 1, FM1_ONOFF, "", fm1_g_fx, fm1_s_fx, fm1_t_onoff, E_, "tap: cu_fx_toggle (FX)") \
    FM1_AMT(X, "Chord Drive", 3) FM1_AMT(X, "Chord Chorus", 1) FM1_AMT(X, "Chord Delay", 2) \
    FM1_AMT(X, "Chord Reverb", 0) \
    FM1_BSEND(X, "Bass Drive", P_DIST) FM1_BSEND(X, "Bass Chorus", P_CHOR) FM1_BSEND(X, "Bass Delay", P_DLY) \
    FM1_BSEND(X, "Bass Reverb", P_REV) \
    FM1_FXP(X, "Reverb Size", rsize, XP_RSIZE, 0, 0) FM1_FXP(X, "Reverb Damp", rdamp, XP_RDAMP, 0, 0) \
    FM1_FXP(X, "Reverb Decay", rdecay, XP_RDECAY, 0, 0) FM1_FXP(X, "Reverb Pre-delay", rpre, XP_RPRE, 0, HD_) \
    FM1_FXP(X, "Reverb Mod", rmod, XP_RMOD, 0, HD_) FM1_FXP(X, "Reverb Low Cut", rlow, XP_RLOW, 0, HD_) \
    FM1_FXP(X, "Reverb Width", rwidth, XP_RWIDTH, 0, HD_) \
    FM1_FXP(X, "Chorus Rate", crate, FC_G + G_CRATE, 0, 0) FM1_FXP(X, "Chorus Depth", cdepth, FC_G + G_CDEPTH, 0, 0) \
    FM1_FXP(X, "Chorus Width", cwidth, XP_CWIDTH, 0, HD_) \
    FM1_FXP(X, "Delay Time", dtime, XP_DTIME, 0, 0) FM1_FXP(X, "Delay Division", ddiv, XP_DDIV, CU_XDIV, HD_) \
    FM1_FXP(X, "Delay Spread", dspread, XP_DSPREAD, 0, HD_) FM1_FXP(X, "Delay Ratio", dratio, XP_DRATIO, FM1_RATIO, HD_) \
    FM1_FXP(X, "Delay Feedback", dfdbk, XP_DFDBK, 0, 0) FM1_FXP(X, "Delay Sync", dsync, XP_DSYNC, FM1_SYNC, 0) \
    FM1_FXP(X, "Delay Mode", dmode, XP_DMODE, FM1_DMODE, HD_) FM1_FXP(X, "Delay Tone", dtone, XP_DTONE, 0, HD_) \
    FM1_FXP(X, "Delay Wow", dwow, XP_DWOW, 0, HD_) \
    FM1_FXP(X, "Filter Low Cut", flow, XP_FLOW, 0, 0) FM1_FXP(X, "Filter High Cut", fhigh, XP_FHIGH, 0, 0) \
    FM1_FXP(X, "Filter Slope", fslope, XP_FSLOPE, FM1_SLOPE, HD_) FM1_FXP(X, "Filter", fon, XP_FON, FM1_ONOFF, HD_) \
    /* ---- the sound editor (FM1P_HIDDEN unless FM1_EXPOSE_EDITOR), the knobs' own cells */ \
    FM1_EDITOR_ENTRIES(X) FM1_HIDDEN_ENTRIES(X)

#define FM1_AS_PARAM(nm, kind, a, b, mn, mx, df, nms, un, g, s, t, fl, pa) {nm, mn, mx, df, nms, un, g, s, t, 0, fl, pa},
#define FM1_AS_SRC(nm, kind, a, b, ...) {kind, a, b},
static fm1param_t FM1_PARAMS[] = {FM1_LIST(FM1_AS_PARAM)};
static const struct { uint8_t kind; int16_t a, b; } FM1_SRC[] = {FM1_LIST(FM1_AS_SRC)};
#define FM1_NPARAMS ((uint32_t)(sizeof FM1_PARAMS / sizeof FM1_PARAMS[0]))

/* ============================================================ the knobs == */
/* knob_target(role): what each physical knob turns on the screen showing now, cu_knob's dispatch read without
 * turning (the same tests in the same order; the table in the file's header). Evaluated by param_epoch() (once per
 * UI frame in the plugin), which moves when any knob's target, or a rewritten cell's descriptor, changed. */
static int32_t fm1_idx_get(int32_t (*g)(void))
{
    uint32_t i;
    for (i = 0; i < FM1_NPARAMS; i++)
        if (FM1_PARAMS[i].get == g)
            return (int32_t)i;
    return -1;
}
static int32_t fm1_idx_src(uint32_t kind, int32_t a, int32_t b)
{
    uint32_t i;
    for (i = 0; i < FM1_NPARAMS; i++)
        if (FM1_SRC[i].kind == kind && FM1_SRC[i].a == a && FM1_SRC[i].b == b)
            return (int32_t)i;
    return -1;
}
static int32_t fm1_idx_perf(uint32_t m, int32_t p) { return p < 0 ? -1 : fm1_idx_src(FM1K_PERF, (int32_t)m, p); }
static int32_t fm1_idx_send(uint32_t part, uint32_t id)   /* the entry of a part's send into (P_DIST .. P_REV) */
{
    uint32_t i;
    if (part)
        return fm1_idx_src(FM1K_BSEND, (int32_t)id, 0);
    for (i = 0; i < CU_NFX; i++)
        if (CU_FX[i].send == id)
            return fm1_idx_src(FM1K_AMT, (int32_t)i, 0);
    return -1;
}

/* the candidates of the rewritten entries, filled while the targets are computed (the epoch commits them) */
static fm1_cell_t fm1_ek_new[4];
static param_desc_t fm1_ekd_new[4];
static char fm1_eknm_new[4][17];
static uint8_t fm1_opt_new;

static int32_t fm1_edit_cell(uint32_t k)          /* ce_knob(k): the active lane's cell in column k */
{
    uint32_t part = ce.part & 1u;
    track_t *t = fm1_ptrk(part);
    ce_view_t vw;
    ce_ref_t r;
    const param_desc_t *d;
    int32_t v = 0, e, sid;
    k &= 3u;
    ce_view(t, part, &vw);
    r = vw.ref[vw.active][k];
    if (!(d = ce_param(t, r, &v)) || d->max <= d->min)
        return -1;
    if ((sid = ce_send_ref(t, r)) >= 0)            /* a send: the part's own entry */
        return fm1_idx_send(part, (uint32_t)sid);
    if (r.k == CE_R_TRK) {                         /* a parameter some entry is already */
        if ((e = fm1_idx_src(FM1K_ED, r.a, (int32_t)part)) >= 0)
            return e;
        if (r.a == P_LEVEL)
            return fm1_idx_get(part ? fm1_g_bass_level : fm1_g_chord_level);
    }
    fm1_ek_new[k].part = (uint8_t)part;
    fm1_ek_new[k].r = r;
    fm1_ekd_new[k] = *d;                           /* (a desc hook may hand out one static: copied at once) */
    ce_label(t, r, d, fm1_eknm_new[k], sizeof fm1_eknm_new[k]);
    return fm1_idx_src(FM1K_EDK, (int32_t)k + 1, 0);
}
static int32_t fm1_option(uint32_t o)             /* Options: KNOB 1 on row o */
{
    switch (o) {
    case O_STYLE: return fm1_idx_get(fm1_g_style);
    case O_EXTADD: return fm1_idx_get(fm1_g_extadd);
    case O_SECRET: return fm1_idx_get(fm1_g_secret);
    case O_VEL: return fm1_idx_get(fm1_g_vel);
    case O_BASSMODE: return fm1_idx_get(fm1_g_bass_mode);
    case O_SINGLE: return fm1_idx_get(fm1_g_single);
    case O_SPLIT: return fm1_idx_get(fm1_g_split);
    default: break;
    }
    if (o >= cu_opt_n() || O_MAX[o] <= 0)          /* Version, Calibrate, Reset, Safe Mode, Flash Data: OCT+ acts */
        return -1;
    fm1_opt_new = (uint8_t)o;
    return fm1_idx_get(fm1_g_option);
}
static int32_t fm1_fx_target(uint32_t k)           /* the FX layer's KNOB k: the page's cell */
{
    uint32_t pg = cu_fx_pg();
    int32_t c0 = CU_FXP[pg].c[k & 3u], e = cu_fx_send_of(pg, c0), id;
    if (c0 == FC_NONE)
        return -1;
    if (e >= 0)
        return fm1_idx_src(FM1K_AMT, e, 0);
    id = cu_fx_id(c0);
    return fm1_idx_src(FM1K_XFP, id, 0);
}
static int32_t fm1_layer_knob_target(uint32_t l, uint32_t k)   /* cu_layer_knob(l, k) */
{
    static int32_t (*const KEY[2][4])(void) = {{fm1_g_tonic, fm1_g_scale, fm1_g_transpose, fm1_g_single},
                                                {fm1_g_key_plays, fm1_g_keyboard, fm1_g_snap, fm1_g_numerals}};
    static int32_t (*const BASS[4])(void) = {fm1_g_bass_mode, fm1_g_bass_reg, 0, fm1_g_bass_level};
    static int32_t (*const LOOP[2][4])(void) = {{fm1_g_loop_len, fm1_g_loop_rate, fm1_g_quant, fm1_g_count_in},
                                                 {fm1_g_loop_level, fm1_g_metro, 0, 0}};
    k &= 3u;
    switch (l) {
    case L_KEY: return fm1_idx_get(KEY[cu.kpage ? 1 : 0][k]);
    case L_PERF: return fm1_idx_perf(cu_perf_mode(), CU_PERF_KNOB[cu_perf_mode()][k]);
    case L_FX: return fm1_fx_target(k);
    case L_BASS: return BASS[k] ? fm1_idx_get(BASS[k]) : -1;   /* KNOB 3: the bass sound, a list: turned */
    case L_LOOP:
        if (cu.ledit)                              /* the loop editor: the steps' */
            return -1;
        return LOOP[cu.lpage ? 1 : 0][k] ? fm1_idx_get(LOOP[cu.lpage ? 1 : 0][k]) : -1;
    case L_METRO: return k == 0u ? fm1_idx_get(fm1_g_click) : -1;
    default: return -1;                            /* L_EDIT (pool, INIT, fav), L_LOCK and L_VIEW (choices) */
    }
}
static int32_t fm1_layer_select_target(uint32_t l)   /* SELECT in layer l: its picker */
{
    switch (l) {
    case L_PERF: return fm1_idx_get(fm1_g_perf_mode);
    case L_FX: return fm1_idx_get(fm1_g_fxpage);
    case L_BASS: return fm1_idx_get(fm1_g_bass_mode);
    case L_METRO: return fm1_idx_get(fm1_g_sig);
    default: return -1;                            /* L_EDIT: the engines; L_VIEW: the views */
    }
}
static int32_t fm1_knob_compute(uint32_t role)    /* cu_knob(role, ..) without the turn */
{
    uint32_t l;
    if (role >= NE || cc.on)                       /* (MASTER; calibrating: every knob is being taught) */
        return -1;
    if (cu.page == PG_SAVE && ce.save_step && !cu_layer())
        return -1;                                 /* naming a sound: the name's */
    if (role == EN_ALGO && cu.armed == BT_BASS && ((cu.bheld >> BT_BASS) & 1u) && !(cu.ledit && cu.lock == L_LOOP)
#if CR_EDIT_HOOKS
        && !(cu.page == PG_EDIT && !cu_layer() && ce_owns(BT_BASS))
#endif
    )
        return fm1_idx_get(fm1_g_bass_level);      /* BASS held + ALGORITHM (cu_bass_level_hold) */
    l = cu_layer();
    if (cu.ledit && l == L_LOOP && cu_shift() && (role == EN_PRESET || role == EN_ALGO || role == EN_K2 || role == EN_K3))
        return -1;                                 /* the loop editor, GLO held: its jumps and snaps */
    if (cu_shift() && cu.page == PG_EDIT && (role >= EN_K1 || role == EN_SELECT) && !l)
        return role == EN_SELECT ? -1 : fm1_edit_cell(role - EN_K1);
    if (cu_shift() && (role == EN_PRESET || role == EN_ALGO) && cu_epk_ok(role == EN_ALGO))
        return -1;                                 /* OPT + PRESETS / ALGORITHM: the engine picker */
    if (cu_shift() && (role == EN_K1 || role == EN_SELECT))
        return fm1_idx_get(role == EN_K1 ? fm1_g_split : fm1_g_click);
#if CR_EDIT_HOOKS
    if (role == EN_PRESET && !l && cu.page == PG_EDIT)
        return -1;                                 /* the picker, previewing */
#endif
    if (l == L_EDIT && role == EN_PRESET)
        return -1;
    if (l && role >= EN_K1)
        return fm1_layer_knob_target(l, role - EN_K1);
    if (l == L_LOOP && (role == EN_PRESET || role == EN_ALGO || role == EN_SELECT))
        return -1;                                 /* the PLAY menu: the slot, the record mode, its page */
    if (l == L_KEY && role == EN_SELECT)
        return -1;                                 /* the KEY layer: its page */
    if (l && role == EN_SELECT && l != L_LOCK)
        return fm1_layer_select_target(l);
    if (cu.opt_open && (role == EN_SELECT || role == EN_K1))
        return role == EN_SELECT ? -1 : fm1_option(cu.opt_sel);
    if (cu.page == PG_EDIT && (role == EN_SELECT || role >= EN_K1))
        return role == EN_SELECT ? -1 : fm1_edit_cell(role - EN_K1);
    if (cu.page == PG_SAVE && !ce.save_step && (role == EN_SELECT || role >= EN_K1))
        return -1;                                 /* the SAVE menu: its choice, the mark, the filter, the type */
    switch (role) {
    case EN_K1: return fm1_idx_get(fm1_g_voicing);
    case EN_K2: return fm1_idx_get(fm1_g_bass_reg);
    case EN_K3: return fm1_idx_perf(cu_perf_mode(), CU_PERF_KNOB[cu_perf_mode()][0]);
    case EN_K4: return fm1_idx_src(FM1K_AMT, (int32_t)(cs.fx_sel % CU_NFX), 0);
    case EN_SELECT: return fm1_idx_get(fm1_g_tempo);
    default: return -1;                            /* PRESETS / ALGORITHM: the parts' sound lists (wrapping) */
    }
}

static int32_t fm1_kt[EMU_NE] = {-1, -1, -1, -1, -1, -1, -1, -1};
static uint32_t fm1_epoch = 1, fm1_ksig;
static int fm1_ksig_valid;
static uint32_t fm1_fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t fm1_fnv_s(uint32_t h, const char *s)
{
    for (; s && *s; s++)
        h = fm1_fnv(h, (uint8_t)*s);
    return h;
}
static uint32_t fm1core_cr_param_epoch_fn(void)
{
    int32_t t[EMU_NE], ek0 = fm1_idx_src(FM1K_EDK, 1, 0), opt = fm1_idx_get(fm1_g_option);
    uint32_t r, k, b, h = 2166136261u;
    for (r = 0; r < EMU_NE; r++) {
        t[r] = fm1_knob_compute(r);
        h = fm1_fnv(h, (uint32_t)t[r]);
        if (t[r] >= ek0 && t[r] < ek0 + 4) {       /* a rewritten cell: what it is matters, not only which */
            const param_desc_t *d = &fm1_ekd_new[t[r] - ek0];
            const fm1_cell_t *c = &fm1_ek_new[t[r] - ek0];
            h = fm1_fnv(h, (uint32_t)c->part | (uint32_t)c->r.k << 8 | (uint32_t)c->r.a << 16 | (uint32_t)c->r.b << 24);
            h = fm1_fnv(fm1_fnv(fm1_fnv(h, (uint32_t)(uint16_t)d->min), (uint32_t)(uint16_t)d->max), d->fmt);
            h = fm1_fnv(fm1_fnv_s(h, fm1_eknm_new[t[r] - ek0]), (uint32_t)(uintptr_t)d->names);
        } else if (t[r] == opt) {
            h = fm1_fnv(h, fm1_opt_new);
        }
    }
    if (fm1_ksig_valid && h == fm1_ksig)
        return fm1_epoch;
    b = (fm1_ekcur & 1u) ^ 1u;                     /* the other buffer: the current one, then what changed */
    for (k = 0; k < 4u; k++) {
        fm1_ekd[b][k] = fm1_ekd[b ^ 1u][k];
        cu_cpy(fm1_eknm[b][k], fm1_eknm[b ^ 1u][k], sizeof fm1_eknm[b][k]);
    }
    fm1_optb[b] = fm1_optb[b ^ 1u];
    for (r = 0; r < EMU_NE; r++) {
        if (t[r] >= ek0 && t[r] < ek0 + 4) {
            k = (uint32_t)(t[r] - ek0);
            fm1_ek[k] = fm1_ek_new[k];
            fm1_ekd[b][k] = fm1_ekd_new[k];
            cu_cpy(fm1_eknm[b][k], fm1_eknm_new[k], sizeof fm1_eknm[b][k]);
        } else if (opt >= 0 && t[r] == opt) {
            fm1_optb[b] = fm1_opt_new;
        }
    }
    for (k = 0; k < 4u; k++) {                     /* the entries' own fields: the buffer's */
        fm1param_t *p = &FM1_PARAMS[ek0 + (int32_t)k];
        const param_desc_t *d = &fm1_ekd[b][k];
        p->name = fm1_eknm[b][k];
        p->min = d->min;
        p->max = d->max > d->min ? d->max : d->min + 1;
        p->def = fm1_clamp(d->def, p->min, p->max);
        p->names = d->fmt == F_ENUM ? d->names : 0;
        p->flags = (p->flags & ~(uint32_t)FM1P_ENUM) | (d->fmt == F_ENUM ? FM1P_ENUM : 0u);
    }
    if (opt >= 0) {
        fm1param_t *p = &FM1_PARAMS[opt];
        uint32_t o = fm1_optb[b];
        p->name = FM1_OPTNM[o];
        p->min = o == O_VEL ? 1 : 0;
        p->max = O_MAX[o] > p->min ? O_MAX[o] : p->min + 1;
        p->def = p->min;
    }
    fm1_ekcur = b;
    for (r = 0; r < EMU_NE; r++)
        fm1_kt[r] = t[r];
    fm1_ksig = h;
    fm1_ksig_valid = 1;
    fm1_epoch++;
    return fm1_epoch;
}

/* the ranges and defaults from the firmware's tables (once, when the descriptor is first asked for) */
static int fm1_params_ready;
static void fm1_params_init(void)
{
    cr_settings_t d;                               /* the settings record's defaults (a fresh unit's) */
    uint32_t i;
    if (fm1_params_ready)
        return;
    cr_settings_defaults(&d);
    for (i = 0; i < FM1_NPARAMS; i++) {
        fm1param_t *p = &FM1_PARAMS[i];
        int32_t a = FM1_SRC[i].a, b = FM1_SRC[i].b;
        if (p->names)
            p->flags |= FM1P_ENUM;
        switch (FM1_SRC[i].kind) {
        case FM1K_PERF:
            p->min = CU_PAR[b].min;
            p->max = CU_PAR[b].max;
            p->def = CR_PAR_DEFAULT[a][b];
            break;
        case FM1K_AMT:
        case FM1K_BSEND:
        case FM1K_OPT:
        case FM1K_EDK:
            break;
        case FM1K_GP:
            p->min = GP[a].min;
            p->max = GP[a].max;
            p->def = GP[a].def;
            break;
        case FM1K_XFP:
            if (a >= FC_G) {
                p->min = GP[a - FC_G].min;
                p->max = GP[a - FC_G].max;
                p->def = GP[a - FC_G].def;
            } else {
                p->min = XFP[a].min;
                p->max = XFP[a].max;
                p->def = XFP[a].def;
            }
            break;
        case FM1K_TP:
        case FM1K_ED:
            p->min = TP[a].min;
            p->max = TP[a].max;
            p->def = a == P_LEVEL ? TP[P_LEVEL].def - 12 : TP[a].def;   /* (cr_ui_init: the parts' level -6 dB) */
            break;
        default:                                   /* the record's own default where it holds the value */
#define FM1_DEF(fn, val) if (p->get == fn) p->def = (int32_t)(val);
            FM1_DEF(fm1_g_tempo, d.bpm) FM1_DEF(fm1_g_transpose, d.transpose) FM1_DEF(fm1_g_perform, d.perform_on)
            FM1_DEF(fm1_g_perf_mode, d.perf_sel) FM1_DEF(fm1_g_latch, d.sticky & 1u) FM1_DEF(fm1_g_key_mode, d.key_on & 1u)
            FM1_DEF(fm1_g_tonic, d.tonic) FM1_DEF(fm1_g_scale, d.scale & 0x7Fu) FM1_DEF(fm1_g_single, d.single)
            FM1_DEF(fm1_g_split, d.split_pc) FM1_DEF(fm1_g_style, d.playstyle) FM1_DEF(fm1_g_extadd, d.extadd)
            FM1_DEF(fm1_g_secret, d.secret) FM1_DEF(fm1_g_vel, d.vel) FM1_DEF(fm1_g_bass_mode, d.bass_mode)
            FM1_DEF(fm1_g_bass_reg, d.bass_voicing) FM1_DEF(fm1_g_metro, d.metro_on) FM1_DEF(fm1_g_click, d.metro_vol)
            FM1_DEF(fm1_g_sig, d.metro_sig) FM1_DEF(fm1_g_loop_len, d.loop_sync) FM1_DEF(fm1_g_quant, d.loop_quant)
            FM1_DEF(fm1_g_count_in, d.loop_count_in) FM1_DEF(fm1_g_loop_level, d.loop_level) FM1_DEF(fm1_g_fx, d.fx_on)
#undef FM1_DEF
            break;
        }
    }
    {   /* the rewritten entries before the first epoch: Edit Knob k on part 0's MIX 2 cell k, Option on Play Style */
        static const uint8_t MIX2[4] = {P_TRANS, P_DETUNE, P_PRIO, P_GLMODE};
        uint32_t k, b;
        for (k = 0; k < 4u; k++) {
            fm1_ek[k].part = 0;
            fm1_ek[k].r = ce_r(CE_R_TRK, MIX2[k], 0);
            for (b = 0; b < 2u; b++) {
                fm1_ekd[b][k] = TP[MIX2[k]];
                fm1_txt(fm1_eknm[b][k], "Edit Knob ", sizeof fm1_eknm[b][k]);
                fm1_eknm[b][k][10] = (char)('1' + k);
                fm1_eknm[b][k][11] = 0;
            }
            i = (uint32_t)fm1_idx_src(FM1K_EDK, (int32_t)k + 1, 0);
            FM1_PARAMS[i].name = fm1_eknm[0][k];
            FM1_PARAMS[i].min = TP[MIX2[k]].min;
            FM1_PARAMS[i].max = TP[MIX2[k]].max;
            FM1_PARAMS[i].def = TP[MIX2[k]].def;
            FM1_PARAMS[i].names = TP[MIX2[k]].fmt == F_ENUM ? TP[MIX2[k]].names : 0;
            if (FM1_PARAMS[i].names)
                FM1_PARAMS[i].flags |= FM1P_ENUM;
        }
        fm1_optb[0] = fm1_optb[1] = O_STYLE;
        i = (uint32_t)fm1_idx_get(fm1_g_option);
        FM1_PARAMS[i].max = O_MAX[O_STYLE];
        FM1_PARAMS[i].def = d.playstyle;
    }
    fm1_params_ready = 1;
}

/* for the descriptor (crf_desc.h) */
const fm1param_t *fm1core_cr_params(uint32_t *n)
{
    fm1_params_init();
    *n = FM1_NPARAMS;
    return FM1_PARAMS;
}
uint32_t fm1core_cr_param_epoch(void) { return fm1core_cr_param_epoch_fn(); }
int32_t fm1core_cr_knob_target(int role) { return role >= 0 && role < EMU_NE ? fm1_kt[role] : -1; }
