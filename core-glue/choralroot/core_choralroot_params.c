/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot core's Tier 2 parameter map (FM1-VST-PLAN.md 4.5; core-api/fm1core.h fm1param_t).
 *
 * NOT A TRANSLATION UNIT OF ITS OWN: core_choralroot.c includes this file after the firmware unit (emu_fw.c), so
 * the firmware's statics (cr_ui.c's mirror `cs`, its knob code, cr_out.c's engine `cr` and event queue, params.c's
 * tables and formatter) are in scope here. Nothing in the submodule is modified.
 *
 * HOW A HOST WRITE REACHES THE FIRMWARE (each entry's `path` says which, the param_test prints them)
 *   - The panel's own code wherever one exists: cu_layer_knob (a layer's KNOB 1..4: the value, the posted engine
 *     event, the knob row's hot cell, the CR_TRACE line), cu_layer_pick (a picker), opt_set (an Options entry),
 *     cu_set_tempo + the SELECT meter (the view's tempo knob), the view's KNOB 1 body (voicing), cu_tap's bodies
 *     (the toggles), cu_midi_poll's CC bodies (levels and sends: the path a MIDI controller takes).
 *   - A relative knob reaches an absolute target in ONE detent: the value is first placed one step short of the
 *     target (fm1_pre: on whichever side keeps it in range; nothing is posted or drawn for that placement), then the
 *     knob code turns it by +-1 and lands exactly on the target, posting, tracing and lighting the cell as a panel
 *     detent does. Knob code that is relative by nature (transpose, tonic, the registers) gets the whole delta.
 *   - A perform parameter of a mode that is not the current one (or one no knob carries: Arp Range, Retrig, Rotate,
 *     the Hold of most modes) goes through the same functions with the PERF selection switched to that mode for
 *     the call (so the trace names the right mode) and put back afterwards, with the hot cell restored: the row on
 *     screen is the current mode's and must not light up.
 *   - The settings record (what SAVE and the flash persist) needs no marking: cr_settings_poll captures the UI's
 *     mirror every frame and saves what changed after its quiet time (fm1core flash_sync does it now). The entries
 *     with FM1P_PERSIST are the ones the record holds; the others (voicing, levels, sends, the FX buses) live in
 *     the parts and the song, as on the device.
 *
 * READING: get() reads the UI's mirror (cs), the part / song parameters, or the engine; for the two registers the
 * engine owns (voicing, bass register) it adds the events still queued for the audio ISR (cr_evq), so a value just
 * written reads back at once.
 *
 * THREADS: get / set / target / param_epoch run on the clock's thread between device milliseconds (the plugin's
 * audio thread); text() only formats its argument from constant tables and may run on any thread.
 *
 * FM1_EXPOSE_EDITOR (default 0): the sound editor's ENV, LFO, MOD and MIX pages for the chord and the bass part are
 * appended (30 more: MIX's Level is the live page's / the BASS layer's already). Off by default: the plan keeps the
 * plugin under Live's 128 parameters, and a sound edit is not saved until SAVE on the device. */
#ifndef FM1_EXPOSE_EDITOR
#define FM1_EXPOSE_EDITOR 0
#endif

enum { FM1K_LIT, FM1K_META, FM1K_PERF, FM1K_GP, FM1K_TP };   /* where init takes min / max / def from */

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
    cs.par[m][p] = (int16_t)fm1_pre(v, CU_PAR[p].step, CU_PAR[p].min, CU_PAR[p].max, &s);
    if (k >= 0)
        cu_layer_knob(L_PERF, (uint32_t)k, s);     /* the PERF layer's KNOB k+1 */
    else
        cu_param_turn(m, p, s, 0);                 /* the same function, no knob carries it */
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

/* ==================================================== the meta parameters == */
/* "Perf Knob k": whatever KNOB k of the current perform mode's layer carries (CU_PERF_KNOB). target() names the
 * map entry it stands for; param_epoch() moves when the perform mode changed, so the host relabels */
static uint32_t fm1_epoch, fm1_epoch_mode = 0xFFu;
static uint32_t fm1core_cr_param_epoch_fn(void)
{
    uint32_t m = cu_perf_mode();
    if (m != fm1_epoch_mode) {
        fm1_epoch_mode = m;
        fm1_epoch++;
    }
    return fm1_epoch;
}
static int32_t fm1_meta_target(uint32_t k);
#define FM1_META_FN(k) \
    static int32_t fm1_mg_##k(void) { return cs.par[cu_perf_mode()][CU_PERF_KNOB[cu_perf_mode()][k]]; } \
    static void fm1_ms_##k(int32_t v) { fm1_perf_set(cu_perf_mode(), CU_PERF_KNOB[cu_perf_mode()][k], v); } \
    static int32_t fm1_mt_##k(void) { return fm1_meta_target(k); }
FM1_META_FN(0) FM1_META_FN(1) FM1_META_FN(2) FM1_META_FN(3)

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

static int32_t fm1_g_transpose(void) { return cs.transpose; }
static void fm1_s_transpose(int32_t v)             /* the KEY layer's KNOB 3 */
{
    v = fm1_clamp(v, -24, 24);
    if (v != cs.transpose)
        cu_layer_knob(L_KEY, 2, v - cs.transpose);
}
static void fm1_t_semi(int32_t v, char *d, uint32_t n)
{
    cu_int(d, v, 1, n);
    cu_cat(d, " st", n);
}

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
/* the toggles: cu_tap's bodies (called as such, not through cu_tap: in the sound editor cu_tap hands the section
 * buttons to the editor first) */
static int32_t fm1_g_perform(void) { return cs.perform_on; }
static void fm1_s_perform(int32_t v)
{
    if ((v != 0) == (cs.perform_on != 0))
        return;
    cs.perform_on ^= 1u;                           /* cu_tap BT_PERF */
    cr_post(CRE_PERFORM, 0, 0, cs.perform_on);
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
static void fm1_s_latch(int32_t v)
{
    if ((v != 0) == (cs.sticky != 0))
        return;
    cs.sticky ^= 1u;                               /* cu_tap BT_LATCH */
    cr_post(CRE_STICKY, 0, 0, cs.sticky);
}
static int32_t fm1_g_key_mode(void) { return cs.key_on; }
static void fm1_s_key_mode(int32_t v)
{
    if ((v != 0) == (cs.key_on != 0))
        return;
    cs.key_on ^= 1u;                               /* cu_tap BT_KEY */
    cu_post_key();
}
static int32_t fm1_g_tonic(void) { return cs.tonic; }
static void fm1_s_tonic(int32_t v)                 /* the KEY layer's KNOB 1 (turns Key Mode on, as the knob does) */
{
    v = fm1_clamp(v, 0, 11);
    if (v != cs.tonic)
        cu_layer_knob(L_KEY, 0, v - cs.tonic);
}
static void fm1_t_note(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_NOTE[fm1_clamp(v, 0, 11)], n); }
static int32_t fm1_g_scale(void) { return cs.scale; }
static void fm1_s_scale(int32_t v)                 /* the KEY layer's KNOB 2 */
{
    if ((v != 0) != (cs.scale != 0))
        cu_layer_knob(L_KEY, 1, v ? 1 : -1);
}
static const char *const FM1_SCALE[2] = {"Major", "Minor"};   /* cu_key_cells */
static void fm1_t_scale(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_SCALE[v != 0], n); }
static int32_t fm1_g_single(void) { return cs.single; }
static void fm1_s_single(int32_t v)                /* the KEY layer's KNOB 4 */
{
    if ((v != 0) != (cs.single != 0))
        cu_layer_knob(L_KEY, 3, v ? 1 : -1);
}
static const char *const FM1_SINGLE[2] = {"Full Octave", "Split"};   /* opt_text O_SINGLE */
static void fm1_t_single(int32_t v, char *d, uint32_t n) { fm1_txt(d, FM1_SINGLE[v != 0], n); }

/* Options entries: opt_set, the Options page's own setter (its knob calls it with opt_get + steps) */
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
        cu_layer_knob(L_BASS, 1, d);
}
static void fm1_t_oct(int32_t v, char *d, uint32_t n)
{
    cu_int(d, v, 1, n);
    cu_cat(d, " oct", n);
}
static int32_t fm1_g_bass_level(void) { return trk[CR_PART_BASS].p[P_LEVEL]; }
static void fm1_s_bass_level(int32_t v)            /* the BASS layer's KNOB 4 (steps of 2) */
{
    int32_t s;
    v = fm1_clamp(v, 0, 127);
    if (v == trk[CR_PART_BASS].p[P_LEVEL])
        return;
    trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)fm1_pre(v, 2, 0, 127, &s);
    cu_layer_knob(L_BASS, 3, s);
}

