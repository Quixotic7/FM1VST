# FM1VST

The FM-1 emulator from [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) as a plugin,
planned as VST3, AU and Standalone. The emulator compiles the firmware's own C source against a host
HAL, so the plugin plays exactly what the device plays. The plan, the architecture and the phases are
in [FM1-VST-PLAN.md](FM1-VST-PLAN.md).

Status: phase 4. Three firmwares build as loadable core modules behind a versioned C ABI: ChoralRoot
(`build/cores/choralroot.fm1core`), Felucca (`felucca.fm1core`) and Melodee (`melodee.fm1core`), each from its
unmodified source; the plugin's firmware dropdown switches between them in place. A JUCE-free engine loads a core,
runs its device clock and resamples its output to the host rate, and a JUCE plugin (AU, VST3, Standalone) plays it
with the eight physical knobs as host parameters that follow the firmware's screen (the Roto-Control map, plan 4.5;
the firmware's own parameters by name as an opt-in) and the panel's 41 buttons and keys as host parameters, MIDI in and out, the flash image as its state, and presets per firmware.
The editor is the FM-1 panel itself (phase 3): LEDs, LCD, mouse and keyboard as in the emulator, with colour themes.

## Layout

| path | what |
|---|---|
| `core-api/fm1core.h` | the core ABI (`FM1CORE_ABI 3`): the descriptor `fm1core_t` (id, name, version, source, flash size, button labels, the parameter map `fm1param_t` and `param_epoch`, the panel state `emu_hal_t`, every `emu_fw_*` entry point, `shutdown`, `halted`, and the flash image: `flash`, `flash_dirty`, `flash_stage` to boot from bytes, `flash_sync`) and the one symbol a module exports, `fm1core_get`. The threading contract is in its header comment. |
| `core-glue/choralroot/` | the ChoralRoot core: `core_choralroot.c` (the submodule's `tools/emu/emu_fw.c`, unmodified, with `exit` / `atexit` redirected so a firmware reboot halts the core instead of the host), `core_choralroot_params.c` (the Tier 2 parameter map and the knobs' targets, included into the same unit so it reaches the firmware's UI code) and `core_choralroot_desc.c` (the descriptor) |
| `core-glue/felucca/`, `core-glue/melodee/` | the Felucca and Melodee cores, the same three files (`core_<id>.c`, `core_<id>_params.c`, `core_<id>_desc.c`) plus, because these firmwares have no emulator of their own, its equivalent written here: `<id>_fw.c` (the `emu_fw_*` hooks, as ChoralRoot's `emu_fw.c`), `<id>_firmware.h` (the include list, as `emu_firmware.h`) and `<id>_hal.h` (the HAL on the host, as `emu_hal_fw.h`) |
| `cores/` | the firmwares, git submodules, read-only, pinned (below) |
| `plugin/` | the JUCE plugin: `Processor.{h,cpp}` (`FM1Processor`: the core and its `Device` on the audio thread, parameters and the Tier 2 feedback loop, MIDI, state, presets, the firmware switch; the threading and file layout are in its header comment), `Tier2Parameter.{h,cpp}` (a rebindable host parameter slot, and `KnobParameter`: a physical knob that follows the firmware's screen), `Editor.{h,cpp}` (the settings bar and the panel; the theme picker), `PanelComponent.{h,cpp}` (the FM-1 panel: the emulator's drawing, layout, mouse and key map) and `Theme.{h,cpp}` (colour themes: the derived palette, the presets, custom theme files) |
| `engine/` | C++17, no JUCE: `cores.{h,cpp}` (find and load modules, one private copy per instance), `device.{h,cpp}` (the single-thread device clock, input, LEDs, LCD, `render` at any host rate), `resampler.h` (4-point Lagrange) |
| `tests/` | `core_smoke` and `param_test` (per core), `replay_test`, `double_load_test`, `halt_test`, `plugin_test`, `tier2_test`, `panel_render_test`, and `script_runner` (the emulator's script grammar on a `Device`) |

## Cores

A core is the firmware compiled as a module (`*.fm1core`, a Mach-O bundle exporting only `fm1core_get`). The plugin
looks for them in its bundle (`<bundle>/Contents/Resources/cores/`; the build puts `build/cores/*.fm1core` there, and
a test or a dev build falls back to `build/cores/`) and in the user folder
`~/Library/Application Support/fm1emu/cores/` (created when first scanned): dropping a module there installs it (a user
module with the same id as a bundled one replaces it). Because the firmware's state is C globals, every loaded
instance runs its own copy of the module, copied to `~/Library/Caches/fm1emu/instances/<uuid>/` and deleted when the
instance goes away.

The bundled cores, each built from its firmware's own, unmodified source (a pinned submodule under `cores/`):

| core | what | source (submodule, pin) | licence |
|---|---|---|---|
| `choralroot` | ChoralRoot FM-1: a chord instrument (strum, arp, pattern, harp, a bass part, a looper), forked from Felucca; its own emulator (`tools/emu`) is the core | `cores/ChoralRootFM1`, <https://github.com/Quixotic7/ChoralRootFM1> at `44453d0524e4aae93ed4c13adfc78b0a75bec55a` | GPL-3.0-only, Quixotic7 (on Felucca and Melodee: their lines below) |
| `felucca` | Felucca 1.0.1: the multi-engine synth (13 engines: ANALOG, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, DRUM, NOISE, FM6, SLICE; the CC0 samples), four parts, a 64-step sequencer, the song chain | `cores/Felucca`, <https://github.com/hugelton/Felucca> at `20c275e39f75fa820978032efaceddfc5283c8cb` (tag `v1.0.1`) | GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments |
| `melodee` | Melodee 0.11.1: Felucca's fork with the FM6 (Dexed's synthesis) and CZ-1 engines, pattern banks and USB audio (the sample engines removed) | `cores/melodee`, <https://github.com/keremimo/melodee> at `9c271e26872fb293cd05cbf7520cbc74f098af82` (tag `v0.11.1`) | GPL-3.0-only, Felucca's copyright plus modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) |

Felucca and Melodee have no emulator, only their host tests (`tests/hostsim.c`, the DSP; `tests/ui_test.c`, the UI
against HAL stubs). Their cores are this repo's equivalent of ChoralRoot's emulator unit, written against the
firmware's own power-on and main loop (`firmware/src/main.c`): the unit list follows `felucca.c` / `melodee.c`
(hostsim's sound side, the HAL, `audio.c`, the UI, `storage.c` on a RAM image of the 1 MiB NOR, the user presets and
projects; Melodee also its FM6 and CZ-1 stores), the 1 ms tick delivers the panel's edges and encoder steps as
`fm1_input_tick` does, MIDI goes through `usb.c`'s queues, a flash erase plays the device's ~45 ms of silence, the
power-on splash lasts as on the device (430 ms), and the device's UBOOT entries (two failed boots, OCT- + OCT+ held
5 s, the USB UBOOT SysEx) halt the core. Built out, as the host tests build them: the update entry (OTA) and with it
the web editor's SysEx, the serial console, USB audio (Felucca's UAC input, Melodee's "Melodee Out / In"), the TRS
UART poll. On the device, Felucca's and Melodee's song (the parts' sounds and patterns) is RAM until SAVE > PROJECT,
and so it is in the core: the plugin state keeps the flash (settings, projects, user presets), not unsaved edits.

**Adding a core.** A firmware of the Felucca family becomes a core with a folder `core-glue/<id>/` and one call in
`CMakeLists.txt`:

```cmake
fm1_add_core(<id> core-glue/<id> SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/cores/<repo> UNIT core_<id>.c
             DESC core_<id>_desc.c PREFIX <XX> GEN <the headers its tools/build.py generate() writes>
             VERSION_FILE firmware/src/<unit>.c VERSION_REGEX "^#define <NAME>_VERSION \"v?([^\"]*)\"")
```

which runs the submodule's generate step into its gitignored `build/gen`, builds the unit and the descriptor once
(OBJECT) and links `core_<id>` (STATIC) and `build/cores/<id>.fm1core` (MODULE, exporting only `fm1core_get`), puts it
into every plugin bundle (signed) and registers `core_smoke_<id>` and `param_test_<id>`. The three glue files: the
unit (`core_<id>.c`: the firmware with `exit` / `atexit` / `fopen` redirected, the flash hooks, the map included),
the parameter map (`core_<id>_params.c`, optional: `nparams` 0 is valid) and the descriptor (`core_<id>_desc.c`: id,
name, version, source, button labels, the hooks). A firmware that, like Felucca, has no emulator also needs the three
emulator files (`<id>_fw.c`, `<id>_firmware.h`, `<id>_hal.h`): copy Felucca's.

## The plugin

`FM1VST` as **AU** (`aumu Fm1v Qx7u`, an instrument with MIDI out), **VST3** (Instrument|Synth) and a **Standalone**
app. The build installs the plugins (ad-hoc signed, with the cores inside) to
`~/Library/Audio/Plug-Ins/VST3/FM1VST.vst3` and `~/Library/Audio/Plug-Ins/Components/FM1VST.component`; the
Standalone stays in `build/FM1VST_artefacts/Standalone/`.

**In Live:** add FM1VST (VST3 or AU) to a MIDI track and play notes 53..79 (F3..G5): they press the FM-1's 27 keys,
so ChoralRoot plays its chords as on the device. The firmware needs under a second to power on after the plugin
is loaded. The track's MIDI output carries what the firmware sends (ChoralRoot by default: the chord stream on channel 1; the
bass on channel 2 once a bass sound is chosen), so it can drive other instruments.

- **Parameters (49):** first the eight physical knobs (`knob_master` `knob_select` `knob_presets` `knob_algo`
  `knob_1` .. `knob_4`, panel order: below), then the 41 panel controls: the 14 buttons (`btn_fx` .. `btn_octup`,
  momentary: on = held; named with the firmware's role and the printed label, e.g. "KEY (SEL)") and the 27 keys
  (`key_00` .. `key_26`, named by note, "F3" .. "G5"; a key is held when its parameter or a MIDI note holds it).
  **Only the knobs ever report a change to the host**: a press on the panel never moves a button or key parameter,
  and nothing else is a parameter, so Live's Configure mode collects exactly the knobs you turn. The firmware's own
  parameters by name (79 Tier 2 slots, below) are an **opt-in**: `-DFM1_TIER2_SLOTS=79` (8 + 79 + 41 = 128, Live's
  limit for showing a plugin's parameters without configuring them).
- **The knobs (the Roto-Control's first page, plan 4.5).** One host parameter per physical knob, in panel order, with
  **names that never change** (a Roto-Control binds by name): **Knob Master, Knob Select, Knob Presets, Knob Algo,
  Knob 1, Knob 2, Knob 3, Knob 4**. Knob Master is the pot (0..1023, default 724 as the emulator powers on; its value
  text "Master: 724"). The other seven are always **whatever that knob does on the firmware's screen right now**, and
  that goes into the **value text**: "Function: value", e.g. on ChoralRoot's view Knob 1 reads "Voicing: -1", Knob
  Select "Tempo: 147 BPM", Knob 3 "Strum Rate: 126 ms", Knob 4 "Chord Reverb: 33"; hold PERF and Knob 1..4 read the
  perform mode's row ("Strum Rate: .." .. "Strum Hold: Off"; pick Arpeggiate and they become Division, Dir, Gate,
  Swing); the FX layer gives the effect's row, the KEY layer Tonic / Scale / Transpose / Single Notes, the sound
  editor the cells of the lane on screen; on Felucca's HOME the engine's four knobs, on its ENV page Attack ..
  Release, FX held its macros. Each knob is a continuous 0..1 parameter over its current function's range (quantised
  inside to the firmware's steps), so nothing the host caches about it (name, steps, default) ever changes and the
  plugin never sends "parameter info changed" for a knob; when the screen changes (within a UI frame, whatever
  caused it) the new value is pushed without a gesture and a VST3 host is asked to re-read the values
  (`restartComponent(kParamValuesChanged)`: Live ignores a value change outside a gesture), so the Roto-Control's
  motor moves to it and its display shows the new text. For 150 ms after a knob's function changed, host writes to
  it are dropped (they were meant for the old function: the motor had not moved yet) and the host is told the new
  value again. **Who reports:** a knob
  turned on the unit, the panel GUI or its keys reports on that knob, inside a gesture (a touch: Live's Configure
  collects it, automation records when armed); a knob whose function's value changed for another reason (a PRESETS
  turn loads a sound whose reverb send differs from the one Knob 4 shows; a MIDI CC) is updated without a gesture;
  a value the host already has is not sent again. Where a knob has **no value** on the current screen (SELECT
  scrolling Options or the editor's lanes, PRESETS and ALGORITHM browsing sounds, a page's empty column), it is a
  **relative control** (value text "turn"): a host change becomes detents at the device (the change of the 0..1 value
  x 24 per full travel, the knobDetents setting) and the parameter springs back to 0.5 after 400 ms (that re-centre
  turns nothing), so the knob still scrolls; turned on the unit, it is nudged off the centre by its detents (inside a
  gesture) and springs back the same way. The core says what each knob is (`fm1core_t.knob_target`, ABI 4); the
  per-screen tables are in the headers of `core-glue/*/core_*_params.c`. Entries that exist only as knob targets
  (the sound editor's pages, Options rows, FX Effect, Felucca's FX macros and T1..T4 levels, the cells no named entry
  covers) are `FM1P_HIDDEN`: they never get a Tier 2 slot of their own.
- **The firmware's parameters by name (Tier 2, plan 4.5; opt-in: `-DFM1_TIER2_SLOTS=79`, off by default).** For
  direct mappings of a specific parameter whatever the screen shows. Each slot reports its own entry's changes,
  inside a gesture, so with the slots on Live's Configure also collects the sends and parameters a knob or a preset
  moves. Absolute values with the firmware's own ranges and value
  texts ("1/8", "120 ms", "Up-down", "-10.0dB"), read from and written to the running firmware, so a motorised
  controller such as the Melbourne Instruments Roto-Control follows them (the knobs above are the same values, bound
  to whatever is on screen). A host change goes through the firmware's own panel code (the screen, the knob row's hot
  cell, the settings record and the trace behave as for a knob turned on the unit); a change on the firmware's side
  (its knobs, a preset, a MIDI CC, a mode change) moves the host's value, as a touch (inside a change gesture). After
  every power-on (a preset load, a state restore, a flash reset) every value is reported once, without gestures, so
  the controller sweeps to the unit's real state. The values are not stored separately in the plugin state: the flash
  image is the truth. Slots beyond a core's visible map are "(unused)". They come after the knobs (`t2_00` ..
  `t2_78`), eight a page:

  **ChoralRoot** (73 visible, 109 with the hidden knob targets):

  | the slots, in order |
  |---|
  | Voicing, Tempo, Transpose, Chord Level, then the perform parameters of every mode, the ones its engine uses: Strum (Rate, Dir, Range, Hold), Slop (Amount, Rate, Dir, Range, Hold), Arp (Division, Dir, Gate, Swing, Range, Retrig, Hold), Pattern (Type, Div, Gate, Swing, Range, Rotate, Retrig, Hold), Harp (Rate, Dir, Gate, Range, Hold) |
  | chord and global: Perform, Perform Mode, Latch, Key Mode, Key Tonic, Key Scale, Single Notes, Split Point, Play Style, Ext Addition, Secret Chords, Velocity, Bass, Bass Mode, Bass Register, Bass Level, Metronome, Click Level, Time Signature, Loop Length, Loop Quantize, Count-In, Loop Level |
  | FX: FX on, the chord part's sends (Chord Drive / Chorus / Delay / Reverb: the FX amounts), the bass part's sends, the shared buses (Reverb Size / Damp / Type, Chorus Rate / Depth, Delay Time / Feedback / Colour) |

  **Felucca** (73 visible, 85 in all) and **Melodee** (the same layout): the selected part's parameters, as the device's pages show
  them, and the song's. "The selected part" follows the unit: pick another part (the ALGORITHM knob, or the `Part`
  parameter) and every part slot reads and writes that one. A host write is the knob's own write (the value clamped
  to the firmware's range, `motion_capture`: a live edit is the motion sequencer's new base, recorded while the part
  records; Melodee shares SCALE / QUANT across the parts as its knob does).

  | the slots, in order |
  |---|
  | Level, Drive, Delay Send, Reverb Send; ENV: Attack, Decay, Sustain, Release |
  | ENV DEST: Env>Filter, Env>Pitch, Env>Shape; Pan |
  | the engine (EDIT 1 / EDIT 2): `E1`..`E8`, named, ranged and texted as the selected part's engine has them ("E1 WAVE" SIN..S&H for ANALOG, "E1 ALG" 0..32 for FM6; the host is told when the engine or the part changes) |
  | LFO: Rate, Wave, Phase, Fade; LFO DEST: >Pitch, >Filter, >Shape, >Amp |
  | Chorus Send, Voice Mode, Glide, Glide Mode, Priority, Allocation, Detune, Mute |
  | ARP: Mode, Rate, Octaves, Gate, Swing, Chance, Hold, Order |
  | SCL: Scale Root, Scale, Quantize, Transpose; CHORD: Chord, Chord Voicing; Part; Tempo |
  | Swing, Tune; the FX buses: Delay Time / Feedback / Colour / Mix, Reverb Type / Size / Damp, Chorus Rate / Depth; PATTERN: Length, Div, Swing, Gate (not while a song plays, as the knob); MOD slots 1 and 2: Source, Dest, Amount |

  No named slot (but the knobs reach them on their pages, through the hidden cells): the SLICER insert, MOD slots 3
  and 4, the clock source and MIDI routing (GLO > SYSTEM), the operator pages, Melodee's FM6 patch pages (a copy of
  the patch, written back by `fm6_page_put`) and CZ-1 tone pages (the same through `cz_ed_put`), its BOOT / DRUM
  device settings. Not a value at all (relative on the knobs): the engine and preset choice (a sound load, with its
  undo and the engine's fade), the transport (PLAY / REC are buttons). Everything is part or song (the
  project, saved by SAVE > PROJECT on the unit), except Melodee's Tune, which it keeps as last used in its settings.

  `param_test` prints the whole map with ranges, flags (H: hidden) and the firmware path each entry is written
  through. **`-DFM1_EXPOSE_EDITOR=ON`** (off by default) gives ChoralRoot's sound editor entries (ENV, LFO, MOD and
  MIX for the chord and the bass part, 30) slots of their own (off they are hidden: the knobs reach them in the
  editor); build the plugin with `-DFM1_TIER2_SLOTS=103` (or more) to fit them (that goes past Live's 128). A sound
  edit is kept only by SAVE on the unit.
- **MIDI in:** with **MIDI notes play keys** on (default), note n on any channel holds key `n - 53 + 12 x Transpose`
  (Transpose: -2..+2 octaves; velocity 0 is a release). Every other message (CCs, program changes, clock, SysEx, and
  notes outside the keys or with the setting off) goes to the firmware's own MIDI in as USB-MIDI packets. A note that
  played a key is not also sent to the firmware (ChoralRoot's CHORD channel is 1, so it would sound twice); the toggle
  "... and go to the firmware's MIDI in" sends it as well.
- **MIDI out:** the firmware's USB-MIDI packets, decoded (SysEx reassembled), at the position in the block where they
  were produced.
- **State:** the firmware's id and version, the settings (transpose, the MIDI toggles, the theme, the editor size and big-LCD view) and the whole 1 MiB
  flash image, gzip-compressed (a few KB), so a Live set reopens with the unit exactly as it was saved.
- **Firmware dropdown:** every core found in the two folders; switching saves the old one's flash, loads the new one
  and powers it on from its own flash.
- **Presets per firmware** (the preset dropdown, Save / Save as / Rename / Delete / Reset flash / Export / Import, and
  the host's program list):

  ```
  ~/Library/Application Support/fm1emu/
    cores/                       user-installed *.fm1core
    <core-id>/flash.bin          the working flash: what a new instance of that firmware starts from (written when
                                 an instance is released or closed, before a firmware switch, and on Save)
    <core-id>/presets/*.fm1preset  a snapshot of the whole flash
    <core-id>/backups/*.fm1preset  written before a preset load or a flash reset (the newest 50 kept)
    themes/*.json                custom colour themes (one per file), settings.json: the default theme
  ```

  A `.fm1preset` is one line of JSON (`{"fm1preset":1,"core":"choralroot","version":"0.1","name":"..","date":"..",
  "size":1048576}`), a newline, then the flash image gzip-compressed (`tail -n +2 X.fm1preset | gunzip > flash.bin`).
- **`FM1EMU_HOME`:** when set, it replaces `~/Library/Application Support` (data under `$FM1EMU_HOME/fm1emu/`) and
  `~/Library/Caches` (`$FM1EMU_HOME/Caches/fm1emu/`). The tests set it to a folder in the build.

## The panel (phase 3)

The editor is the FM-1 itself: a port of the ChoralRoot emulator's window (`tools/emu/emu.c`, `keymap.c`) as a JUCE
component (`plugin/PanelComponent.{h,cpp}`) under a slim settings bar (firmware, presets and their buttons, Transpose,
the two MIDI toggles, Theme, Big LCD, the status with Power on). The host parameters have no generic list any more;
they remain the host's.

- **Drawing.** The panel in the designer geometry (904 x 566 units, the 876 x 538 view shown), drawn with the
  emulator's own rasteriser (anti-aliased rounded boxes, rings, round-capped pointers, its 5x7 font): the body, the
  key and button beds, the 27 keys with their LED bars, the 14 buttons with the firmware's labels on the caps
  (ChoralRoot: FX KEY BASS LATCH EDIT OPT / HOME SAVE PERF METRO LOOP REC) and the panel's printed labels small on the
  bed where they differ (SEL ENV LFO GLO over the top row, ARP SEQ PLAY under the bottom one), the OCT buttons, the 8
  knobs with their names and pointers (a knob bound to a firmware value points at that value's place in its range
  over 270 degrees, as MASTER shows its 0..1023 position, and moves whenever the value does, from the host, a
  Roto-Control, the panel or the firmware; its function is a small caption under its name, e.g. KNOB1 / VOICING; a
  relative knob is endless: its pointer turns 15 degrees per detent the device is given, from the panel or the host), the computer-key hints in green, and the LEDs as the
  firmware sets them: lit / dim / off, REC red, PLAY orange plus its green LED. The panel is rasterised once per size
  and theme at the screen's physical pixels (sharp on Retina at any size); after that only the controls whose state
  changed (held, LED, pointer, selection) and the LCD are redrawn, polled at 60 Hz.
- **The LCD** is the firmware's 240 x 240 RGB565 framebuffer on the panel's screen. The window is resizable with the
  panel's aspect kept and snaps to an integer multiple of the LCD when one is within 8 %: the LCD is then drawn
  nearest-neighbour, pixel for pixel (at other sizes it is smoothed). The default size puts the panel at 1.6 points
  per unit (1401 x 861 points: about 80 % of a 1080p screen's height; the LCD at 1x, 2x on Retina). **Big LCD** (the
  bar's button, or the backtick key) shows the LCD above the panel as a square as wide as the panel (snapped down to
  a multiple of 240 when one is within 10 %; the frame letterboxes in the window). The size and the big-LCD view are
  remembered per instance in the state.
- **Mouse:** click a key or a button to press it (released on mouse up); right-click or ctrl-click latches it down
  until clicked again; the wheel over a knob turns it one detent per notch (MASTER: 16 of 1023); dragging a knob up /
  down turns it (8 points a detent); a click on a knob selects it for Up / Down (the green ring).
- **Keyboard** (click the panel first; physical US-layout keys, `keymap.c`'s map; the F keys may need `fn`):

  | computer keys | FM-1 |
  |---|---|
  | `A S D F G H J K L ; ' ]` | white keys C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 |
  | `W E T Y U O P` | black keys C#4 D#4 F#4 G#4 A#4 C#5 D#5 (F#5: the mouse only) |
  | `F1 F2 F3 F4` / `2 3 4 5` / Tab | F#3 G#3 A#3 C#4 / F3 G3 A3 C4 / B3 (ChoralRoot's LOCK) |
  | `Z` `X` (also Esc / Return) | OCT- / OCT+; End: both (panic) |
  | F5 .. F10 | the top button row (FX SEL ENV LFO EDIT GLO; ChoralRoot: FX KEY BASS LATCH EDIT OPT) |
  | `7 8 9 0 - =` | the bottom row (HOME SAVE ARP SEQ PLAY REC; ChoralRoot: HOME SAVE PERF METRO LOOP REC) |
  | Page Down / Page Up | select the next / previous of SELECT, KNOB1 .. KNOB4 |
  | Up / Down | turn the selected knob one detent (repeats; MASTER: 32 of 1023) |
  | Shift + Up / Down | fine steps (the firmware's SHIFT: GLO held around the detent); outside the editor, OPT's second function |
  | `` ` `` | the big LCD view |

  The emulator's screenshot, recording and dump keys are not mapped. Cmd shortcuts stay the host's. Losing the
  keyboard focus releases every key the keyboard or the mouse holds (latches stay); closing the editor releases
  everything.
- **How the panel reaches the firmware.** The panel is its own "held" source, as the emulator merges its keyboard,
  mouse and latch sources: `FM1Processor::panelKey` / `panelButton` set atomics that the audio thread merges at each
  block start with the host parameters and MIDI (keys: `key_NN` | MIDI notes | panel; buttons: `btn_*` | panel). A
  panel press never moves the `btn_*` / `key_*` host parameters, so automation and the Roto-Control are unaffected,
  and a press released before the next block still reaches the firmware as a tap. `panelEnc` queues encoder detents
  for the audio thread; MASTER is the host's `master` parameter, which the panel moves as any plugin GUI moves a
  parameter (inside a gesture). Shift + Up / Down holds GLO around the detent on the device clock (`emu.c`
  fine_turn). The GUI never reads the core: the audio thread copies the LCD, the LEDs and the held state into a
  snapshot after each block in which they changed, under a lock it only tries.

### Themes

The real FM-1 comes in several colours; the panel's colours are a theme (`plugin/Theme.{h,cpp}`): three picked
colours, **Base** (the body), **Membrane** (the silicone keys and buttons) and **Knobs**, plus two optional overrides,
**Bed** (the recessed plate the keys sit in; else a darker shade of the membrane) and **Label** (the printed labels;
else white or dark, whichever contrasts more with the body). Everything else is derived so a theme stays legible: the
edge lines from the base, the pressed cap (lightened), the LED-off slot (darkened) and the cap outlines from the
membrane, the knob pointer by contrast with the knob, the text on caps and beds by contrast. The LEDs' lit colours
(white, red, orange, green) are fixed. `panel_render_test` checks every preset against contrast floors (label / body
3.0, pointer / knob 2.0, LED-off / cap 1.3, pressed / cap 1.3).

- **Presets** (`themes/presets.json`, embedded in the plugin): Black, Black/Green, Cool Gray, Orange, Purple,
  White/Blue (sampled from M-VAVE's product photos in `reference/MVaveOfficialColors/`) and Emulator (the ChoralRoot
  emulator's own palette).
- **Custom:** the Theme menu's **Custom...** opens colour pickers for Base, Membrane and Knobs, Bed and Label with an
  "override" switch each, and a name; every change previews live. **Save** writes
  `~/Library/Application Support/fm1emu/themes/<name>.json`; **Save as default** also makes it the theme new
  instances start with (`fm1emu/settings.json`, `"defaultTheme"`); **Delete** removes the file. Custom themes are
  listed after the presets. A theme file has the keys of a presets entry (`bed` and `label` optional):

  ```json
  {"name": "My Teal", "base": "#1F3B3D", "membrane": "#E07A3C", "knob": "#E8E2D6", "bed": "#B85F2C", "label": "#F4F0E8"}
  ```

- **In the state:** the theme's name and colours are stored with the instance, so a set reopens looking the same even
  when its custom theme file is gone (the menu then lists it as "<name> (this set)").

## Building

The firmware sources come in as a git submodule, so clone recursively:

```sh
git clone --recursive https://github.com/Quixotic7/FM1VST.git
# or, in an existing clone: git submodule update --init --recursive
```

Then:

```sh
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The Tier 2 slots (the firmware's parameters by name) are off by default; `cmake -B build -DFM1_TIER2_SLOTS=79` turns
them on (`FM1_TIER2_SLOTS` is a cache variable: a build folder configured before the default became 0 keeps 79 until
it is set again, `-DFM1_TIER2_SLOTS=0`).

The tests:

- `core_smoke_<id>` (`choralroot`, `felucca`, `melodee`): the module, loaded as the plugin loads it, boots, draws,
  sounds when D4 is held (and not before), does not halt; its screen goes to `build/cores/<id>.ppm`.
- `replay_test`: `tools/emu/scripts/cr_dmaj.txt` and `cr_allsynth.txt` through a `Device` on the loaded module must
  give the reference emulator's samples bit for bit at 44.1 kHz and the same `expect` results; the same run rendered
  at 48 kHz must match the reference resampled by a windowed sinc within a stated tolerance.
- `double_load_test`: the module loaded twice in one process, two scripts interleaved millisecond by millisecond,
  each equal to its single-load run (separate globals); one unloaded while the other plays on; ChoralRoot and
  Felucca loaded together, both sounding.
- `halt_test`: the firmware's two `exit()` paths (the boot guard's UBOOT, SAFE MODE's Flash Data reboot) halt the
  core and leave the host running.
- `param_test_<id>`: a core's parameter map on a `Device`: every entry set to min, max and its default reads back at
  once and 50 ms later, the enum texts are distinct, exactly the entries the settings record holds reach the flash
  image; it prints the map. ChoralRoot: off-grid targets land exactly, one entry per group prints the trace line a
  panel turn prints; for every core every knob's target on the boot screen is an entry or -1, the hidden entries pass
  the same checks. ChoralRoot: the view's KNOB3 follows the perform mode. Felucca / Melodee: param_format's texts, the
  HOME knob and engine entries follow the selected part and its engine.
- `tier2_test` (skipped unless built with `-DFM1_TIER2_SLOTS=79`): the Tier 2 slots on the plugin's processor: a
  host write reaches the firmware and is not echoed, a
  panel turn reaches the host (coalesced, inside a gesture), a state restore and a preset load report every slot
  without gestures, the perform mode picked on the panel; after a switch to Felucca its map on the same slots,
  ALGORITHM on the panel relabelling the engine slots (with the engine's range).
- `knob_test`: the eight knob parameters: their fixed names, order and host info (continuous, default steps, never
  parameterInfoChanged); ChoralRoot's view (the functions in the value texts, a host write of Knob 1 moves the
  voicing, not echoed); **the turn rule** for all seven knobs on the view and in the KEY, PERF and FX layers
  (`Device::enc(role, +2)` reports on that knob first, inside a gesture; a relative one is nudged and springs back;
  any other knob reports only if its function's value changed, without a gesture; nothing else reports), Knob 4 on
  the view explicitly; PRESETS on the view (only Knob Presets touched; Knob 4 follows its send untouched) and in the
  KEY layer (the sends change, no knob shows one: only Knob Presets reports); **page switches** clicked on the panel
  (the view -> EDIT -> HOME, PERF open / mode changed from the panel and from the host / closed, FX, KEY, Options
  open / closed, Felucca HOME -> ENV -> HOME): every knob's host value is its new function's before anything is
  turned, the first turn then reports a step from it (EDIT: Level 98 -> 99), a stale host write within 150 ms of the
  switch is dropped, the switch asks a VST3 host to re-read the values and a turn alone does not; the PERF layer and the perform mode
  switched from the host; a panel turn; the FX layer; Options (SELECT relative: 0.5 -> 0.75 is 6 detents at the
  device, it springs back to 0.5 with no detents; a row with no named entry: the hidden Option), the editor;
  Felucca's HOME, ENV, ENV DEST, SLICER (a hidden cell), EDIT 1, GLO and FX held; Melodee's HOME.
- `plugin_test`: the plugin's processor, headless: MIDI notes press the keys and sound at 48 and 44.1 kHz, the panel
  parameters reach the HAL, MIDI out is well formed, the flash survives the state round trip, presets save / reset /
  load / rename / export / import / delete with backups, the installed bundles resolve their cores folder (all three
  cores load from it), the parameter list (49: the knobs, the buttons, the keys; 128 with the opt-in slots), and the
  firmware switch: ChoralRoot -> Felucca -> Melodee -> ChoralRoot relabels the buttons, follows each core's screen on
  the knobs (and rebinds the opt-in Tier 2 slots), plays a note on each, and comes back to ChoralRoot's working flash byte for
  byte.
  After the build, `auval -v aumu Fm1v Qx7u` validates the installed AU.
- `panel_render_test`: the panel, headless (no window: painted offscreen into images). It writes the pictures
  `build/panel/<theme>.png` for every preset and `custom-Test-Teal.png` (904 x 566 at scale 2, key D4 held from the
  panel), `Emulator-released.png`, `lcd.png` (the LCD alone), `lcd-1x.png`, `lcd-2x.png`, `big-lcd.png` and
  `editor.png` (the whole editor), and checks: the LCD on the panel at 1x, 2x and in the big view (software and
  native renderers), sampled back to 240 x 240, equals the device's framebuffer (RGB565 to RGB888) within 1 per
  channel; D4's LED pixel is the lit colour while `panelKey` holds it and follows the device after the release, and
  its `key_09` parameter never moves; a panel button and MASTER reach the HAL; every preset keeps the contrast floors
  (the table is printed); a custom theme saves, lists, becomes the default for a new instance, and a state naming it
  restores its colours into the editor after its file is deleted; a host write of Knob 1 (Voicing) to 0.25 and to
  0.75 turns KNOB1's pointer by the firmware values' distance on the 270 degree sweep (`knob1-025.png`,
  `knob1-075.png`: the knob's pixels differ), the panel's own turn moves it too, and the relative PRESETS turns 15
  degrees per detent the host gives it. And one picture per other core,
  `build/panel/core-felucca.png` and `core-melodee.png` (1.5 s after the power-on, D4 held), its LCD checked against
  its framebuffer.

**The reference emulator.** `replay_test` compares against the submodule's own headless emulator,
`cores/ChoralRootFM1/build/host/emu --headless --script ... --wav ...`. The build target `reference_emu` builds it
with the submodule's `tools/emu/build.sh` when it is missing (it needs SDL2: `brew install sdl2`; it writes only to
the submodule's gitignored `build/`), and the test runs it to make `build/reference/*.wav` when they are missing or
stale. Without SDL2, configure with `-DFM1_REFERENCE_TESTS=OFF` (or it is turned off with a warning) and
`replay_test` is not built.

Requirements: CMake 3.22+, clang, network access on the first configure (JUCE 8.0.15 via FetchContent), a C++17 standard library (macOS only for now: the core uses `os/lock.h`, the
loader `dlopen` and `<uuid/uuid.h>`), SDL2 from Homebrew for the reference emulator, and a `python3` with
Pillow and fontTools for the submodules' header generators (each repo's `tools/build.py` `generate()`; CMake picks the
first `python3` on `PATH` that has both, or pass `-DCR_PYTHON=/path/to/python3`). The generated headers go to each
submodule's `build/gen` (Felucca's generate step also writes `build/genwav`), which those repos gitignore, so the
submodules stay clean. Felucca's sample tables are generated from its `assets/samples-cc0/` (in its repo: no
download).

## Licensing

GPL-3.0-only (see [LICENSE](LICENSE)), as required by the firmware it builds.

The firmware is the work of several projects, each included as an unmodified git submodule:

- **Felucca** (`cores/Felucca`): GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
  Instruments. The firmware ChoralRoot and Melodee are forked from. Its third-party parts (Inter Tight, OFL 1.1; the
  Versilian Studios CC0 instrument samples, which the Felucca core contains; DaisySP and Rings, MIT; msfa,
  Apache-2.0; the Fukiai icons, MIT) are listed in [cores/Felucca/LICENSING.md](cores/Felucca/LICENSING.md).
- **Melodee** (`cores/melodee`): GPL-3.0-only, Felucca's copyright plus modifications Copyright (C) 2026 Kerem Kilic
  (Ellic Studio). Its FM6 and CZ-1 engines (MSFA / Dexed, Apache-2.0 / GPL-3.0-or-later; MAME's uPD933 model,
  BSD-3-Clause; the Casio CZ-1 factory tones as sound data) and the rest are listed in
  [cores/melodee/LICENSING.md](cores/melodee/LICENSING.md).
- **ChoralRoot FM-1** (`cores/ChoralRootFM1`, Quixotic7), which carries Felucca's code and Melodee's FM6 and CZ-1
  engines. The full table of what comes from where is in
  [cores/ChoralRootFM1/LICENSING.md](cores/ChoralRootFM1/LICENSING.md).

The glue in `core-glue/` (this repo's, GPL-3.0-only) compiles those sources as they are.

This project is independent of and unaffiliated with M-VAVE.
