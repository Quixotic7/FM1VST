# Live names and values for every RFM1 firmware: survey (2026-11-24)

The owner's goal (2026-11-24): every RFM1 firmware to give a host custom labels and values like ChoralRoot, so a firmware is usable without its web page. Two things: **names** (the 49 live labels, docs/DYNAMIC-LABELS.md) and **values** (readouts, rd1: docs/RFM1.md "Readouts (rd1)").

How it was measured: every .rfm1 of the current catalog release (RFM1Manager/cache/release/current, 122 files, the 121 of RemoteFM1FW catalog.json plus chordism) was described (`rfm1_run --describe --labels`), checked (`rfm1_check`) and run for 2.8 s with two knob turns (`rfm1_run --readouts`): the "today" columns are what the released files do. The wrapper columns come from core-glue/ and, for the class, from reading each firmware's source at its pin (five read-only passes, one per glue family; line counts are estimates, not measured).

Classes: **A** names and values switch on from what the wrapper already has (a Tier 2 map: `WANTS rd1`). **B** needs an adapter in the glue reading the firmware's own tables (pattern: core-glue/dinghy/grimm_rd.h). **C** only a fixed names list is realistic (RemoteFM1Metadata.json labels; drafts in names/). **D** built by its author, shipped unchanged. **done**: already gives both.