static int32_t fm1_g_metro(void) { return cs.metro; }
static void fm1_s_metro(int32_t v)
{
    if ((v != 0) == (cs.metro != 0))
        return;
    cs.metro ^= 1u;                                /* cu_tap BT_METRO */
    cr_post(CRE_LOOP, LP_METRO, 0, cs.metro);
    cu_message(cs.metro ? "metronome on" : "metronome off", CR_COL_WHITE);
}
static int32_t fm1_g_click(void) { return cs.metro_vol; }
static void fm1_s_click(int32_t v)                 /* the METRO layer's KNOB 1 (steps of 5) */
{
    int32_t s;
    v = fm1_clamp(v, 0, 100);
    if (v == cs.metro_vol)
        return;
    cs.metro_vol = (uint8_t)fm1_pre(v, 5, 0, 100, &s);
    cu_layer_knob(L_METRO, 0, s);
}
static int32_t fm1_g_sig(void) { return cs.metro_sig; }
static void fm1_s_sig(int32_t v)                   /* the METRO picker */
{
    v = fm1_clamp(v, 0, CRL_NSIG - 1);
    if (v != cs.metro_sig)
        cu_layer_pick(L_METRO, v);
}
static void fm1_t_sig(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_SIG[fm1_clamp(v, 0, CRL_NSIG - 1)], n); }

