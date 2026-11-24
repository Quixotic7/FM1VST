# Fixed names, batch 1 (drafts for the owner, 2026-11-24)

Ten class C firmwares (SURVEY.md): a fixed list of what the eight knobs and fourteen buttons do on the firmware's main screen, in the RemoteFM1Metadata.json format (docs/RFM1-METADATA.md), one file each in names/<id>.json. Every file passes tools/rfm1_labels.py (10 characters, printable ASCII). **Not built into any firmware.** A control left out keeps the host's default (the printed name; MASTER: volume); "" = no job on that screen. Each firmware's evidence table below gives the source of every name (file:line in the firmware at its catalog pin, rfm1-manager/cores/..., or the manual) and where the control does something else.

## At a glance

| id | knobs (SELECT ALGO PRESETS K1 K2 K3 K4) | buttons given | remaps by page? |
|---|---|---|---|
| etch | PAGE / "" / BPM / MORPH / ETCH / TONE / SPACE | SEL GUIDE, LFO MOD, EDIT DRAW, GLO SYSTEM | heavily (64 pages): B by capturing its footer draw would be better |
| rupture | PAGES / OSC / BPM / PRESSURE / BALANCE / MEMORY / DENSITY | EDIT EQ | heavily; K1-K4 differ by oscillator (CXS vs GROWL): owner to pick |
| tonitor | LEVEL / PAN / ENGINE / FILTER / GRAIN / CRUSH / REV SEND | FX TRACK 1, SEL TRACK 2, ENV TRACK 3, LFO TRACK 4, EDIT TRACK 5, GLO TRACK 6, HOME NEW SOUND, SAVE SESSIONS, ARP SCALE, SEQ FX/MOD | K1-K4 on ~7 pages; buttons stable |
| loafbreaker | TEMPO / BREAK / PATTERN / GATE / FILTER / DRUM FLT / SUB DECAY | FX EFFECTS, SEL SHIFT, ENV SONG, LFO MIX, EDIT SOUNDS, GLO SYSTEM, SEQ PATTERN, OCT- PITCH -, OCT+ PITCH + | almost every knob on every page (~25 knob sets) |
| sidou | PAGE / PROFILE / PRESET / CUTOFF / RESO / DECAY / RELEASE | FX LAB FX, SEL FX SLOT, ENV LAB ENV, LFO LAB MOD, EDIT FX EDIT, GLO MUTE/SOLO, HOME MENU, SEQ SONG | heavily; K3 differs by preset (DECAY / PULSE / RATIO) |
| chipbox | TAPE / PAGE / CHANNEL / TEMPO / LEVEL / SLIDE / DUCK | FX PUNCH-IN, SEL COPY, ENV ACCENT, LFO UNDO, EDIT CONSOLE, GLO HELP, HOME VIEW, ARP TAILS, SEQ STEP | buttons stable; K1-K4 on the CHIP view |
| hawkchua | OCTAVE / OSC / PRESET / CHAOS / SHAPE / OUTPUT / RING | SEL PAN, LFO FREE EG, GLO MIDI, SEQ KARMA, REC TAPE | ~20 pages; buttons fine as a fixed list |
| guitchordi | BPM / INSTR / STYLE / PATTERN / DIV / GATE / STRM/SWNG | SEL OK, ENV LOCK, LFO "", EDIT CHORD, ARP MODE, SEQ "", PLAY DAMP, REC "" | K1-K4 between three sets (Strum / Freeplay / rhythm) |
| trax | ROW / CHANNEL / KEY INSTR / NOTE / INSTR / VOLUME / FX VALUE | FX FX COLUMNS, SEL COPY/PASTE, ENV KEY-OFF, LFO MUTE, EDIT INSTRUMENT, GLO SONGS, HOME PATTERN, ARP UNDO, SEQ ORDER, REC EDIT MODE | knobs by view, FX flips to effect columns |
| morse | · / PAGE / "" / HEAR WPM / SPACING / PITCH / SEND WPM | FX SOUND, SEL CHOOSE, ENV "", LFO "", EDIT "", GLO "", HOME MENU, ARP "", SEQ VIEW, REC DEL / EDIT, OCT- PREV, OCT+ NEXT | no: the fixed list is exact (REC, OCT-/+ only act on some pages) |

