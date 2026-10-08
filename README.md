# FM1VST

The FM-1 emulator from [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) as a plugin,
planned as VST3, AU and Standalone. The emulator compiles the firmware's own C source against a host
HAL, so the plugin plays exactly what the device plays. The plan, the architecture and the phases are
in [FM1-VST-PLAN.md](FM1-VST-PLAN.md).

Status: phase 2. The ChoralRoot firmware builds as a loadable core module
(`build/cores/choralroot.fm1core`) behind a versioned C ABI, a JUCE-free engine loads it, runs its device clock and
resamples its output to the host rate, and a JUCE plugin (AU, VST3, Standalone) plays it with the firmware's own
parameters (77, absolute and bidirectional: the Roto-Control map, plan 4.5) and the panel's 42 controls as host
parameters, MIDI in and out, the flash image as its state, and presets per firmware. The panel GUI (phase 3) is
next.

## Layout

| path | what |
|---|---|
| `core-api/fm1core.h` | the core ABI (`FM1CORE_ABI 3`): the descriptor `fm1core_t` (id, name, version, source, flash size, button labels, the parameter map `fm1param_t` and `param_epoch`, the panel state `emu_hal_t`, every `emu_fw_*` entry point, `shutdown`, `halted`, and the flash image: `flash`, `flash_dirty`, `flash_stage` to boot from bytes, `flash_sync`) and the one symbol a module exports, `fm1core_get`. The threading contract is in its header comment. |
| `core-glue/choralroot/` | the ChoralRoot core: `core_choralroot.c` (the submodule's `tools/emu/emu_fw.c`, unmodified, with `exit` / `atexit` redirected so a firmware reboot halts the core instead of the host), `core_choralroot_params.c` (the Tier 2 parameter map, included into the same unit so it reaches the firmware's UI code) and `core_choralroot_desc.c` (the descriptor) |
| `plugin/` | the JUCE plugin: `Processor.{h,cpp}` (`FM1Processor`: the core and its `Device` on the audio thread, parameters and the Tier 2 feedback loop, MIDI, state, presets, the firmware switch; the threading and file layout are in its header comment), `Tier2Parameter.{h,cpp}` (a rebindable host parameter slot) and `Editor.{h,cpp}` (a plain settings bar over JUCE's generic parameter editor; phase 3 replaces it) |
| `engine/` | C++17, no JUCE: `cores.{h,cpp}` (find and load modules, one private copy per instance), `device.{h,cpp}` (the single-thread device clock, input, LEDs, LCD, `render` at any host rate), `resampler.h` (4-point Lagrange) |
| `tests/` | `core_smoke`, `replay_test`, `double_load_test`, `halt_test`, `param_test`, `plugin_test`, `tier2_test`, and `script_runner` (the emulator's script grammar on a `Device`) |

## Cores

A core is the firmware compiled as a module (`*.fm1core`, a Mach-O bundle exporting only `fm1core_get`). The plugin
looks for them in its bundle (`<bundle>/Contents/Resources/cores/`; the build puts `build/cores/*.fm1core` there, and
a test or a dev build falls back to `build/cores/`) and in the user folder
`~/Library/Application Support/fm1emu/cores/` (created when first scanned): dropping a module there installs it (a user
module with the same id as a bundled one replaces it). Because the firmware's state is C globals, every loaded
instance runs its own copy of the module, copied to `~/Library/Caches/fm1emu/instances/<uuid>/` and deleted when the
instance goes away.

## The plugin

`FM1VST` as **AU** (`aumu Fm1v Qx7u`, an instrument with MIDI out), **VST3** (Instrument|Synth) and a **Standalone**
app. The build installs the plugins (ad-hoc signed, with the cores inside) to
`~/Library/Audio/Plug-Ins/VST3/FM1VST.vst3` and `~/Library/Audio/Plug-Ins/Components/FM1VST.component`; the
Standalone stays in `build/FM1VST_artefacts/Standalone/`.

**In Live:** add FM1VST (VST3 or AU) to a MIDI track and play notes 53..79 (F3..G5): they press the FM-1's 27 keys,
so ChoralRoot plays its chords as on the device. The firmware needs under a second to power on after the plugin
is loaded. The track's MIDI output carries what the firmware sends (ChoralRoot by default: the chord stream on channel 1; the
bass on channel 2 once a bass sound is chosen), so it can drive other instruments.

- **Parameters (128):** first 86 Tier 2 slots (`t2_00` .. `t2_85`: the firmware's own parameters, below), then the
  42 Tier 1 panel controls: the 14 buttons (`btn_fx` .. `btn_octup`, momentary: on = held; named with the firmware's
  role and the printed label, e.g. "KEY (SEL)"), the 27 keys (`key_00` .. `key_26`, named by note, "F3" .. "G5"; a
  key is held when its parameter or a MIDI note holds it) and `master` (the MASTER pot, 0..1023, default 724 as the
  emulator powers on). The encoders are not parameters (they are relative; the panel GUI drives them). 128 is Live's
  limit for showing a plugin's parameters without configuring them.
- **The firmware's parameters (Tier 2, plan 4.5).** Absolute values with the firmware's own ranges and value texts
  ("1/8", "120 ms", "Up-down", "-10.0dB"), read from and written to the running firmware, so a motorised controller
  such as the Melbourne Instruments Roto-Control follows them. A host change goes through the firmware's own panel
  code (the screen, the knob row's hot cell, the settings record and the trace behave as for a knob turned on the
  unit); a change on the firmware's side (its knobs, a preset, a MIDI CC, a mode change) moves the host's value, as a
  touch (inside a change gesture). After every power-on (a preset load, a state restore, a flash reset) every value
  is reported once, without gestures, so the controller sweeps to the unit's real state. The values are not stored
  separately in the plugin state: the flash image is the truth. Slots beyond a core's map are "(unused)". The order
  is a Roto-Control's page order (eight knobs a page):

  | page | parameters |
  |---|---|
  | 1, the live page | `K1`..`K4` (the meta knobs "Perf Knob 1..4": whatever KNOB 1..4 of the current perform mode carry, renamed when the mode changes, e.g. "K1 Strum Rate" -> "K1 Arp Division"), Voicing, Tempo, Transpose, Chord Level |
  | 2..5 | the perform parameters of every mode, the ones its engine uses: Strum (Rate, Dir, Range, Hold), Slop (Amount, Rate, Dir, Range, Hold), Arp (Division, Dir, Gate, Swing, Range, Retrig, Hold), Pattern (Type, Div, Gate, Swing, Range, Rotate, Retrig, Hold), Harp (Rate, Dir, Gate, Range, Hold) |
  | 5..8 | chord and global: Perform, Perform Mode, Latch, Key Mode, Key Tonic, Key Scale, Single Notes, Split Point, Play Style, Ext Addition, Secret Chords, Velocity, Bass, Bass Mode, Bass Register, Bass Level, Metronome, Click Level, Time Signature, Loop Length, Loop Quantize, Count-In, Loop Level |
  | 8..10 | FX: FX on, the chord part's sends (Chord Drive / Chorus / Delay / Reverb: the FX amounts), the bass part's sends, the shared buses (Reverb Size / Damp / Type, Chorus Rate / Depth, Delay Time / Feedback / Colour) |

  `param_test` prints the whole map with ranges and the firmware path each entry is written through.
  **`-DFM1_EXPOSE_EDITOR=ON`** (off by default) appends the sound editor's ENV, LFO, MOD and MIX pages for the chord
  and the bass part (30 more, 107 in all); build the plugin with `-DFM1_TIER2_SLOTS=116` (or more) to give them
  slots (that goes past Live's 128). A sound edit is kept only by SAVE on the unit.
- **MIDI in:** with **MIDI notes play keys** on (default), note n on any channel holds key `n - 53 + 12 x Transpose`
  (Transpose: -2..+2 octaves; velocity 0 is a release). Every other message (CCs, program changes, clock, SysEx, and
  notes outside the keys or with the setting off) goes to the firmware's own MIDI in as USB-MIDI packets. A note that
  played a key is not also sent to the firmware (ChoralRoot's CHORD channel is 1, so it would sound twice); the toggle
  "... and go to the firmware's MIDI in" sends it as well.
- **MIDI out:** the firmware's USB-MIDI packets, decoded (SysEx reassembled), at the position in the block where they
  were produced.
- **State:** the firmware's id and version, the settings (transpose, the MIDI toggles, the theme) and the whole 1 MiB
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
  ```

  A `.fm1preset` is one line of JSON (`{"fm1preset":1,"core":"choralroot","version":"0.1","name":"..","date":"..",
  "size":1048576}`), a newline, then the flash image gzip-compressed (`tail -n +2 X.fm1preset | gunzip > flash.bin`).
- **`FM1EMU_HOME`:** when set, it replaces `~/Library/Application Support` (data under `$FM1EMU_HOME/fm1emu/`) and
  `~/Library/Caches` (`$FM1EMU_HOME/Caches/fm1emu/`). The tests set it to a folder in the build.

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

The tests:

- `core_smoke`: the core (static library, through its descriptor) boots, draws and sounds.
- `replay_test`: `tools/emu/scripts/cr_dmaj.txt` and `cr_allsynth.txt` through a `Device` on the loaded module must
  give the reference emulator's samples bit for bit at 44.1 kHz and the same `expect` results; the same run rendered
  at 48 kHz must match the reference resampled by a windowed sinc within a stated tolerance.
- `double_load_test`: the module loaded twice in one process, two scripts interleaved millisecond by millisecond,
  each equal to its single-load run (separate globals); one unloaded while the other plays on.
- `halt_test`: the firmware's two `exit()` paths (the boot guard's UBOOT, SAFE MODE's Flash Data reboot) halt the
  core and leave the host running.
- `param_test`: the ChoralRoot parameter map on a `Device`: every entry set to min, max and its default reads back
  at once and 50 ms later, off-grid targets land exactly, the enum texts are distinct, one entry per group prints the
  trace line a panel turn prints, exactly the entries the settings record holds reach the flash image, the meta
  entries follow the perform mode; it prints the map.
- `tier2_test`: the Tier 2 slots on the plugin's processor: a host write reaches the firmware and is not echoed, a
  panel turn reaches the host (coalesced, inside a gesture), a state restore and a preset load report every slot
  without gestures, a meta slot relabels when the perform mode is picked on the panel.
- `plugin_test`: the plugin's processor, headless: MIDI notes press the keys and sound at 48 and 44.1 kHz, the panel
  parameters reach the HAL, MIDI out is well formed, the flash survives the state round trip, presets save / reset /
  load / rename / export / import / delete with backups, and the installed bundles resolve their cores folder.
  After the build, `auval -v aumu Fm1v Qx7u` validates the installed AU.

**The reference emulator.** `replay_test` compares against the submodule's own headless emulator,
`cores/ChoralRootFM1/build/host/emu --headless --script ... --wav ...`. The build target `reference_emu` builds it
with the submodule's `tools/emu/build.sh` when it is missing (it needs SDL2: `brew install sdl2`; it writes only to
the submodule's gitignored `build/`), and the test runs it to make `build/reference/*.wav` when they are missing or
stale. Without SDL2, configure with `-DFM1_REFERENCE_TESTS=OFF` (or it is turned off with a warning) and
`replay_test` is not built.

Requirements: CMake 3.22+, clang, network access on the first configure (JUCE 8.0.15 via FetchContent), a C++17 standard library (macOS only for now: the core uses `os/lock.h`, the
loader `dlopen` and `<uuid/uuid.h>`), SDL2 from Homebrew for the reference emulator, and a `python3` with
Pillow for the submodule's header generators (`tools/build.py` `generate()`; CMake picks the first
`python3` on `PATH` that has Pillow, or pass `-DCR_PYTHON=/path/to/python3`). The generated headers go to
`cores/ChoralRootFM1/build/gen`, which that repo gitignores, so the submodule stays clean.

## Licensing

GPL-3.0-only (see [LICENSE](LICENSE)), as required by the firmware it builds.

The firmware is the work of several projects: Felucca (Leo Kuroshita, Hügelton Instruments), the
firmware ChoralRoot is forked from; Melodee (Kerem Kilic), whose FM6 and CZ-1 engines ChoralRoot
carries; and ChoralRoot FM-1 (Quixotic7). The full table of what comes from where is in
[cores/ChoralRootFM1/LICENSING.md](cores/ChoralRootFM1/LICENSING.md).

This project is independent of and unaffiliated with M-VAVE.