static int32_t fm1_g_loop_len(void) { return cs.loop_len; }
static void fm1_s_loop_len(int32_t v)              /* the LOOP picker; while a loop plays it picks the action, so the
                                                    * length stays (as on the device: the host knob springs back) */
{
    v = fm1_clamp(v, 0, (int32_t)CRL_NSYNC - 1);
    if (v != cs.loop_len && !cu_playing())
        cu_layer_pick(L_LOOP, v);
}
static void fm1_t_loop_len(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_LOOPLEN[fm1_clamp(v, 0, CRL_NSYNC - 1)], n); }
static int32_t fm1_g_quant(void) { return cs.loop_quant; }
static void fm1_s_quant(int32_t v)                 /* the LOOP layer's KNOB 2 (one entry per detent) */
{
    int32_t s;
    v = fm1_clamp(v, 0, (int32_t)CRL_NQUANT - 1);
    if (v == cs.loop_quant)
        return;
    cs.loop_quant = (uint8_t)fm1_pre(v, 1, 0, (int32_t)CRL_NQUANT - 1, &s);
    cu_layer_knob(L_LOOP, 1, s);
}
static void fm1_t_quant(int32_t v, char *d, uint32_t n) { fm1_txt(d, CU_QUANT[fm1_clamp(v, 0, CRL_NQUANT - 1)], n); }
static int32_t fm1_g_count_in(void) { return cs.loop_count_in; }
static void fm1_s_count_in(int32_t v)              /* the LOOP layer's KNOB 3 */
{
    if ((v != 0) != (cs.loop_count_in != 0))
        cu_layer_knob(L_LOOP, 2, v ? 1 : -1);
}
static int32_t fm1_g_loop_level(void) { return cs.loop_level; }
static void fm1_s_loop_level(int32_t v)            /* the LOOP layer's KNOB 4 (steps of 5) */
{
    int32_t s;
    v = fm1_clamp(v, 0, 100);
    if (v == cs.loop_level)
        return;
    cs.loop_level = (uint8_t)fm1_pre(v, 5, 0, 100, &s);
    cu_layer_knob(L_LOOP, 3, s);
}