(· = not given: the host's default.)

Open questions for the owner, from the drafting: ETCH's PRESETS (the code and README make it BPM outside the preset browser; ETCH-USER-MANUAL.md says it does nothing there); RUPTURE CXS or GROWL names; sidou K3; guitchordi Strum vs Freeplay; morse REC / OCT-/+ "" or their page jobs. Several of these would be better as class B later (etch, rupture, loafbreaker, sidou, hawkchua knobs): see each "Pages" line.

---

# ETCH: fixed control names (draft)

Repo: `rfm1-manager/cores/ETCH`. Code paths relative to it. Home screen = PLAY (`ETCH_UI_PLAY`, page 0, shown as SOUND in the README). Default workflow is SIMPLE (`bringup.c:495`).

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | PAGE | Turns through the home pages PLAY > MORPH > FILTER > (FX in SIMPLE) > LIBRARY | firmware/fm1-native/bringup.c:3647, native_ui_navigation.h:62-68 and 198-203; ETCH-USER-MANUAL.md section 6 "The home-screen carousel" | Yes: A/B/C wave choice in DRAW, FX editor choice on FX pages, step/tab/rate-mode on SEQ, SYSTEM, MOD WAVE |
| ALGO | "" | No job on PLAY: only used in sequencer views (inspector), MORPH (ETCH depth), PERFORMANCE (scene) and DRAW (brush) | bringup.c:3666-3669, 3714, 3858, 4317; ETCH-USER-MANUAL.md section 5 table, section 10 | Yes: MORPH = ETCH depth; DRAW = brush DETAIL/SOFT/EDGE; SEQ context = advanced views |
| PRESETS | BPM | Global tempo outside the preset browser, save and name pages; scrolls/loads presets inside PRESETS (hold HOME) | bringup.c:3413-3420, 3421-3425; README.md "Controls & shortcuts" ("Control BPM: Rotate PRESETS outside the preset browser") | Yes: preset browser = load preset. Note: ETCH-USER-MANUAL.md section 5 says PRESETS "does nothing outside the PRESETS browser"; the code and README say BPM, I followed them |
| K1 | MORPH | A-to-B carrier shape | bringup.c:1636-1643 (names "MORPH ETCH TONE SPACE"); ETCH-USER-MANUAL.md section 6 "PLAY: four immediate sound controls" | Yes: every page relabels K1-K4 (bringup.c parameters() 1375-1645: ENV, FILTER, MOD, FX editors, ARP, SEQ, DRAW...) |
| K2 | ETCH | Wave C modulation depth | same | Yes |
| K3 | TONE | Filter cutoff (code comment: "'TONE' means cutoff") | bringup.c:1637-1639 | Yes (manual table calls it FILTER / CUTOFF; screen says TONE) |
| K4 | SPACE | Reverb/spatial amount | bringup.c:1639-1642; manual section 6 | Yes |
| SEL | GUIDE | Outside sequencer views, goes through the GLO route: GUIDE > FIFTHS > SYSTEM; in SEQ views cycles SEQ > NOTES > HARM > WAVE SEQ | bringup.c:3655-3664, native_ui_navigation.h:133-143 | Yes: SEQ views step through editors; confirms in dialogs (workflow choice, flat-line prompt, storage prompt) |
| LFO | MOD | Opens MOD WAVE (Wave C); in ADVANCED cycles MOD > WAVE C FX > MATRIX > WAVE AMP | bringup.c:3652, native_ui_navigation.h:38-44, 213-215; manual section 5 table ("Cycle MOD WAVE > WAVE C FX > EVOLUTION") | In DRAW: redo (bringup.c:4321) |
| EDIT | DRAW | Enter / leave DRAW (waveform editor); hold 700 ms in DRAW = flat-line prompt | bringup.c:3427-3453; manual section 7 "Entering DRAW" | No |
| GLO | SYSTEM | SIMPLE (default): toggles SYSTEM page; ADVANCED: GUIDE > FIFTHS > SYSTEM | bringup.c:3680-3685, native_ui_navigation.h:223-225; manual section 5 table, line 828 | Depends on workflow (SIMPLE vs ADVANCED), not page |

Kept printed names (not labelled): FX (FX editors; DRAW: waveform FX), ENV (envelope; DRAW: undo), HOME (short = PLAY, hold = preset browser), SAVE (save editor), ARP, SEQ, PLAY (transport), REC (context confirm), OCT-/OCT+ (octave; both = POLY/MONO), MASTER (volume, manual section 5).

Pages: Yes, heavily. ETCH has 64 UI pages (native_ui_navigation.h:9-24) and K1-K4 are relabelled on almost every one (bringup.c parameters(), ~40 distinct name sets); SELECT, ALGO, SEL and REC are also page-dependent, and SIMPLE/ADVANCED changes what LFO/GLO/ARP/SEQ open. The fixed list is only right on the PLAY/SOUND page. A class B adapter (reading `native_page` / `native_draw` and the names[] the firmware already builds in parameters()) would be much better.

`names/etch.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "PAGE",
      "ALGO": "",
      "PRESETS": "BPM",
      "K1": "MORPH",
      "K2": "ETCH",
      "K3": "TONE",
      "K4": "SPACE"
    },
    "buttons": {
      "SEL": "GUIDE",
      "LFO": "MOD",
      "EDIT": "DRAW",
      "GLO": "SYSTEM"
    }
  }
}
```

---

# RUPTURE: fixed control names (draft)

Repo: `rfm1-manager/cores/RUPTURE`. Code: `vendor/etch-v1.88-source/firmware/fm1-native/` (abbreviated `fm1-native/` below). RUPTURE's own UI lives in `rupture_ui.c` and the `ru_*` handlers in `bringup.c` (the ETCH parameters() at ~2043 is the inherited ETCH table, not RUPTURE's). Home screen = PLAY (`RU_PAGE_PLAY`), oscillator CXS by default (`bringup.c:768`, `1645`).

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | PAGES | On pages with no SELECT job (PLAY among them), opens the screen list (MENU) | fm1-native/bringup.c:5499-5507; README.md line 52; CHEAT-SHEET.html "SELECT: GLOBAL SCREEN LIST" | Yes: step cursor on SEQ, FX slot on FX, slot/row/cursor in PROJECTS, SETTINGS, NAME, HELP |
| ALGO | OSC | On PLAY and SOUND SHAPE: main oscillator CXS (left) / GROWL (right) | bringup.c:5520-5523; CHEAT-SHEET.html "PLAY & HOME" ("ALGORITHM switches CXS / SAWTOOTH") | Yes: SUB 1/2 = BLEND; FX = reorder slot; EQ = EQ2/LEGACY; SEQ = note sustain; during live REC = metronome volume; NAME = character; HELP = scroll |
| PRESETS | BPM | Globally assignable knob, BPM by default (GLO > SETTINGS > PRESETS KNOB); SEQ held + PRESETS shifts the pattern | bringup.c:4741-4760, 6205; rupture_preset_assign.h:6, 18; CHEAT-SHEET.html "V2.0.5: PRESETS KNOB ASSIGNMENT" | Not by page, but the owner of the unit can reassign it; the label is then wrong |
| K1 | PRESSURE | CXS harmonics | rupture_ui.c:490 (ru_knob_labels PLAY row), 548-550; rupture_tutorial.h:13; README.md line 39 | Yes: with GROWL selected K1-K4 read BRIGHT / EDGE / BODY / LEVEL (rupture_ui.c:549); every page has its own row in ru_knob_labels (rupture_ui.c:489-500+) |
| K2 | BALANCE | CXS tone balance | same | Yes (GROWL: EDGE) |
| K3 | MEMORY | CXS oscillator memory | same | Yes (GROWL: BODY) |
| K4 | DENSITY | CXS oscillator complexity | same | Yes (GROWL: LEVEL) |
| EDIT | EQ | Opens Channel EQ; again cycles GAIN / FREQ / Q; on FX opens the selected effect's parameters | bringup.c:4984-4995; CHEAT-SHEET.html "FILTER / FX / LFO / EQ"; README.md line 29 ("EDIT for channel EQ") | On FX page: selected effect's parameters |

Kept printed names (not labelled): FX (tap = FX chain, hold 700 ms = BEND, bringup.c:4970-4979), SEL (confirm; HOME + SEL = undo last knob gesture), ENV, LFO, GLO (tap = GLO menu, hold = PANIC, bringup.c:5975-5982), HOME (short cycles PLAY > SHAPE > SUB 1 > SUB 2 > GLIDE, long = PLAY, bringup.c:4945-4959), SAVE (projects; sequence slots on SEQ), ARP, SEQ, PLAY (transport / count-in), REC (arm/overdub, SEQ page only, bringup.c:5025), OCT-/OCT+, MASTER (hardware volume; a separate MASTER VOLUME lives in GLO > SETTINGS).

Could not decide: whether the owner wants the GROWL names; the fixed list uses the CXS names because CXS is the default and NEW projects start on CXS.

