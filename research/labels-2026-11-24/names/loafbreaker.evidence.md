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