/* ======================================================================= FX == */
static int32_t fm1_g_fx(void) { return cs.fx_on; }
static void fm1_s_fx(int32_t v)
{
    if ((v != 0) == (cs.fx_on != 0))
        return;
    cs.fx_on ^= 1u;                                /* cu_tap BT_FX */
    cu_fx_apply();
}
/* the FX layer with effect e picked for the call (its row's hot cell put back when e is not the one on screen) */
static void fm1_fx_knob(uint32_t e, uint32_t knob, int32_t s)
{
    uint8_t sel = cs.fx_sel;
    __typeof__(cu_hot) hot = cu_hot;
    cs.fx_sel = (uint8_t)e;
    cu_layer_knob(L_FX, knob, s);
    if (sel != e) {
        cs.fx_sel = sel;
        cu_hot = hot;
    }
}
/* the chord part's sends = the FX amounts: the FX layer's KNOB 4 (steps of 4; turns FX on, as the knob does) */
static void fm1_amt_set(uint32_t e, int32_t v)
{
    int32_t s;
    v = fm1_clamp(v, 0, 127);
    if (v == cs.fx_amt[e])
        return;
    cs.fx_amt[e] = (uint8_t)fm1_pre(v, 4, 0, 127, &s);
    fm1_fx_knob(e, 3, s);
}
#define FM1_AMT_FN(e) \
    static int32_t fm1_ga_##e(void) { return cs.fx_amt[e]; } \
    static void fm1_sa_##e(int32_t v) { fm1_amt_set(e, v); }
FM1_AMT_FN(0) FM1_AMT_FN(1) FM1_AMT_FN(2) FM1_AMT_FN(3)   /* CU_FX order: Reverb Chorus Delay Drive */
static void fm1_t_amt(int32_t v, char *d, uint32_t n) { cu_2d(d, (uint32_t)fm1_clamp(v, 0, 127) * 99u / 127u, n); }

/* the bass part's sends: cu_midi_poll's CC 91 / 93 / 94 body (Drive has no CC: the same statements) */
static void fm1_bsend_set(uint32_t id, int32_t v)
{
    uint32_t i;
    v = fm1_clamp(v, 0, 127);
    if (v == trk[CR_PART_BASS].p[id])
        return;
    fm1_irq_off();
    trk[CR_PART_BASS].p[id] = (int16_t)v;
    fm1_irq_on();
    for (i = 0; i < CU_NFX; i++)
        if (CU_FX[i].send == id)
            break;
    {
        char lb[24];
        cu_cpy(lb, i < CU_NFX ? CU_FX[i].name : "send", sizeof lb);
        lb[0] = (char)(lb[0] | 0x20);
        cu_popup_num(v * 99 / 127, 0, "", lb, CR_COL_ORANGE, 0, 99, 12);
    }
    cu_trace("send: part 1 %s %u (host)\n", i < CU_NFX ? CU_FX[i].name : "?", (unsigned)v);
}
#define FM1_BSEND_FN(id) \
    static int32_t fm1_gb_##id(void) { return trk[CR_PART_BASS].p[id]; } \
    static void fm1_sb_##id(int32_t v) { fm1_bsend_set(id, v); } \
    static void fm1_tb_##id(int32_t v, char *d, uint32_t n) { cp_value(&TP[id], v, d, n); }
FM1_BSEND_FN(P_DIST) FM1_BSEND_FN(P_CHOR) FM1_BSEND_FN(P_DLY) FM1_BSEND_FN(P_REV)

