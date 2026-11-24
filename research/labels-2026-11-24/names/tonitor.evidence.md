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
