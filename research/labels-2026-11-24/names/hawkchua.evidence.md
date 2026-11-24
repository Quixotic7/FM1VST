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