/* the shared buses: the FX layer's KNOB 1..3 (steps of 2 over a range wider than 20, else 1), cu_fx_val's text */
static void fm1_bus_set(uint32_t e, uint32_t knob, uint32_t g, int32_t v)
{
    int32_t s, step = GP[g].max - GP[g].min > 20 ? 2 : 1;
    v = fm1_clamp(v, GP[g].min, GP[g].max);
    if (v == song.g[g])
        return;
    song.g[g] = (int16_t)fm1_pre(v, step, GP[g].min, GP[g].max, &s);
    fm1_fx_knob(e, knob, s);
}
static void fm1_bus_text(uint32_t g, int32_t v, char *out, uint32_t n)   /* cu_fx_val with v for song.g[g] */
{
    const param_desc_t *d = &GP[g];
    char val[12];
    const char *unit;
    uint32_t i;
    if (d->fmt != F_ENUM && d->fmt != F_LFOHZ) {
        cu_int(out, v, 0, n);
        return;
    }
    param_format(d, fm1_clamp(v, d->min, d->max), val, &unit);
    if (val[0] == '.') {
        cu_cpy(out, "0", n);
        cu_cat(out, val, n);
    } else {
        cu_cpy(out, val, n);
    }
    if (d->fmt == F_ENUM && out[0] >= 'A' && out[0] <= 'Z')
        for (i = 1; out[i]; i++)
            if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
    if (d->fmt == F_LFOHZ) {
        cu_cat(out, " ", n);
        cu_cat(out, unit, n);
    }
}
#define FM1_BUS_FN(e, k, gid) \
    static int32_t fm1_gg_##gid(void) { return song.g[gid]; } \
    static void fm1_sg_##gid(int32_t v) { fm1_bus_set(e, k, gid, v); } \
    static void fm1_tg_##gid(int32_t v, char *d, uint32_t n) { fm1_bus_text(gid, v, d, n); }
FM1_BUS_FN(0, 0, G_RSIZE) FM1_BUS_FN(0, 1, G_RDAMP) FM1_BUS_FN(0, 2, G_RTYPE)
FM1_BUS_FN(1, 0, G_CRATE) FM1_BUS_FN(1, 1, G_CDEPTH)
FM1_BUS_FN(2, 0, G_DTIME) FM1_BUS_FN(2, 1, G_DFDBK) FM1_BUS_FN(2, 2, G_DCOLOR)

/* ============================================================ sound editor == */
#if FM1_EXPOSE_EDITOR
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
#endif

/* ================================================================ the map == */
/* X(name, kind, a, b, min, max, def, names, unit, get, set, text, flags, path); kind FM1K_PERF: (mode, param), its
 * range from CU_PAR and default from cr_engine.c's CR_PAR_DEFAULT; FM1K_GP / FM1K_TP: (id) from params.c's GP / TP;
 * FM1K_META: (knob); FM1K_LIT: as written */
#define P_ (FM1P_PERSIST)
#define E_ (FM1P_ENUM)
#define FM1_PERF(X, nm, m, p, txt, nms, un) \
    X(nm, FM1K_PERF, m, p, 0, 0, 0, nms, un, fm1_g_##m##_##p, fm1_s_##m##_##p, txt, P_, \
      "perf: cu_layer_knob(L_PERF) / cu_param_turn, mode switched for the call")
#define FM1_META(X, nm, k) \
    X(nm, FM1K_META, k, 0, 0, 0, 0, 0, "", fm1_mg_##k, fm1_ms_##k, fm1_t_int, FM1P_META | P_, \
      "meta: the current mode's PERF knob (its entry's path)")
#define FM1_BUS(X, nm, gid, nms) \
    X(nm, FM1K_GP, gid, 0, 0, 0, 0, nms, "", fm1_gg_##gid, fm1_sg_##gid, fm1_tg_##gid, 0, \
      "fx: cu_layer_knob(L_FX, KNOB 1..3), effect picked for the call")
#define FM1_BSEND(X, nm, id) \
    X(nm, FM1K_TP, id, 0, 0, 0, 0, 0, "", fm1_gb_##id, fm1_sb_##id, fm1_tb_##id, FM1P_SOUND, \
      "cc: cu_midi_poll's send body")
#define FM1_AMT(X, nm, e) \
    X(nm, FM1K_LIT, e, 0, 0, 127, 0, 0, "", fm1_ga_##e, fm1_sa_##e, fm1_t_amt, FM1P_SOUND, \
      "fx: cu_layer_knob(L_FX, KNOB 4), effect picked for the call")