"Today" before this branch. On branch rfm1-labels: melodee, felucca (A) and grimm, glitch, arcticscream (B) now give live knob names and values (and the GR!MM three the page buttons' names).

## Totals

| class | firmwares |
|---|---|
| done | 2: choralroot, fizz |
| A | 2: felucca, melodee |
| B | 90: a22, arcticscream, aurora, berlinan, bytemania, caravel, carnacos, chimeravocal, clipper, coralflower, csfm1, cultbox, dabke, drift, drumlab, dub, duell, dungeontracker, dx747, echofield, elasticat, evilgenius, fartmachine, fimba, fmav1, fmel1, fmini, fmkick, fomni, ghoulbox, glitch, golana, golf, grimm, h1, hadal1, hortator, hyp1, jamesos, jiant, karma, kwasub, matryoshka, mightyarp, minitone, mnm1, modelar, modularkoi, mono2k, mope1, mpyre, mutabledreams, mvance, mvsynth2, nachtfahrt, niagaradrift, obervave, obssynth, orbit, orgigs, patchwerk, penumbra, perf1, pigeon, pingos1, quantumgammawave, quartz, ravel, rhythmer, ruido, s1vave, sid1, sidj, skiff, sloop, sloop3, smileyface, speak37, spellbox, stallion, sy1, tekstuur, tessera, trapstationos, vector37, vesper, viscera, x0x, yehnah, zen1 |
| C | 17: chipbox, conductor, etch, guitchordi, hawkchua, loafbreaker, morse, nomansky, purplemonkey, rainbow, remotefm1, ripple, rupture, sid, sidou, tonitor, trax |
| D | 11: amb, amenity, chordism, ctl, dubwise, fmlarynx, frasil, kris_jangle, loom, pond, samsara |

Of class B, built on this branch: grimm, glitch, arcticscream. Today (before the branch) names follow pages in 3 (choralroot, fizz, elasticat), values in 2 (choralroot, fizz); no catalog file ships a fixed names list from a RemoteFM1Metadata.json except ChoralRoot's skin file (it has no labels block); dubwise ships none in its .rfm1.

## Recommended order

1. **Done here, ready to publish after review**: melodee, felucca (A); grimm, glitch, arcticscream (B pattern).
2. **(Batch 2: done; elasticat a trial until its pin has the lines)** **Free or nearly**: elasticat (owner's repo: 2-4 `RFM1_READOUT_BAR` lines in turn_cell / ec_ux_takeover); evilgenius (FIZZ's own label and turn functions are already in its tree: export them as FIZZ's adapter does).
3. **(Batch 2: dungeontracker, ghoulbox, minitone done; pigeon failed the check: its own views)** **Felucca forks on the existing Felucca map** (one map, `#if` shims): dungeontracker, ghoulbox, minitone, pigeon (~20-80 lines each).
4. **(Batch 2: done for six, by what the screen draws, not the model; nomansky moved to C, sloop3 still open)** **The sloop family, one shared adapter** (Felucca's model in every fork, one unit sloop_fw.c): sloop, clipper, karma, mpyre, nomansky, orbit, stallion (~150 + ~15 each); sloop3 (~50, its own view model).
5. **Small table adapters in the grimm_rd.h style (25-60 lines)**: sid1, fmel1, jamesos, aurora, bytemania, obervave + s1vave, fmav1, mvsynth2, mightyarp, sy1, golf + tekstuur, a22 + h1, fmkick, ctl1, echofield, hadal1, vesper, fomni, zen1, hyp1, fimba, fartmachine, spellbox, obssynth, golana, coralflower, modularkoi, drift, mutabledreams, carnacos.
6. **Mid (60-150)**: the plat VIEW_KNOB family (dub, nachtfahrt, niagaradrift, perf1, mope1), quartz, berlinan, kwasub, cultbox, drumlab, orgigs, mono2k, sidj, ravel, x0x, speak37, vector37, viscera, ruido, dx747, csfm1, fmini, matryoshka, hortator + mnm1, chimeravocal, dabke, modelar, patchwerk, pingos1, smileyface.
7. **Large reworks (150-250)**: jiant (then quantumgammawave, rhythmer, yehnah on it), caravel, duell, trapstationos, skiff, tessera, penumbra.
8. **Class C**: fixed lists after the owner approves the drafts (batch 1: names/, NAMES-BATCH-1.md); the rest (conductor, sid, rainbow, purplemonkey, remotefm1, ripple) in a batch 2.
9. **Class D**: ask the authors (rfm1_ext.h is MPL-2.0 for vendoring; a RemoteFM1Metadata.json in their repo).

Two cheap partial routes the passes found, not built: (a) fm1-base and plat apps already speak "NAME VALUE" to a screen reader (announce.c, F0 7D 53 52): a wrapper keeping the reader on could read out a knob for ~a dozen apps at once, but the text is free-form and Quartz turns on a CPU probe with it; (b) several apps fill draw-time captures (vesper web_knobs, mutabledreams reader_cards, sloop3 ui_view) that an adapter reads directly.

## Built so far, what is left, the cost (after batch 3)

**Done: 37 of 122.** Before this branch: choralroot, fizz. Built on rfm1-labels (BUILT.md: files, sha256, tests, what to try on the Move): 35: a22, arcticscream, aurora, bytemania, clipper, coralflower, dungeontracker, elasticat, evilgenius, felucca, fmav1, fmel1, fmkick, fomni, ghoulbox, glitch, golf, grimm, h1, hyp1, jamesos, karma, melodee, mightyarp, minitone, mpyre, mvsynth2, nachtfahrt, orbit, sid1, sloop, sloop3, stallion, tekstuur, zen1 (elasticat as a trial from its own branch).

**Left:** class B 57 (berlinan, caravel, carnacos, chimeravocal, csfm1, cultbox, dabke, drift, drumlab, dub, duell, dx747, echofield, fartmachine, fimba, fmini, golana, hadal1, hortator, jiant, kwasub, matryoshka, mnm1, modelar, modularkoi, mono2k, mope1, mutabledreams, mvance, niagaradrift, obervave, obssynth, orgigs, patchwerk, penumbra, perf1, pigeon, pingos1, quantumgammawave, quartz, ravel, rhythmer, ruido, s1vave, sidj, skiff, smileyface, speak37, spellbox, sy1, tessera, trapstationos, vector37, vesper, viscera, x0x, yehnah); class C 17 (fixed lists: the first ten drafted, waiting for the owner); class D 11 (the authors').

**How it is done now** (batch 3): common/rd_cols.h. After each UI frame the glue copies what the firmware itself keeps of its drawn knob row (a view model, a per-knob name and text function, its screen reader's card function) into four (or seven) label / value pairs; the knobs' live labels are those; a readout is sent when the turned knob's drawn value changes within 8 frames of the turn (the shim's fm1core_rd_turn / retry). A screen the feeder does not know (a list, a dialog, a scene with no knob row) is "not given": the host keeps its own labels and there is no readout, never a guess.

**What batch 3 really cost.** The shared part (rd_cols.h ~110 lines, the shim's retry and turn note ~35, rfm1_run --keys none) about 1.5 h. Then per firmware a 15-30 line feeder, a screen tour (8-9 pages, one contact sheet), a test and the audio check: 20-40 min each when the firmware has the functions; 19 built this way in about 9 h. Two of the eight "drawn strings" candidates failed: VESPER's web mirror disagrees with its LCD (screens/b3/vesper_mismatch.png), Mutable Dreams' cards exist only while a screen reader listens and join label and value in one string. Four from the "name and text" list turned out to need more than a copy (obervave + s1vave and mvance build their knob row inside the draw function; hadal1 draws bars, not text; dub rides knobs with LFOs), and ctl1 is not shipped by us (its .rfm1 is the author's ctl).

**Revised estimate for the 57 left in class B.**

- With name / text functions or a view model (the rd_cols way, ~30 min each): perf1, mope1, niagaradrift, fimba, quartz, berlinan, sy1, echofield, kwasub, cultbox, drumlab, orgigs, mono2k, sidj, ravel, x0x, speak37, vector37, viscera, ruido, carnacos, spellbox, obssynth, golana, drift, modularkoi, fmini, csfm1, chimeravocal, dabke, patchwerk, pingos1, smileyface, hadal1, mvance, fartmachine, matryoshka: about 37, 20-30 h, of which some will turn out to need more (the batch-3 rate: 4 in 16).
- A mirror of a draw function or of the dispatch (2-4 h each): obervave + s1vave, vesper, mutabledreams, dub, dx747, penumbra, and the big Felucca forks (jiant and its three, caravel, duell, trapstationos, skiff, tessera, hortator + mnm1, pigeon): about 20, 40-70 h.
- Elasticat: in its own repo (the trial branch), ready when its pin moves.
- Total for the rest of class B: about 60-100 h.

## Every firmware

| id | version | built by | names today | values today | what the wrapper / firmware has | class | route, rough adapter size |
|---|---|---|---|---|---|---|---|
| a22 | 0.1.0 | us | none | none | core/h1.h h1_control(enc,&lo,&hi,&name,&text) per encoder | B | BUILT (batch 3): h1_control, all seven knobs |
| amb | 1.3.2 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| amenity | 2.1.1 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| arcticscream | 2.1.4 | us | none | none | GR!MM control map (arctic.c PAGES, knob_*; reader.c rd_knob) | B | BUILT here: grimm_rd.h + its knob_text page mask |
| aurora | 0.8.0 | us | none | none | aurora.c PAGES[].p[4], fmt_value (draw_knob) | B | BUILT (batch 3): the knob cells |
| berlinan | 0.5.1 | us | none | none | VIEW_KNOB, KNOB_LABEL, cell(view,i) | B | ~70-90 |
| bytemania | 1.3.2 | us | none | none | knob_param / knob_text per bank | B | BUILT (batch 3): main screen and editor (CC, SAVE, NAME, ENV: none) |
| caravel | 1.1.0 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca params model; own HOME performance, UX layers, action banks | B | Felucca map + rework ~150-250 |
| carnacos | 0.9.6 | us | none | none | ui_sm_knob(sm,page,layer,knob,&UiKnobInfo), ui_sm_knob_text | B | ~60-80 |
| chimeravocal | 0.9.0 | us | none | none | KNOB[NK] (name, min, max, fmt), PAGE[6][4]; extra modes ad hoc | B | ~100 |
| chipbox | 1.5.5 | us | none | none | labels ad hoc in chipview.c chip_cards(); values as bars | C | fixed list (drafted); CHIP view knobs would need B |
| choralroot | 0.44 | us | follows pages | all knobs | Tier 2 map (core_choralroot_params.c) + its own RFM1_READOUT lines (cr_ui.c) | done | shipped (0.44, rd1) |
| chordism | 0.9 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| clipper | 1.3.1 | us | none | none | Felucca params model; draws columns via draw_column (ui.big_l/v/u) | B | BUILT: sloop_rd.h |
| conductor | 1.0 | us | none | none | knob meanings hard-coded per screen (draw_foot_*) | C | fixed list |
| coralflower | 0.7.1 | us | fixed: 7 buttons renamed (descriptor) | none | cf_ui.c ui_knob_info(k) -> name + value for the current screen | B | BUILT (batch 3): ui_knob_info (its # / b glyphs mapped) |
| csfm1 | 0.20.0 | us | fixed: 1 buttons renamed (descriptor) | none | COS_SLIM UI cos.c: cpg_t pages, cos_knob(page,k,&vp) -> param_desc_t | B | ~100-150 |
| ctl | 1.0.3 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| cultbox | 1.2 | us | none | none | fm1_param_t tables, page_param(page,k), page_title | B | ~50 |
| dabke | 0.18 | us | none | none | PAR[], KROW[4][rows][4], knob_value(id, buf, &label) | B | ~100-130 |
| drift | 0.9.6 | us | fixed: 1 buttons renamed (descriptor) | none | drift_ui.c D_PARAMS[page][4], D_LABEL, drift_desc + param_format | B | ~60-90 |
| drumlab | 0.1.3-beta | us | none | none | LAB screen labui.c PG[6], param_text (sequencer screens ad hoc) | B | ~50 (LAB only) |
| dub | 1.4.3 | us | none | none | vknob (strips), fm_knob_off, LFO-ridden knobs | B | ~100-150 (more than its VIEW_KNOB siblings) |
| dubwise | 0.4.2 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| duell | 1.3.6 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca tables, dispatch rewritten (two parts, own scopes) | B | ~150-250 |
| dungeontracker | 1.5 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca 1.6.1 model, no symbol missing | B | BUILT: Felucca map + 3 options (SEQ+SELECT RONDO, DICE own) |
| dx747 | 0.1 | us | none | none | readouts: ov_describe() (label, value, bar); labels: none (ad hoc per screen) | B | ~40 readouts + ~80 hand labels; wrapper unit |
| echofield | 0.2.15 | us | none | none | PI[] (label, kind, names), PAGES[] (p[4]), knob_text | B | ~50-60 |
| elasticat | 0.6.97 | us (owner repo + glue) | follows pages | none | exports live labels (ec_core.c); EC_PARAMS, ec_app_item_format | B | TRIAL BUILT: 5 RFM1_READOUT_BAR lines in its repo (branch labels-readouts bd1c75d), not in the pin yet |
| etch | 1.88 | us | none | none | names[4]/values[4] only inside the draw routine parameters() (64 pages) | C | fixed list (drafted); B ~150 by capturing the footer draws |
| evilgenius | 0.1.4 | us | fixed: 9 buttons renamed (descriptor) | none | FIZZ derivative: qd_ctl_labels (49 slots), qd_view_turn, qd_view_edit | B | BUILT: core_evilgenius.c, FIZZ's adapter (~130 lines) |
| fartmachine | 1.1.0 | us | none | none | single page: names[4] + om_param_info | B | ~25 |
| felucca | 1.8.2 | us | none | none | Tier 2 map core_felucca_params.c (85 entries, knob_target) | A | BUILT here: WANTS rd1 |
| fimba | 0.7.0 | us | none | none | VIEW_KNOB, VIEW_BTN, view_knob, sp_knob | B | ~60 |
| fizz | 0.13.0 | us (RemoteFM1 repo) | follows pages | all knobs (+ held steps) | adapter in RemoteFM1 tools/rfm1/fizz/fizz_unit.c over qd_ctl_labels / qd_view_turn | done | shipped (0.13.0, rd1) |
| fmav1 | 0.6.4 | us | none | none | page_param / plabel / param_value (knob_view) | B | BUILT (batch 3): the page's knobs as knob_view draws them |
| fmel1 | 1.3 | us | none | none | mel.c CTL[page][4], fmt_val (draw_plate) | B | BUILT (batch 3): the plate |
| fmini | 2.6.0 | us | fixed: 1 buttons renamed (descriptor) | none | ui_fmini.c FMG_PAGE[].f[] (fmgf_t), fmg_field | B | ~100-150 |
| fmkick | 0.5.1 | us | none | none | knob_info(i) name / val (draw_footer) | B | BUILT (batch 3): the footer (HOME: none) |
| fmlarynx | 0.4.2 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| fomni | 0.5 | us | none | none | VIEW_KNOB[view][4], knob_name, knob_text | B | BUILT (batch 3): VIEW_KNOB / knob_text |
| frasil | 3.4 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| ghoulbox | 2.4 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca 1.4.1 model (3 symbols missing) | B | BUILT: Felucca map + crank SELECT, no EUCLID / HOME LEVELS |
| glitch | 2.0.2 | us | none | none | GR!MM 2 control map (same functions) | B | BUILT here: grimm_rd.h |
| golana | 1.3 | us | fixed: 1 buttons renamed (descriptor) | none | golana_ui.c GU_PAGES[] (btn, k[4] label/names), gu_text | B | ~60-80 |
| golf | 1.0 | us | none | none | PAGE[ui.page][4] par_t, par_text | B | BUILT (batch 3): PAGE / par_text |
| grimm | 2.0.2 | us | none | none | GR!MM control map: PAGES[] (name, btn, knob[4]), knob_name/param/min/max/text, ui_slab_make | B | BUILT here: grimm_rd.h (178 lines, shared) |
| guitchordi | 0.1.0-beta.1 | us | none | none | no table; fw_encoder ad hoc; 3 knob contexts | C | fixed list (drafted); hand page-aware ~60-90 |
| h1 | 0.3.0 | us | none | none | h1_control / h1_control_set API | B | BUILT (batch 3): h1_control, all seven knobs |
| hadal1 | 1.0.1 | us | none | none | knob_name / value_text, but most values drawn as bars (no text) and only while touched | B | ~50, readouts would show numbers the screen draws as bars |
| hawkchua | 2.1.7 | us | fixed: 1 buttons renamed (descriptor) | none | no params.c; ui.c edits ad hoc per sys.page (~20 pages) | C | fixed list (drafted) |
| hortator | 1.0.1 | us | fixed: 1 buttons renamed (descriptor) | none | own pages.c PAGES/TP/GP, page_desc, param_format (Felucca shape) | B | ~120-180 |
| hyp1 | 1.5.54 | us | none | none | VIEW_KNOB, KNOB_LABEL, knob_text | B | BUILT (batch 3): VIEW_KNOB / KNOB_LABEL / knob_text |
| jamesos | 1.0b | us | none | none | br_control / br_control_text (its reader's rd_knob) | B | BUILT (batch 3): its knob cards; levels through br_level_law as drawn |
| jiant | 10.26 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca 1.1.5 model; own knob dispatch (GEN, macros, lens) | B | ~150-250, reusable by its forks |
| karma | 3.1.5 | us | none | none | as SLOOP; HOME loop screen and OP screens its own | B | BUILT: sloop_rd.h (HOME: none) |
| kris_jangle | 0.15 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| kwasub | 1.1.03 | us | none | none | ksub_ui.c VIEWS[], item_name, item_text, cursor | B | ~70 |
| loafbreaker | 0.9.73 | us | none | none | per-module name tables (KN[NPAGES][4], knob_name) across 18 modules; values inline | C | fixed list (drafted); B ~300+ |
| loom | 0.17.1 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| matryoshka | 1.24.1 | us | none | none | Modular-1: pages built per patch (pages_ui.c), unit_of | B | ~100-150 |
| melodee | 1.6.1 | us | none | none | Tier 2 map core_melodee_params.c (86 entries, knob_target) | A | BUILT here: WANTS rd1 (+ Prophet HOME cells in %) |
| mightyarp | 1.5 | us | none | none | PAGE[] (p[4]), page_knob, par_name, param_text | B | BUILT (batch 3): HOME macros, page_knob |
| minitone | 1.0.5 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca 1.5 model (eu, sel_mode missing), six parts | B | BUILT: Felucca map + SELECT PAGES, no EUCLID |
| mnm1 | 1.0.1 | us | fixed: 1 buttons renamed (descriptor) | none | Hortator's model, 6 tracks | B | ~40 on top of a Hortator adapter |
| modelar | 0.3 | us | none | none | menu rows built per page (row_t/col_t), col_text; KAOS page ad hoc | B | ~80-100 (partial) |
| modularkoi | 0.1 | us | fixed: 1 buttons renamed (descriptor) | none | koi_ui.c KC_NAME[] + koi state (labels almost fixed) | B | ~30-50 |
| mono2k | 1.13 | us | none | none | m2k_ui.c knob_param(page,sub,k), page_label, pinfo, fmt | B | ~60-90 |
| mope1 | 0.5.0 | us | none | none | PAGE_KNOB, PAGE_LABEL, PAGE_BTN, sy_param_info | B | ~80 |
| morse | 0.8 | us | none | none | K1..K4 fixed (speed, spacing, pitch, send speed) | C | fixed list (drafted): already exact |
| mpyre | 1.0.3 | us | none | none | as SLOOP; FX, ARP, AMP its own screens | B | BUILT: sloop_rd.h (ENV, LFO, SEL ..; amp pages: none) |
| mutabledreams | 2.3 | us | none | none | reader.c reader_cards: only with a screen reader listening; label and value joined in one string | B | NOT built: needs a hook into reader_card (or md_named) ~60-100 |
| mvance | 2.7.1 | us | none | none | kui_knob(p, lab, ..) per screen kind (pages, HOME labels, others) | B | ~80-120: the screens' choice of p mirrored |
| mvsynth2 | 1.5.2 | us | none | none | VS_PAGES[].id, eng.patch.home, vs_param_label / ptext | B | BUILT (batch 3): a page's and HOME's knobs (bank, SAVE, GLOBAL: none) |
| nachtfahrt | 1.0.2 | us | none | none | VIEW_KNOB, KNOB_LABEL, knob_text | B | BUILT (batch 3): VIEW_KNOB (REC grid: none) |
| niagaradrift | 0.3.1 | us | none | none | VIEW_KNOB, knob_param (SEL held), dr_param_text | B | ~80 |
| nomansky | 0.9 | us | none | none | its knob band draws numbers only (ui_nms.c n_knobs), no names on screen | C | fixed list; values without names not useful |
| obervave | 3.5.2 | us | none | none | draw_page builds its knob_view_t k[4] locally (mixer, drive, global, layers, seq, params) | B | mirror draw_page's choices ~80-120 (shared with s1vave) |
| obssynth | 0.2.0 | us | fixed: 1 buttons renamed (descriptor) | none | ui.c PAGES (id[4]), ui_view, page_param; engine.c PARAMS[], param_text | B | ~50-70 |
| orbit | 1.0.0 | us | none | none | as SLOOP; ENV, LFO, FX, EDIT are its own screens | B | BUILT: sloop_rd.h (SEL, ARP only; its own pages ~60-100 more) |
| orgigs | 1.1 | us | none | none | *_cells (4 knob strings), *_btnmap (12 live button labels), par_fmt | B | ~80 |
| patchwerk | 1.0.3 | us | none | none | pw_param_desc per module, pw_param_at, pw_format_param | B | ~100-150 |
| penumbra | 1.9 | us | none | none | PAGE[], HKNOB, many *_text(), HELP[NB] | B | ~200 (or ~40 readouts via announce) |
| perf1 | 0.17.0 | us | none | none | VIEW_KNOB (~25 views), knob_text | B | ~80 |
| pigeon | 0.4.1 | us | fixed: 1 buttons renamed (descriptor) | none | PIGEON profile: KO's own views everywhere (ko_ui.c), Felucca pages unreachable | B | NOT the Felucca map (tried: wrong names); own adapter over ko_ui.c views ~80-120 |
| pingos1 | 1.0.1 | us | none | none | ch_device_desc / ch_param_desc, dev_cell_at (DEVICE view) | B | ~120-150 |
| pond | 0.1.2 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| purplemonkey | 0.12 | us | fixed: 6 buttons renamed (descriptor) | none | pm_ui.c ENC_KNOB[mode][enc]; names only in comments | C | hand names through its ENC_KNOB table ~40 |
| quantumgammawave | 4.1.0 | us | fixed: 1 buttons renamed (descriptor) | none | JIANT family model | B | ~50-80 on top of a JIANT adapter |
| quartz | 0.10 | us | none | none | q_page PAGE[] (label[4], min, max), knob_get, knob_text | B | ~60-80 |
| rainbow | 1.1.5-rb | us | fixed: 1 buttons renamed (descriptor) | none | kid.c runs from power-up with fixed roles (full Felucca after HOME+SAVE 3 s) | C | fixed list for kid mode |
| ravel | 1.45.0 | us | fixed: 1 buttons renamed (descriptor) | none | rv_ui.c pages[], page_knob, kb_format; rv_params.c rv_pinfo | B | ~100 (in ravel_app_unit.c) |
| remotefm1 | 0.4.0 | us | none | none | sa_menu_knob switch; knobs mostly MIDI CC sends | C | fixed list |
| rhythmer | 0.1.0-beta.2 | us | fixed: 1 buttons renamed (descriptor) | none | JIANT family model | B | ~50-80 on top of a JIANT adapter |
| ripple | 0.6.1 | us | none | none | knobs belong to each Lua droplet | C | fixed list (or a droplet label API) |
| ruido | 0.7.2 | us | none | none | formatColumnRoleAndValue(k) per knob bank (C++) | B | ~70-110 |
| rupture | 2.0.14 | us | none | none | ETCH's code + rupture_ui.c (ru_knob_labels per the drafting pass, unverified) | C | fixed list (drafted); B possible, cost unclear |
| s1vave | 2.0.1 | us | none | none | OberVave fork: draw_page as OberVave | B | mirror draw_page ~80-120 (shared with obervave) |
| samsara | 1.0.0 | author | none | none | author-built file, shipped unchanged | D | the author (rfm1_ext.h, or a RemoteFM1Metadata.json in their repo) |
| sid | 0.3 | us | none | none | fixed literals KN[4] per mode, tracker | C | fixed list per mode (2 modes) |
| sid1 | 1.0.1 | us | none | none | sid1.c KNOB[page][4], knob_get, knob_text | B | BUILT (batch 3): KNOB[page] / knob_text |
| sidj | 1.5.1 | us | none | none | PG_NAME, KNOB_SAY[NPG][4], st_cap | B | ~100 |
| sidou | 2.0.0 | us | fixed: 1 buttons renamed (descriptor) | none | names local to draw functions; menu tables only for settings | C | fixed list (drafted); B 300+ |
| skiff | 2.7 | us | none | none | Felucca 1.1.5 + 1.6.1 model, 6 tracks, own sheets | B | ~80-150 |
| sloop | 2.8.5 | us | none | none | Felucca params model; draw_column keeps ui.big_l / big_v / big_u | B | BUILT: sloop_rd.h (81 lines, shared) |
| sloop3 | 3.4.0 | us | none | none | view model: ui_view V (knob label + value strings) | B | BUILT (batch 3): main.c's view V (rd_cols.h) |
| smileyface | 1.1.5 | us | none | none | views.c KNAME[PAGES][4], knob_val (no text formatter) | B | ~30 (names + raw numbers) |
| speak37 | 1.0 | us | none | none | SP_PAGE[page][4], SK_NAME, sk_ptr, sk_text | B | ~120-180 |
| spellbox | 0.3 | us | none | none | main.c KN_NAME[8], knob_text, page | B | ~30-40 |
| stallion | 1.1 | us | none | none | as SLOOP | B | BUILT: sloop_rd.h |
| sy1 | 1.0.0 | us | none | none | PAGES[] (button, rows[][4]), cell_at, ux_label, ux_value | B | ~50-70 |
| tekstuur | 2.0 | us | none | none | Golf's model: par_name, par_text | B | BUILT (batch 3): par_name / par_text |
| tessera | 3.7.5 | us | none | none | Melodee ~1.3.6 model (TP/GP, PAGES, page_desc) | B | Melodee map + rework ~150-250 |
| tonitor | 0.55 | us | none | none | ad hoc if-chain on ui.*_page; partial name helpers | C | fixed list (drafted); hand adapter ~150-250 |
| trapstationos | 1.1.0.2 | us | fixed: 1 buttons renamed (descriptor) | none | Felucca model much extended; page_desc covers every scope | B | ~120-200 |
| trax | 0.9.5 | us | none | none | hand-written enc() dispatch per view | C | fixed list (drafted) |
| vector37 | 0.3.2 | us | none | none | pdesc_t CDESC/GDESC, LAYERS[].k[8], kd_desc, val_text | B | ~100-150 |
| vesper | 2.0.2 | us | none | none | FM1_HOST_TEST web_knobs (its web mirror) DISAGREES with the LCD (KEY F# vs F, STEP 02 vs 10) | B | NOT built: the mirror failed the screen check; mirror draw_page's knob() calls instead ~80-120 |
| viscera | 2.9.2 | us | none | none | vsc_params info[], slot(k), slot_text (statics of vsc_ui.c) | B | ~80-120; wrapper unit |
| x0x | 1.3 | us | none | none | ui.c pages_t pg (pref_t r[page][4]), pref_name, pref_value; announce | B | ~150 |
| yehnah | 0.2.0 | us | fixed: 1 buttons renamed (descriptor) | none | JIANT family model, bigger deltas | B | ~80-120 on top of a JIANT adapter |
| zen1 | 1.2 | us | none | none | scenes: amb_knob_text; pages: VIEW_KNOB, fm_param_text | B | BUILT (batch 3): the scene knob bar, else VIEW_KNOB |

"names today: fixed: n buttons renamed" is the descriptor's button_names (FX..OCT+ given other words by the glue), not live labels. "?" none. Versions are the release's files.

## What could not be determined

- Usage per firmware (no download counts were available), so "most used" could not guide the order.
- The B line counts are estimates from reading, not built; only grimm_rd.h was measured (178 lines with comments, ~120 of code, for three firmwares; plus a 15-line shim hook used by every later adapter).
- Whether symbols with Felucca's names keep Felucca's meaning in ghoulbox, minitone and pigeon (names checked, not behaviour).
- rupture: whether ru_knob_labels makes it a cheap B (the two passes disagree).
- Move builds: this repo builds no .rfm1m; remoteFM14Move tools/build_fw.sh detects rfm1_readout in a module and builds the rd1 glue itself (not run here: that checkout is in use).