Pages: Yes, heavily. Over 30 pages each with their own K1-K4 row (rupture_ui.c:489+), K1-K4 change with the oscillator even on PLAY, and SELECT/ALGO/SEL are page-dependent. The fixed list is only right on PLAY with CXS. A class B adapter reading `ru_page`, `ru_main_wave` and `ru_fx_selected` and serving the existing `ru_knob_labels` table would be much better.

`names/rupture.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "PAGES",
      "ALGO": "OSC",
      "PRESETS": "BPM",
      "K1": "PRESSURE",
      "K2": "BALANCE",
      "K3": "MEMORY",
      "K4": "DENSITY"
    },
    "buttons": {
      "EDIT": "EQ"
    }
  }
}
```

---

# tonitor: fixed control names (draft)

Repo: `rfm1-manager/cores/tonitor`. Code: `firmware/src/ui.c`. Home = the chosen track's view (no page up). Encoder order `EN_SELECT, EN_ALGO, EN_PRESET, EN_K1..K4` (ui.c:48); buttons `B_FX .. B_OCTUP` (ui.c:47).

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | LEVEL | Chosen track's level (OFF .. +4.5 dB) | ui.c:1054-1066; README.md "Playing it" table, SELECT knob | SCALE page: also the root (ui.c:1046-1052) |
| ALGO | PAN | Chosen track's pan | ui.c:1068-1080; README.md, ALGORITHM knob | SCALE page: the scale (ui.c:1039-1045) |
| PRESETS | ENGINE | Chosen track's engine FM / ANIMA / CALIGULA / RUSSOLO / DRUID / BAYAREA / HIVE | ui.c:1150-1156; README.md, PRESETS knob | SHIFT = new sound of next/previous family; SESSIONS / PRESETS / GLOBAL SETUP / MOD pages: list or entry choice (ui.c:1118-1148) |
| K1 | FILTER | Track's DJ filter (LP left, HP right) | ui.c:910-916; README.md KNOB 1 | EDIT (black key) = macro 1; ENVELOPE = attack; DELAY/REVERB/MOD/BALLSY/BRICKLAYER pages: their own (ui.c:844-909) |
| K2 | GRAIN | Track's granulator (dark / scatter) | ui.c:917-923; README.md KNOB 2 | same kinds of pages |
| K3 | CRUSH | Track's bit / rate crush | ui.c:924-930; README.md KNOB 3 | SHIFT + K3 = distortion (ui.c:838-843); other pages as above |
| K4 | REV SEND | Track's reverb send | ui.c:931-936; README.md KNOB 4 | SHIFT + K4 = delay send (ui.c:832-837); other pages as above |
| FX | TRACK 1 | Choose track 1; held 0.8 s = copy/paste track | ui.c:51, 710; README.md "FX SEL ENV LFO EDIT GLO (top row)" | No (also closes any page) |
| SEL | TRACK 2 | Choose track 2 | same | No |
| ENV | TRACK 3 | Choose track 3 | same | No |
| LFO | TRACK 4 | Choose track 4 | same | No |
| EDIT | TRACK 5 | Choose track 5 (the EDIT macro mode is a black key, not this button) | same; README.md line 108 | No |
| GLO | TRACK 6 | Choose track 6 | same | No |
| HOME | NEW SOUND | New sound for the chosen track; SHIFT = same family | ui.c:938-939; README.md HOME row | No |
| SAVE | SESSIONS | Short = SESSIONS page; held 0.8 s = save; SHIFT = GLOBAL SETUP | ui.c:463, 498, 795-830; README.md SAVE rows | On SESSIONS/ask pages it answers / loads |
| ARP | SCALE | SCALE page (SELECT root, ALGO scale); SHIFT = presets page | ui.c:775-795; README.md ARP rows | On PRESETS page: load (held = save) |
| SEQ | FX/MOD | Cycles DELAY > REVERB > MOD pages > back; SHIFT = random modulation | ui.c:750-772; README.md SEQ row | No |

Kept printed names (not labelled): PLAY (chosen track's loops stop/play, held = loops on keys, SHIFT = all; starts BALLSY/BRICKLAYER on their pages, ui.c:999), REC (chosen track's looper; held = empty; BRICKLAYER entry on its page, ui.c:940-975), OCT-/OCT+ (track octave; SHIFT = transpose loops, ui.c:1082-1116), MASTER (level, README.md line 133).

Pages: Moderate. The top-row buttons, HOME, SELECT, ALGO and PRESETS keep one job almost everywhere; K1-K4 change on the EDIT (macros), ENVELOPE, DELAY, REVERB, MOD, BALLSY and BRICKLAYER pages, and SHIFT (a black key) adds second jobs. The fixed list is right for the default track view and for all buttons. A class B adapter would help mainly K1-K4 (page flags `ui.edit`, `env_page`, `rv_page`+`fx`, `mod_page`, `bb_page`, `bl_page`); the macro names vary per sound, so they would need the firmware's own names.

`names/tonitor.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "LEVEL",
      "ALGO": "PAN",
      "PRESETS": "ENGINE",
      "K1": "FILTER",
      "K2": "GRAIN",
      "K3": "CRUSH",
      "K4": "REV SEND"
    },
    "buttons": {
      "FX": "TRACK 1",
      "SEL": "TRACK 2",
      "ENV": "TRACK 3",
      "LFO": "TRACK 4",
      "EDIT": "TRACK 5",
      "GLO": "TRACK 6",
      "HOME": "NEW SOUND",
      "SAVE": "SESSIONS",
      "ARP": "SCALE",
      "SEQ": "FX/MOD"
    }
  }
}
```

---

# loafbreaker (LOAF BREAKER): fixed labels, draft

Sources: cores/loaf-breaker (app/docs/MANUAL.md, app/ui/main.c). Main page = HOME (LIVE › PERFORM).