#if FM1_EXPOSE_EDITOR
#define FM1_ED(X, nm, part, id, nms) \
    X(nm, FM1K_TP, id, 0, 0, 0, 0, nms, "", fm1_ge_##part##_##id, fm1_se_##part##_##id, fm1_te_##part##_##id, \
      FM1P_EDITOR | FM1P_SOUND, "direct: ce_knob's write, IRQ off, sound marked edited")
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
#else
#define FM1_EDITOR_ENTRIES(X)
#endif

#define FM1_LIST(X) \
    /* ---- page 1: the live page (a Roto-Control's first eight knobs) */ \
    FM1_META(X, "Perf Knob 1", 0) FM1_META(X, "Perf Knob 2", 1) FM1_META(X, "Perf Knob 3", 2) \
    FM1_META(X, "Perf Knob 4", 3) \
    X("Voicing", FM1K_LIT, 0, 0, CR_VOICE_MIN, CR_VOICE_MAX, 0, 0, "", fm1_g_voicing, fm1_s_voicing, fm1_t_voicing, 0, \
      "view KNOB 1: CRE_VOICING + the voicing meter") \
    X("Tempo", FM1K_LIT, 0, 0, 20, 300, 120, 0, "BPM", fm1_g_tempo, fm1_s_tempo, fm1_t_bpm, P_, \
      "view SELECT: cu_set_tempo + the bpm meter") \
    X("Transpose", FM1K_LIT, 0, 0, -24, 24, 0, 0, "st", fm1_g_transpose, fm1_s_transpose, fm1_t_semi, P_, \
      "key: cu_layer_knob(L_KEY, KNOB 3)") \
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
    /* ---- chord, global */ \
    X("Perform", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_perform, fm1_s_perform, fm1_t_onoff, P_ | E_, \
      "tap: cu_tap BT_PERF body") \
    X("Perform Mode", FM1K_LIT, 0, 0, 0, 6, 0, FM1_PERF_NAMES, "", fm1_g_perf_mode, fm1_s_perf_mode, fm1_t_perf_mode, \
      P_ | E_, "pick: cu_layer_pick(L_PERF)") \
    X("Latch", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_latch, fm1_s_latch, fm1_t_onoff, P_ | E_, \
      "tap: cu_tap BT_LATCH body") \
    X("Key Mode", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_key_mode, fm1_s_key_mode, fm1_t_onoff, P_ | E_, \
      "tap: cu_tap BT_KEY body") \
    X("Key Tonic", FM1K_LIT, 0, 0, 0, 11, 0, CU_NOTE, "", fm1_g_tonic, fm1_s_tonic, fm1_t_note, P_ | E_, \
      "key: cu_layer_knob(L_KEY, KNOB 1)") \
    X("Key Scale", FM1K_LIT, 0, 0, 0, 1, 0, FM1_SCALE, "", fm1_g_scale, fm1_s_scale, fm1_t_scale, P_ | E_, \
      "key: cu_layer_knob(L_KEY, KNOB 2)") \
    X("Single Notes", FM1K_LIT, 0, 0, 0, 1, 0, FM1_SINGLE, "", fm1_g_single, fm1_s_single, fm1_t_single, P_ | E_, \
      "key: cu_layer_knob(L_KEY, KNOB 4)") \
    X("Split Point", FM1K_LIT, 0, 0, 0, 11, 0, CU_NOTE, "", fm1_g_split, fm1_s_split, fm1_t_note, P_ | E_, \
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
      P_ | E_, "pick: cu_layer_pick(L_BASS)") \
    X("Bass Register", FM1K_LIT, 0, 0, CR_BV_MIN, CR_BV_MAX, 0, 0, "oct", fm1_g_bass_reg, fm1_s_bass_reg, fm1_t_oct, P_, \
      "bass: cu_layer_knob(L_BASS, KNOB 2)") \
    X("Bass Level", FM1K_TP, P_LEVEL, 0, 0, 0, 0, 0, "", fm1_g_bass_level, fm1_s_bass_level, fm1_t_tp_level, 0, \
      "bass: cu_layer_knob(L_BASS, KNOB 4)") \
    X("Metronome", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_metro, fm1_s_metro, fm1_t_onoff, P_ | E_, \
      "tap: cu_tap BT_METRO body") \
    X("Click Level", FM1K_LIT, 0, 0, 0, 100, 0, 0, "%", fm1_g_click, fm1_s_click, fm1_t_pct, P_, \
      "metro: cu_layer_knob(L_METRO, KNOB 1)") \
    X("Time Signature", FM1K_LIT, 0, 0, 0, CRL_NSIG - 1, 0, CU_SIG, "", fm1_g_sig, fm1_s_sig, fm1_t_sig, P_ | E_, \
      "pick: cu_layer_pick(L_METRO)") \
    X("Loop Length", FM1K_LIT, 0, 0, 0, CRL_NSYNC - 1, 0, CU_LOOPLEN, "", fm1_g_loop_len, fm1_s_loop_len, \
      fm1_t_loop_len, P_ | E_, "pick: cu_layer_pick(L_LOOP), not while a loop plays") \
    X("Loop Quantize", FM1K_LIT, 0, 0, 0, CRL_NQUANT - 1, 0, CU_QUANT, "", fm1_g_quant, fm1_s_quant, fm1_t_quant, \
      P_ | E_, "loop: cu_layer_knob(L_LOOP, KNOB 2)") \
    X("Count-In", FM1K_LIT, 0, 0, 0, 1, 0, FM1_ONOFF, "", fm1_g_count_in, fm1_s_count_in, fm1_t_onoff, P_ | E_, \
      "loop: cu_layer_knob(L_LOOP, KNOB 3)") \
    X("Loop Level", FM1K_LIT, 0, 0, 0, 100, 0, 0, "%", fm1_g_loop_level, fm1_s_loop_level, fm1_t_pct, P_, \
      "loop: cu_layer_knob(L_LOOP, KNOB 4)") \
    /* ---- FX: on / off, the chord part's sends (the FX amounts), the bass part's, the shared buses */ \
    X("FX", FM1K_LIT, 0, 0, 0, 1, 1, FM1_ONOFF, "", fm1_g_fx, fm1_s_fx, fm1_t_onoff, P_ | E_, "tap: cu_tap BT_FX body") \
    FM1_AMT(X, "Chord Drive", 3) FM1_AMT(X, "Chord Chorus", 1) FM1_AMT(X, "Chord Delay", 2) \
    FM1_AMT(X, "Chord Reverb", 0) \
    FM1_BSEND(X, "Bass Drive", P_DIST) FM1_BSEND(X, "Bass Chorus", P_CHOR) FM1_BSEND(X, "Bass Delay", P_DLY) \
    FM1_BSEND(X, "Bass Reverb", P_REV) \
    FM1_BUS(X, "Reverb Size", G_RSIZE, 0) FM1_BUS(X, "Reverb Damp", G_RDAMP, 0) \
    FM1_BUS(X, "Reverb Type", G_RTYPE, N_RTYPE) FM1_BUS(X, "Chorus Rate", G_CRATE, 0) \
    FM1_BUS(X, "Chorus Depth", G_CDEPTH, 0) FM1_BUS(X, "Delay Time", G_DTIME, N_DIV) \
    FM1_BUS(X, "Delay Feedback", G_DFDBK, 0) FM1_BUS(X, "Delay Colour", G_DCOLOR, 0) \
    /* ---- the sound editor (FM1_EXPOSE_EDITOR) */ \
    FM1_EDITOR_ENTRIES(X)

