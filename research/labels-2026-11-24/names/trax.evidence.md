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