| control | label | what it does (short) | evidence | differs on other pages? |
| --- | --- | --- | --- | --- |
| SELECT | TEMPO | tempo, 1 BPM a detent | MANUAL.md §3 Playing ("SELECT: Tempo"); app/ui/main.c:1247 | yes: on pages with tabs it turns the top tabs (KICK sections, SONG, MIX, SYSTEM, FX slot, SEQ views); SEL + SELECT the drums' filter anywhere |
| ALGO | BREAK | next / previous break (at the next bar while playing) | MANUAL.md §2 "The rules", §3 table (ALGORITHM); app/ui/main.c:1255 | yes: elsewhere the element in the page (FX slot, SEQ lane, kick part, SYSTEM part); SEL + ALGO is the break anywhere |
| PRESETS | PATTERN | pattern P1–P64 (next one at the loop's end) | MANUAL.md §3 (PRESETS); §2 "The rules" | yes: on SOUNDS the source's memory (KICK 1–20, drum sound), on FX the effect, SONG TRACKS the track |
| K1 | GATE | cuts each slice short (100 % = off) | MANUAL.md §10 (HOME perform K1); app/ui/main.c:1916 | yes: every page has its own four (§10 table); HOME's 2nd tab GENERATOR = CMPLX ANCHOR ROLL FILL |
| K2 | FILTER | DJ filter on the break (left LP, right HP) | MANUAL.md §10; app/ui/main.c:1916 | yes, as K1 |
| K3 | DRUM FLT | the same filter on the drums | MANUAL.md §10; app/ui/main.c:1916 | yes, as K1 |
| K4 | SUB DECAY | length of the kick's SUB | MANUAL.md §10; app/ui/main.c:1916 (screen says "SUB DECAY", main.c:197) | yes, as K1 |
| FX | EFFECTS | opens the EFFECTS domain (the FX chain's four slots) | MANUAL.md §2 domain table (FX = EFFECTS); app/ui/main.c:1011 | again: back to HOME; SEL + FX bypasses a slot |
| SEL | SHIFT | modifier for every SEL + combination; tapped alone: shortcut card; twice: locked | MANUAL.md "Two buttons to remember", §2 SEL table; app/ui/main.c:1049 | no (same everywhere; SEL + K1–K4 = the kick's live knobs) |
| ENV | SONG | opens SONG (TRACKS, PLAY, DROP, BUILD) | MANUAL.md §2 domain table; app/ui/main.c:880 | SEL + ENV = build then drop; again: HOME |
| LFO | MIX | opens the MIXER (LEVELS, ROUTING, MASTER, DYNAMICS); held + K1–K4: levels from any page | MANUAL.md §2 table, §9; app/ui/main.c:861-864, 1147 | again: HOME |
| EDIT | SOUNDS | opens SOUNDS (the KICK menu; SEL + EDIT: DRUMS, BREAK) | MANUAL.md §2 table (EDIT = SOUNDS); app/ui/main.c:784 | again: the next sound; held: the next view |
| GLO | SYSTEM | opens SYSTEM (MIDI, SETUP, DISPLAY, SCREEN FX, LIGHTS, RESET) | MANUAL.md §2 table; app/ui/main.c:779-782 | confirms RESET; quits the TUTORIAL |
| SEQ | PATTERN | opens PATTERN (the sequencer's lanes) | MANUAL.md §2 table (SEQ = PATTERN), §3; app/ui/main.c:911 | SEL + SEQ copy / paste pattern; again: clear confirm |
| OCT- | PITCH - | break pitch down a semitone (both: 0) | MANUAL.md §3 (OCT− / OCT+); app/ui/main.c:1098-1110 | no |
| OCT+ | PITCH + | break pitch up a semitone | MANUAL.md §3; app/ui/main.c:1098-1110 | no |

Left at the printed name (they do what they say): HOME (LIVE: PERFORM / GENERATOR / SPLIT, main.c:751), SAVE (main.c:1084; SEL + SAVE undo), ARP (its page of effects, main.c:976; label "ARP" in the firmware too), PLAY, REC (records keys into the pattern, main.c:951), MASTER (volume, MANUAL.md §3).

Pages: heavy remapping. Every button is a domain page and SELECT / ALGO / PRESETS / K1–K4 change job on nearly every page and tab (MANUAL.md §10 lists ~25 knob sets); held black keys, held white keys and SEL also take K1–K4. The fixed list is right only on HOME's PERFORM tab. The firmware already has the names in code (`knob_name()` / `KN[NPAGES][4]` at app/ui/main.c:1913, and the Studio's ED_X knob_name SysEx), so it would be much better as class B (an adapter reading `ui.page` / tab and serving `knob_name()`), or as live labels exported by the core.

`names/loafbreaker.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "TEMPO",
      "ALGO": "BREAK",
      "PRESETS": "PATTERN",
      "K1": "GATE",
      "K2": "FILTER",
      "K3": "DRUM FLT",
      "K4": "SUB DECAY"
    },
    "buttons": {
      "FX": "EFFECTS",
      "SEL": "SHIFT",
      "ENV": "SONG",
      "LFO": "MIX",
      "EDIT": "SOUNDS",
      "GLO": "SYSTEM",
      "SEQ": "PATTERN",
      "OCT-": "PITCH -",
      "OCT+": "PITCH +"
    }
  }
}
```

---

# sidou: fixed labels (draft)

Main screen: LIVE, knob row on its TONE page. Sources: README.md "Controls" (lines 198-224), docs/reference/controls.md "Panel" (lines 124-164), checked against app/live_view.c and app/panel.h. Printed SEL is SIDOU's SCL (app/panel.h:21 `PANEL_BTN_SCL 3u`). Omitted (printed name already fits): SAVE, ARP, PLAY, REC, OCT-, OCT+, MASTER (volume).

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | PAGE | pages LIVE's knob row: TONE, DIST, MOD, FX | docs/reference/controls.md:151; app/live_view.c:703 (row title TONE / slot name / ARP) | yes: menu/SETTINGS selection, CHIP focus, TRACKER row, STEP lane, SCENE piece, LAB page |
| ALGO | PROFILE | the chip profile on TONE (R2, R3, R4AR, R5, WORN; so the chip model too); on a slot page or with EDIT held, the slot's algorithm | docs/reference/controls.md:152; README.md:210 | yes: slot algorithm on DIST/MOD/FX pages, menu/SETTINGS values, TRACKER voice, STEP page, LAB card choice |
| PRESETS | PRESET | the preset (SAW LEAD, PWM BASS, SYNC LEAD, NOISE PERC, then DICE and USER sounds) | docs/reference/controls.md:135; README.md:209 | yes: lane/song instrument in STEP, INSTR, TRACKER with REC on |
| K1 | CUTOFF | filter cutoff | docs/reference/controls.md:131; app/live_view.c:627; app/live.h:178 LIVE_KNOB_CUTOFF | yes: slot Amount on a slot page, ARP RATE while ARP held, CHIP's first value, LAB card 1, song-view values |
| K2 | RESO | resonance | docs/reference/controls.md:132; app/live_view.c:627; app/live.h:179 | yes: slot A, ARP ORDER, CHIP's second, LAB card 2 |
| K3 | DECAY | "the sound's own control": DECAY on SAW LEAD / NOISE PERC, PULSE on PWM BASS, RATIO on SYNC LEAD | docs/reference/controls.md:133; app/live_view.c:628 (`K3[3] = {"RATIO","PULSE","DECAY"}`); app/live.h:180 LIVE_KNOB_TONE | yes, even on LIVE it changes with the preset (owner: pick DECAY, PULSE or a combined word); slot B, ARP SPAN, LAB card 3 |
| K4 | RELEASE | envelope release 0-15 | docs/reference/controls.md:134; app/live_view.c:627; app/live.h:181 | yes: slot Mix, CHIP's fourth, LAB card 4 |
| FX | LAB FX | opens LAB at its FX page (lit while the 8580 plays) | docs/reference/controls.md:130 | yes: on a song view opens PUNCH; steps LAB pages inside LAB |
| SEL | FX SLOT | selects the next effect slot: Distortion, Modulation, FX | docs/reference/controls.md:138; app/panel.h:21 | EDIT + SEL = DICE in STEP/TRACKER (controls.md:329, 505) |
| ENV | LAB ENV | opens LAB at its ENVELOPE page | docs/reference/controls.md:130 | STEP with a step held; EDIT + ENV = SWEEP in STEP/TRACKER |
| LFO | LAB MOD | opens LAB at its MOD page | docs/reference/controls.md:130 | TRACKER/STEP: clear / rest; EDIT + LFO = undo |
| EDIT | FX EDIT | held + K1-K4: the selected effect slot's Amount / A / B / Mix; lit while the slot has an algorithm | docs/reference/controls.md:137; README.md:211 | LAB: tap for A/B compare; many EDIT + X chords in song views |
| GLO | MUTE/SOLO | held (or double-tap latched): keys 1-3 mute, 4-6 solo the song's voices | docs/reference/controls.md:144-145 | same in every view |
| HOME | MENU | on release: the menu from LIVE; back one view elsewhere; held 600 ms: help card | docs/reference/controls.md:139; README.md:216 | elsewhere it is BACK |
| SEQ | SONG | opens the song views, then steps STEP, TRACKER, ORDER, INSTR, TABLE | docs/reference/controls.md:141; README.md:213 | same everywhere |

Pages: heavy. SIDOU remaps the knobs by LIVE's knob page (TONE / DIST / MOD / FX), by ARP held, by EDIT held, and per view (menu, SETTINGS, CHIP, LAB with four cards a page, STEP, TRACKER, ORDER, INSTR, TABLE, SCENE, NOW PLAYING); even on LIVE's TONE page K3's job follows the preset. It already draws its own knob names (app/live_view.c knob_label, app/lab_view.c), so it would be much better as class B (an adapter reading the view, knob page and preset) than as a fixed list.

`names/sidou.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "PAGE",
      "ALGO": "PROFILE",
      "PRESETS": "PRESET",
      "K1": "CUTOFF",
      "K2": "RESO",
      "K3": "DECAY",
      "K4": "RELEASE"
    },
    "buttons": {
      "FX": "LAB FX",
      "SEL": "FX SLOT",
      "ENV": "LAB ENV",
      "LFO": "LAB MOD",
      "EDIT": "FX EDIT",
      "GLO": "MUTE/SOLO",
      "HOME": "MENU",
      "SEQ": "SONG"
    }
  }
}
```

---

# chipbox (Chipbox): fixed labels, draft

Sources: cores/chipbox (site/guide/index.html = the "Manual", README.md, firmware/src/chipbox.c, chipview.c). Main view = the RACK (`view = 1` at start and on every project open, chipbox.c:470, 518).

| control | label | what it does (short) | evidence | differs on other pages? |
| --- | --- | --- | --- | --- |
| SELECT | TAPE | the tape dial: right plays (½, 1, 2x), left backwards, middle pauses; stopped: moves the cursor | guide §3 The rack; chipbox.c:932-937 | STEP: cursor; SONG: slot cursor; DRAW page: wave step; projects: tile |
| ALGO | PAGE | the rack's page (past the last makes a new one) | guide §3; chipbox.c:5, 920-929 | CHIP: the knobs' page (tone / envelope / arp / wave ...) |
| PRESETS | CHANNEL | down the channels (last row: add) | guide §2 ("PRESETS selects the channel"); chipbox.c:4, 912-918 | SONG: the page in the slot; projects: tile |
| K1 | TEMPO | tempo 40–240 BPM | guide §3; chipbox.c:7, 942 | CHIP: first card (DUTY / WAVE / NOISE / ALGO, by chip; chipview.c:515 `chip_cards`); Add list: groups |
| K2 | LEVEL | the channel's level | guide §2 ("KNOB 2 ... level"); chipbox.c:945 | CHIP: card 2 (VOL on page 1); STEP: note length |
| K3 | SLIDE | slides the channel's steps along the page | guide §2 ("KNOB 3 to slide the channel's steps"); chipbox.c:946-952 | CHIP: card 3 |
| K4 | DUCK | how far a kick ducks the channel | guide §2 ("KNOB 4 to duck ... when a kick plays"); chipbox.c:953 | CHIP: card 4 |
| FX | PUNCH-IN | the white keys become 16 punch-in effect pads | guide §11 Effects; chipbox.c:707 | no |
| SEL | COPY | hold on a channel: copy it (patch, or patch and notes) | guide §2 ("Hold SEL on a channel to copy it"); chipbox.c:726-734 | CHIP on an FM chip: next operator; Add list: console card; projects: hold copies a project |
| ENV | ACCENT | held + a white key in the drum lanes: an accent | guide §7 Drums; chipbox.c:589 | no other job found (does nothing in the rack) |
| LFO | UNDO | undo (6 changes, no redo) | guide §10 Undo; chipbox.c:744-751 | no |
| EDIT | CONSOLE | tap on a channel: give it another console; hold: remove the channel | guide §2; chipbox.c:9, 908-911 | STEP: clear the block / the drum; REC: hold to erase; SONG: tap opens, hold copies the page; projects: hold deletes |
| GLO | HELP | the manual at the view's chapter; GLO + HOME: the menu | guide §1; chipbox.c:11, 627, 675 | no |
| HOME | VIEW | tap: the next view (RACK, CHIP, SONG); hold: projects | guide §1; chipbox.c:10, 624-628, 688 | always "back" in a submenu |
| ARP | TAILS | note tails on / off | guide §3 ("ARP shows or hides note tails"); chipbox.c:741 | no |
| SEQ | STEP | step entry for the channel (or the drum lanes); again: back | guide §1, §4; chipbox.c:669-671 | no |

Left at the printed name: SAVE (save the project, chipbox.c:753), PLAY (tap: play from this page, hold: loop it; chipbox.c:691-699), REC (arm recording, chipbox.c:714), OCT- / OCT+ (octave ±2, chipbox.c:11, 900-904; they are also yes / no in dialogs and add / remove a slot in SONG), MASTER (volume, chipbox.c:11).

Pages: moderate remapping. Four views (RACK, CHIP, SONG, STEP) plus projects and the FX pad layer. The buttons keep their jobs almost everywhere; the knobs change a lot: on the CHIP view K1–K4 are the chip's own cards, different per console and per page (chipview.c `chip_cards`, FM operators via SEL), and SELECT / PRESETS / ALGO move to slots and pages in SONG / STEP. Fixed labels are right for the rack and all buttons; the CHIP view's knob names would need class B (an adapter reading the view, channel kind and `chip_page`, or the core exporting `chip_cards` names as live labels).

`names/chipbox.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "TAPE",
      "ALGO": "PAGE",
      "PRESETS": "CHANNEL",
      "K1": "TEMPO",
      "K2": "LEVEL",
      "K3": "SLIDE",
      "K4": "DUCK"
    },
    "buttons": {
      "FX": "PUNCH-IN",
      "SEL": "COPY",
      "ENV": "ACCENT",
      "LFO": "UNDO",
      "EDIT": "CONSOLE",
      "GLO": "HELP",
      "HOME": "VIEW",
      "ARP": "TAILS",
      "SEQ": "STEP"
    }
  }
}
```

---

# hawkchua (HAWK CHUA): fixed labels (draft)

Main screen: HOME (page 0, the oscillator page, CHUA circuit). Sources: docs/HANDLEIDING.md "Bediening" (Dutch manual, lines 39-70), checked against firmware/src/ui.c (button handler ~4266-4375, knobs ~4589-4604, encoders ~4661, ~4763). Printed SEL is the firmware's B_SCL. Omitted (printed name already names the page it opens or the action): FX, ENV, EDIT, HOME, SAVE, ARP, PLAY, OCT-, OCT+, MASTER (volume).

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | OCTAVE | on HOME: the sound's octave, -3..+3 | firmware/src/ui.c:4661; docs/HANDLEIDING.md:53 | yes: SELECT is "the page's one extra setting" (POSITION, TONE, LATCH, PLAY, CLOCK, MOD ENV target ...), manual line 52 |
| ALGO | OSC | on HOME: the oscillator, CHUA or one of the noise programs (A0..D1, BIG BANG, PIANO) | firmware/src/ui.c:4763-4764; docs/HANDLEIDING.md:54 | yes: "the kind" per page: FX model, space, MOD FX mode, ARP pattern, GE, LOGIC, tape track |
| PRESETS | PRESET | the preset within the bank (4 banks x 128) | docs/HANDLEIDING.md:55; firmware/src/ui.c:3084 (HOME header shows the preset) | yes: FX presets on FX, shimmer presets on SHIMMER, ARP presets on ARP/GROOVE (ui.c:4773), SEQ presets on KARMA |
| K1 | CHAOS | circuit alpha | docs/HANDLEIDING.md:44; firmware/src/ui.c:4601 | yes: noise programs X (ui.c:4590); every page its own (ARP BPM, MOD ENV LENGTH, PAN BASIS, reverb size ...) |
| K2 | SHAPE | circuit beta | docs/HANDLEIDING.md:45; firmware/src/ui.c:4602 | yes: noise Y; per page |
| K3 | OUTPUT | which voltage is heard (X, X+Y, Y, Y+Z, Z) | docs/HANDLEIDING.md:46; firmware/src/ui.c:4603 | yes: noise LEVEL (BIG BANG / PIANO: BANG / DECAY); per page |
| K4 | RING | low pass gate resonance | docs/HANDLEIDING.md:47; firmware/src/ui.c:4604 | yes: BIG BANG EXPAND / PIANO STIFF; per page |
| SEL | PAN | the PAN page (BASIS, ORBIT), again back HOME | firmware/src/ui.c:4367-4368; docs/HANDLEIDING.md:59 | toggles a page, same everywhere |
| LFO | FREE EG | the FREE EG page (record a knob move), again back HOME | firmware/src/ui.c:4359-4360; docs/HANDLEIDING.md:65 | same everywhere |
| GLO | MIDI | the MIDI page, again SOUND, again UI STYLE, then back | firmware/src/ui.c:4371-4372; docs/HANDLEIDING.md:67 | cycles pages |
| SEQ | KARMA | the KARMA phrase-generator page (it starts playing), again LOGIC, then back | firmware/src/ui.c:4320-4333; docs/HANDLEIDING.md:60 | cycles pages |
| REC | TAPE | the TAPE page; on TAPE: record; on FREE EG: record the knob | firmware/src/ui.c:4286-4298; docs/HANDLEIDING.md:58 | yes: records on TAPE and FREE EG pages |

Pages: heavy. Every button except SAVE/PLAY/OCT opens or cycles pages (about 20: HOME, BANK, ENV, MOD ENV, MOD FX, resonator, shimmer, ARP, GROOVE, KARMA, LOGIC, FREE EG, EDIT, PAN, MIDI, SOUND, UI STYLE, TAPE, TAPE SPEED, MULTI), and K1-K4, SELECT, ALGORITHM and PRESETS take that page's parameters (on HOME the knob names also change with the oscillator). Button labels hold up well as a fixed list; knob labels only fit HOME with CHUA. Class B (an adapter reading sys.page and sys.osc) would be much better for the knobs.

`names/hawkchua.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "OCTAVE",
      "ALGO": "OSC",
      "PRESETS": "PRESET",
      "K1": "CHAOS",
      "K2": "SHAPE",
      "K3": "OUTPUT",
      "K4": "RING"
    },
    "buttons": {
      "SEL": "PAN",
      "LFO": "FREE EG",
      "GLO": "MIDI",
      "SEQ": "KARMA",
      "REC": "TAPE"
    }
  }
}
```

---

# guitchordi: fixed control names (draft)

Repo: `rfm1-manager/cores/guitchordi`. Code: `firmware/app.c`; hardware map `firmware/fm1/main.c:33-34` (encoder roles 0 SELECT, 1 ALGO, 2 PRESETS, 3-6 K1-K4, 7 MASTER; buttons 0..13 = FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+). Home = play page (page 0). Factory performance mode is Strum (docs/performance-modes.md line 3; README.md line 19), so K1-K4 on home are the rhythm controls.

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | BPM | Tempo 40-240 BPM, on every page | app.c:436; docs/performance-modes.md lines 5, 24 | No (only the reset prompt takes PRESETS) |
| ALGO | INSTR | Instrument (16 tones) | app.c:447; screen column "ALGORITHM" app.c:671-673; README.md line 41 | No |
| PRESETS | STYLE | Chord style (50 banks); with EDIT on, the chord assigned to the selected slot | app.c:448-453; screen column "PRESET" app.c:671-672; README.md line 43 | EDIT on = slot chord; reset prompt = choice |
| K1 | PATTERN | Strum/Arp pattern (screen "1 PAT") | app.c:438-442, 650; docs/performance-modes.md table | Freeplay or FX page: Intensity (app.c:454, 659); GLO page: gate tails (app.c:437, 656) |
| K2 | DIV | Beat division (screen "2 DIV") | same | Freeplay/FX: Transpose; GLO: Humanity |
| K3 | GATE | Gate 25-100 % or FREE (screen "3 GATE") | same | Freeplay/FX: Room; GLO: Tuning |
| K4 | STRM/SWNG | Strum sweep (Strum) or Swing (Arp) (screen "4 STRM" / "4 SWNG") | app.c:441, 650 | Freeplay/FX: per-slot Voicing; GLO: rhythm velocity |
| SEL | OK | Contextual OK: confirm reset, end editing, leave GLO page | app.c:405-411; docs/performance-modes.md line 24 | No |
| ENV | LOCK | Hold 3 s: lock / unlock chord | app.c:395-398, 476-478; docs/performance-modes.md line 32 | No |
| LFO | "" | Reserved, no job | app.c:412 ("LFO reserved"); docs/performance-modes.md line 24 | No |
| EDIT | CHORD | Tap: chord-assign editing (PRESETS picks the chord); hold 1.2 s: slot reset prompt | app.c:399-400, 414, 480-483; docs/performance-modes.md line 26; manual PDF p.4 "Edit a chord slot" | No |
| ARP | MODE | Cycles Freeplay > Strum > Arp | app.c:413; docs/performance-modes.md line 5 | No |
| SEQ | "" | No job (falls to `default`) | app.c:403-428 (no case 9) | No |
| PLAY | DAMP | Damps all sound and clears pending rhythm notes; hold 5 s = hidden loop | app.c:418-424, 470-475; docs/performance-modes.md line 28; manual PDF p.5 "PLAY damps sound" | No |
| REC | "" | No job (falls to `default`) | app.c:403-428 (no case 11); manual PDF p.5 lists REC as unused | No |

Kept printed names (not labelled): FX (toggles the intensity/transpose/room/voicing page, app.c:404), GLO (global settings page; hold 3 s = reset prompt, app.c:415, 484-488), HOME (back to play page, app.c:416), SAVE (save, RAM-only on the device, app.c:417), OCT-/OCT+ (transpose -/+12, app.c:425-426), MASTER (volume, app.c:461; docs line 24).

Note: the PDF manual (21 October 2026, browser guide) is older than the code: it says SEL is held 5 s for lock and SELECT changes the LCD page. The code and docs/performance-modes.md say ENV (3 s) and SELECT = BPM; I followed the code.

Could not decide: K1-K4 for users who play in Freeplay (INTENSITY / TRANSPOSE / ROOM / VOICING) - the fixed list follows the factory Strum mode.

Pages: Light. Only K1-K4 change: three sets (rhythm on the play page in Strum/Arp, intensity/transpose/room/voicing in Freeplay or on the FX page, globals on the GLO page), plus K4's Strum/Arp name. Everything else keeps one job. A class B adapter reading `page` and `play_mode` would get K1-K4 exactly right with little work; otherwise class A is acceptable.

`names/guitchordi.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "BPM",
      "ALGO": "INSTR",
      "PRESETS": "STYLE",
      "K1": "PATTERN",
      "K2": "DIV",
      "K3": "GATE",
      "K4": "STRM/SWNG"
    },
    "buttons": {
      "SEL": "OK",
      "ENV": "LOCK",
      "LFO": "",
      "EDIT": "CHORD",
      "ARP": "MODE",
      "SEQ": "",
      "PLAY": "DAMP",
      "REC": ""
    }
  }
}
```

---

# trax (FM1-TRAX): fixed labels (draft)

Main screen: the Pattern view (HOME). Sources: README.md "Controls" (lines 117-140), docs/DESIGN.md section 5 (lines 194-272), checked against firmware/src/app/ui_pat.c (pat_input, pat_knob) and firmware/src/app/ui_views.c (global buttons ~1402-1480). Note the code is under firmware/src/app/, not app/. Omitted (printed name already fits): SAVE, PLAY, OCT-, OCT+, MASTER (volume).

| control | label | what it does (short) | evidence | differs on other pages? |
|---|---|---|---|---|
| SELECT | ROW | the cursor row (fast turns jump 4 / 16) | firmware/src/app/ui_pat.c:262-276; docs/DESIGN.md:234 | yes: Order position, instrument-editor row, PSG macro step, library/song list scroll |
| ALGO | CHANNEL | the cursor channel | firmware/src/app/ui_pat.c:278-290; docs/DESIGN.md:235 | yes: Order channel; FM editor: the algorithm (DESIGN.md:257) |
| PRESETS | KEY INSTR | the instrument the keys play and enter | firmware/src/app/ui_views.c:1456; README.md:133 | in the instrument editor: steps instruments |
| K1 | NOTE | the cell's note | firmware/src/app/ui_pat.c:292-293 (pat_knob k=0); docs/DESIGN.md:236 | yes: FX mode FX1 command; Order pattern number; FM editor OP1's parameter; PSG step value; DAC sample |
| K2 | INSTR | the cell's instrument | docs/DESIGN.md:236; firmware/src/app/ui_pat.c pat_knob | yes: FX mode FX1 value; Order all row's patterns; OP2; PSG length |
| K3 | VOLUME | the cell's volume | firmware/src/app/ui_pat.c:225-230; docs/DESIGN.md:236 | yes: FX mode FX2 command; Order song length; OP3; PSG loop |
| K4 | FX VALUE | effect 1's value | firmware/src/app/ui_pat.c:231-235; docs/DESIGN.md:236 | yes: FX mode FX2 value; Order loop point; OP4; PSG release |
| FX | FX COLUMNS | flips K1-K4 to FX1 cmd, FX1 val, FX2 cmd, FX2 val | firmware/src/app/ui_pat.c:295-296; README.md:135 | instrument editor: macro page; Songs: demo song |
| SEL | COPY/PASTE | hold + SELECT/ALGO marks a block; tap copies; hold 1 s pastes | firmware/src/app/ui_pat.c:263-269, 323-336; README.md:136 | instrument editor: opens the Library; Library/Songs: load |
| ENV | KEY-OFF | writes key-off at the cursor; with a block marked clears it | firmware/src/app/ui_pat.c:297-; docs/DESIGN.md:237 | Order: insert a position; FM editor: envelope page; Songs: new song |
| LFO | MUTE | mutes the channel; held: solo | firmware/src/app/ui_pat.c:313-322; README.md:138 | Order: duplicate / held delete; FM editor: modulation page; Songs: held twice delete |
| EDIT | INSTRUMENT | the instrument editor for the instrument under the cursor | firmware/src/app/ui_views.c:1424; README.md:127 | in the editor: its tone page (PSG: noise mode) |
| GLO | SONGS | the Songs view; again: Setup | firmware/src/app/ui_views.c:1415; README.md:128 | same everywhere |
| HOME | PATTERN | the Pattern view; again: the Stage view | firmware/src/app/ui_views.c:1402; README.md:125 | same everywhere |
| ARP | UNDO | undo; again: redo (Pattern and Order views) | firmware/src/app/ui_views.c:1454; README.md:132 | nothing outside Pattern/Order |
| SEQ | ORDER | the Order list; again: song settings | firmware/src/app/ui_views.c:1406; README.md:126 | same everywhere |
| REC | EDIT MODE | edit mode on/off: the keys write notes (records live while playing) | firmware/src/app/ui_views.c:1448; README.md:130 | same everywhere |

Pages: moderate. The buttons are mostly global and fit a fixed list well. The knobs change by view (Pattern, Order, FM/PSG/DAC instrument editor, Library, Songs, Setup, Stage) and, in the Pattern view, by the FX toggle (cell fields vs effect columns). A fixed list covers the Pattern view with FX off; class B (an adapter reading the view and ui.fxmode) would help most for the FX toggle and the instrument editor's per-operator pages.

`names/trax.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "SELECT": "ROW",
      "ALGO": "CHANNEL",
      "PRESETS": "KEY INSTR",
      "K1": "NOTE",
      "K2": "INSTR",
      "K3": "VOLUME",
      "K4": "FX VALUE"
    },
    "buttons": {
      "FX": "FX COLUMNS",
      "SEL": "COPY/PASTE",
      "ENV": "KEY-OFF",
      "LFO": "MUTE",
      "EDIT": "INSTRUMENT",
      "GLO": "SONGS",
      "HOME": "PATTERN",
      "ARP": "UNDO",
      "SEQ": "ORDER",
      "REC": "EDIT MODE"
    }
  }
}
```

---

# morse (fm1-morse): fixed labels, draft

Sources: cores/fm1-morse (README.md "Controls", app.c). Starts on the MENU page (README "Controls"; SETTINGS › START PAGE can change it). The keys are the Morse key / paddles (not labelled).

| control | label | what it does (short) | evidence | differs on other pages? |
| --- | --- | --- | --- | --- |
| ALGO | PAGE | the next page (PRACTICE, LISTEN, KEY, RHYTHM, TYPE, CHART, SETTINGS) | README.md Controls ("ALGO: the next page"); app.c:2077-2082 | no (refused while CHART is being edited) |
| PRESETS | "" | no job: read and thrown away | app.c:2099 `(void)plat_enc(EN_PRESET)`; not in README | no |
| K1 | HEAR WPM | hearing speed (WPM) | README.md Controls (K1 "hearing speed"); app.c:2091, 1807-1816 (screen "HEAR nn WPM") | CHART in edit: the letter's level (app.c:2083-2085) |
| K2 | SPACING | Farnsworth spacing between letters | README.md Controls; app.c:2093, 1819-1835 (screen "SPACING nn WPM") | CHART in edit: groups open |
| K3 | PITCH | sidetone pitch, 300–1200 Hz | README.md Controls; app.c:2095, 1848-1861 | no |
| K4 | SEND WPM | sending speed: keyer, decoder start, RHYTHM target | README.md Controls; app.c:2097, 1838-1846 (screen "SEND nn WPM") | no |
| FX | SOUND | sound on / off (off: read the key lights) | README.md Controls; app.c:2001, 1863-1867 (screen "SOUND") | CHART edit: all one box back |
| SEL | CHOOSE | MENU: open the page picked; PRACTICE: choose / don't know; LISTEN: peek; TYPE: live / compose; SETTINGS: step | README.md page table; app.c:1711-1746 (`page_sel`: "choose, skip, switch") | yes, per page (as listed) |
| ENV | "" | no job | no B_ENV in app.c; not in README | no |
| LFO | "" | no job | no B_LFO in app.c; not in README | no |
| EDIT | "" | no job | no B_EDIT in app.c; not in README | no |
| GLO | "" | no job | no B_GLO in app.c; not in README | no |
| HOME | MENU | the menu; again: back | README.md Controls; app.c:2000 | answers "no" to questions (app.c:1963) |
| ARP | "" | no job | no B_ARP in app.c; not in README | no |
| SEQ | VIEW | a page's view: KEY text/code/timing, TYPE code on keys, LISTEN show/hide, PRACTICE peek | README.md page table; app.c:2005-2024 | nothing on MENU, RHYTHM, CHART, SETTINGS |
| REC | DEL / EDIT | TYPE: delete (held: clear); CHART: edit the progress | README.md page table; app.c:2025-2030 | nothing on other pages |
| OCT- | PREV | TYPE: previous page of keys; SETTINGS: change down; KEY: clear | README.md page table; app.c:2031-2043 | CHART edit: letter level down; nothing on MENU |
| OCT+ | NEXT | TYPE: next page of keys; SETTINGS: change up | README.md page table; app.c:2031-2043 | CHART edit: letter level up; nothing on MENU |

Left at the printed name: SELECT (moves the choice on every page: MENU entry, LISTEN program, RHYTHM drill, CHART letter, SETTINGS item; app.c:1749-1790), SAVE (save now, app.c:2002), PLAY (start / again / stop; on MENU opens the page, app.c:1672-1708; "yes" to questions), MASTER (volume).

Not determined: whether REC / OCT± deserve "" instead of a label, since they do nothing on the start page (MENU); the draft labels their main jobs on TYPE / SETTINGS.

Pages: light remapping. ALGO, K1–K4 and FX, HOME, SAVE are fixed everywhere (only CHART's edit mode borrows K1/K2, FX, OCT±, SEL). SELECT, PLAY, SEL, SEQ, REC and OCT± do a page's job across 8 pages. A fixed list is adequate; class B would only help SEL / SEQ / REC / OCT± (an adapter reading `ui.page`).

`names/morse.json`:

```json
{
  "schema": 1,
  "labels": {
    "knobs": {
      "ALGO": "PAGE",
      "PRESETS": "",
      "K1": "HEAR WPM",
      "K2": "SPACING",
      "K3": "PITCH",
      "K4": "SEND WPM"
    },
    "buttons": {
      "FX": "SOUND",
      "SEL": "CHOOSE",
      "ENV": "",
      "LFO": "",
      "EDIT": "",
      "GLO": "",
      "HOME": "MENU",
      "ARP": "",
      "SEQ": "VIEW",
      "REC": "DEL / EDIT",
      "OCT-": "PREV",
      "OCT+": "NEXT"
    }
  }
}
```

