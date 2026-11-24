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