#define FM1_AS_PARAM(nm, kind, a, b, mn, mx, df, nms, un, g, s, t, fl, pa) {nm, mn, mx, df, nms, un, g, s, t, 0, fl, pa},
#define FM1_AS_SRC(nm, kind, a, b, ...) {kind, a, b},
static fm1param_t FM1_PARAMS[] = {FM1_LIST(FM1_AS_PARAM)};
static const struct { uint8_t kind; int16_t a, b; } FM1_SRC[] = {FM1_LIST(FM1_AS_SRC)};
#define FM1_NPARAMS ((uint32_t)(sizeof FM1_PARAMS / sizeof FM1_PARAMS[0]))

static int32_t fm1_meta_target(uint32_t k)        /* the map entry of the current mode's KNOB k+1, -1 none */
{
    uint32_t m = cu_perf_mode(), i;
    int32_t p = CU_PERF_KNOB[m][k & 3u];
    for (i = 0; i < FM1_NPARAMS; i++)
        if (FM1_SRC[i].kind == FM1K_PERF && FM1_SRC[i].a == (int16_t)m && FM1_SRC[i].b == p)
            return (int32_t)i;
    return -1;
}

/* the ranges and defaults from the firmware's tables (once, when the descriptor is first asked for) */
static int fm1_params_ready;
static void fm1_params_init(void)
{
    static int32_t (*const MT[4])(void) = {fm1_mt_0, fm1_mt_1, fm1_mt_2, fm1_mt_3};
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
        case FM1K_META:                            /* (the host shows its target's; these are Strum's) */
            p->min = CU_PAR[CU_PERF_KNOB[CR_PM_STRUM][a]].min;
            p->max = CU_PAR[CU_PERF_KNOB[CR_PM_STRUM][a]].max;
            p->def = CR_PAR_DEFAULT[CR_PM_STRUM][CU_PERF_KNOB[CR_PM_STRUM][a]];
            p->target = MT[a & 3];
            break;
        case FM1K_GP:
            p->min = GP[a].min;
            p->max = GP[a].max;
            p->def = GP[a].def;
            break;
        case FM1K_TP:
            p->min = TP[a].min;
            p->max = TP[a].max;
            p->def = a == P_LEVEL ? TP[P_LEVEL].def - 12 : TP[a].def;   /* (cr_ui_init: the parts' level -6 dB) */
            break;
        default:                                   /* the record's own default where it holds the value */
#define FM1_DEF(fn, val) if (p->get == fn) p->def = (int32_t)(val);
            FM1_DEF(fm1_g_tempo, d.bpm) FM1_DEF(fm1_g_transpose, d.transpose) FM1_DEF(fm1_g_perform, d.perform_on)
            FM1_DEF(fm1_g_perf_mode, d.perf_sel) FM1_DEF(fm1_g_latch, d.sticky) FM1_DEF(fm1_g_key_mode, d.key_on)
            FM1_DEF(fm1_g_tonic, d.tonic) FM1_DEF(fm1_g_scale, d.scale) FM1_DEF(fm1_g_single, d.single)
            FM1_DEF(fm1_g_split, d.split_pc) FM1_DEF(fm1_g_style, d.playstyle) FM1_DEF(fm1_g_extadd, d.extadd)
            FM1_DEF(fm1_g_secret, d.secret) FM1_DEF(fm1_g_vel, d.vel) FM1_DEF(fm1_g_bass_mode, d.bass_mode)
            FM1_DEF(fm1_g_bass_reg, d.bass_voicing) FM1_DEF(fm1_g_metro, d.metro_on) FM1_DEF(fm1_g_click, d.metro_vol)
            FM1_DEF(fm1_g_sig, d.metro_sig) FM1_DEF(fm1_g_loop_len, d.loop_sync) FM1_DEF(fm1_g_quant, d.loop_quant)
            FM1_DEF(fm1_g_count_in, d.loop_count_in) FM1_DEF(fm1_g_loop_level, d.loop_level) FM1_DEF(fm1_g_fx, d.fx_on)
#undef FM1_DEF
            break;
        }
    }
    fm1_params_ready = 1;
}

/* for the descriptor (core_choralroot_desc.c) */
const fm1param_t *fm1core_cr_params(uint32_t *n)
{
    fm1_params_init();
    *n = FM1_NPARAMS;
    return FM1_PARAMS;
}
uint32_t fm1core_cr_param_epoch(void) { return fm1core_cr_param_epoch_fn(); }
