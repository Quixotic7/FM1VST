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
