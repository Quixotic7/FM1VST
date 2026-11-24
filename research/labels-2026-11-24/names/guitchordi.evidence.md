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
