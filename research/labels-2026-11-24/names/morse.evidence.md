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
