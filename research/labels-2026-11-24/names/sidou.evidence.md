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
