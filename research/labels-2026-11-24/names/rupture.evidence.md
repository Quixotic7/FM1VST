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
