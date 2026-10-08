# FM-1 emulator as a standalone repo and a VST / AU plugin: plan

Date: 2026-10-07. Status: proposal, nothing built yet. The ChoralRoot repos are not touched by any
step below; they are consumed as read-only git submodules.

## 1. What exists today (the starting point)

`ChoralRootFM1/tools/emu/` is a **source-level** emulator: it compiles the firmware's own C
(`firmware/src/*`, the DSP through `tests/hostsim.c`) against a Mac HAL (`emu_hal_fw.h`) instead of
the WL82 chip's registers. Three pieces matter for a plugin:

| piece | file | what it gives us |
|---|---|---|
| the boundary | `emu_hooks.h` | `emu_hal_t` (keys, buttons, 7 encoders, MASTER pot, LEDs, the 240x240 RGB565 LCD) and the `emu_fw_*` hooks (init, tick 1 ms, frame 15 ms, idle, audio 128 frames at 44.1 kHz, MIDI in / out, flash options). `emu.c` never sees a firmware symbol. |
| the firmware unit | `emu_fw.c` + `emu_firmware.h` | one translation unit, built with `clang -O2 -w -Ibuild/gen -Ifirmware/src`, needing only the generated headers from `python3 tools/build.py` |
| the single-thread clock | `web/emu_web.c` `run_ms()` | the whole device advanced one millisecond at a time inside an audio callback, deterministic, no locks. This is exactly the shape a plugin's `processBlock` wants. |

The panel drawing (`emu.c` `draw_panel`, geometry from `ChoralRootFM1Designer/index.html`, 904 x 566
units) and the key map (`keymap.c`) are reusable as-is in spirit; they are written against SDL and need
porting to the plugin's graphics layer.

Everything is GPL-3.0-only (Felucca, Melodee, ChoralRoot). JUCE and the VST3 SDK are both available
under GPLv3, so the plugin can be GPL-3.0 with no licence fees. AU and CLAP add no licence constraint.

## 2. The "any M-VAVE firmware" requirement: what is and is not feasible

There are two different meanings, and they are different projects.

**A. Any firmware of the Felucca family, from source** (Felucca, Melodee, Sloop, ChoralRoot, future
forks). These all share the same HAL boundary and the same `tests/hostsim.c` host build, so one plugin
shell can host any of them as a "core". ChoralRoot is the only one with an emulator today, but
Felucca and Melodee have `tests/ui_test.c` (the HAL stubs `emu_hal_fw.h` was modelled on), so a core
for each is a few hundred lines of glue, not a port. **This is feasible and is what this plan builds.**

**B. The stock M-VAVE `.fwsc` binary** (`MVaveOfficial/V15-FM-1.fwsc`, and any closed-source firmware).
This needs a CPU-level emulator of the JieLi WL82 (AC79): its proprietary **pi32v2** dual core, plus
the SPI LCD, the key matrix GPIO, the ADC, I2S / DAC, USB, the SFC flash with its encryption, timers
and the interrupt controller. No public pi32v2 emulator exists (a search for one on 2026-10-07 found
nothing; JieLi only ships a clang 4.0.1 toolchain for it, and kagaimiq's tooling in `jl-uboot-tool`
covers the boot ROM and flash, not execution). Writing one means reverse-engineering the ISA from the
toolchain's output and the SDK, then the peripherals from the HAL headers (`firmware/hal/*.h` document
every register the Felucca family touches, which is a real head start). That is a multi-month research
project with an uncertain end, and it only pays off for firmware nobody has source for.

**Decision proposed:** build the plugin around a small **core ABI** so that a core is a loadable module.
Phase 1 cores are source-built (A). A binary core (B) would sit behind the same ABI later, so nothing in
the plugin needs to change if it ever exists; it is listed as a separate research track (phase 6) and
not on the critical path.

## 3. The new repo

Working name `fm1emu` (rename freely). Top-level layout:

```
fm1emu/
  LICENSE                GPL-3.0-only (required by the firmware)
  CMakeLists.txt         JUCE via FetchContent (or a submodule), each core as a .fm1core dylib (4.3)
  cores/                 git submodules, read-only, pinned commits:
    ChoralRootFM1/         https://github.com/Quixotic7/ChoralRootFM1
    Felucca/               https://github.com/hugelton/Felucca      (phase 4)
    melodee/               https://github.com/keremimo/melodee      (phase 4)
    sloop-fm1/             https://github.com/isod89/sloop-fm1      (phase 4, if it builds on the host)
  core-api/
    fm1core.h            the ABI: emu_hooks.h's emu_hal_t + hooks, versioned, plus core metadata
  core-glue/
    choralroot/          one .c that includes cores/ChoralRootFM1/tools/emu/emu_fw.c, plus the flash
                         file redirection as web/emu_web.c does; CMake runs that repo's tools/build.py
    felucca/, melodee/   emu_fw.c equivalents written here (the firmware unit list + the HAL), since
                         those repos have no tools/emu
  engine/
    device.cpp           the single-thread device clock (run_ms), the 44.1 kHz -> host-rate resampler,
                         input edge handling, MIDI packet conversion, the flash image as plugin state
  panel/
    geometry.h           the designer's rectangles (from emu.c lines 38-70)
    PanelComponent.cpp   the panel, LEDs, knobs, keys, the LCD texture (port of emu.c draw_panel)
  plugin/
    Processor.cpp        JUCE AudioProcessor: parameters, MIDI, processBlock, state
    Editor.cpp           the panel + a small bar (core, flash reset, transpose, detent size)
  standalone/            JUCE Standalone target (replaces build/host/emu for day-to-day use)
  tests/
    replay_test.cpp      plays cores/.../tools/emu/scripts/cr_dmaj.txt through the engine and compares
                         the WAV bit for bit with build/host/emu --headless (the existing ground truth)
```

Generated headers: `tools/build.py` writes to `<repo>/build/gen`, which is gitignored in every
submodule, so running it leaves the submodules clean. If that is still too close for comfort, the
generate step can be copied into `core-glue/` and pointed at an out-of-tree directory (the script's
`OUT` / `GEN` paths are two constants at the top).

## 4. Architecture decisions

### 4.1 Threading: the device clock lives in the audio thread
`processBlock` does what `emu_web.c` does: for every host block, advance the device millisecond by
millisecond (tick, UI frame every 15 ms or idle, the 128-frame audio blocks that fall due) until enough
samples exist, then resample to the host rate. One thread, no CPU lock, deterministic, and the existing
headless scripts become the plugin's regression tests.

Cost: the UI frame runs in the audio thread. The web build already does this in an AudioWorklet and the
README puts a Mac block render at "a few tens of us"; the worst UI frames are measured by `perf.sh`. If
a frame ever shows up in the audio budget, the fallback is `emu.c`'s realtime mode (UI on a timer thread
behind `emu_hal_fw.h`'s lock). Decide after measuring in phase 2, not before.

Latency: at most one internal 128-frame block (2.9 ms at 44.1 kHz) plus the resampler; reported to the
host with `setLatencySamples`.

### 4.2 Sample rate
The firmware is fixed at 44.1 kHz. The engine runs it there and resamples to the host rate (JUCE's
`LagrangeInterpolator` is enough for a first pass; r8brain if quality complaints). At a 44.1 kHz session
the resampler is bypassed and the output equals the device's samples.

### 4.3 Cores are loadable firmware modules in a folder (the firmware browser)
Each firmware is built as its own dynamic library, a **core module** (`choralroot.fm1core`, a dylib
with a small JSON sidecar or an embedded descriptor), and the plugin finds them in two folders:

- bundled: inside the plugin's own bundle (`Contents/Resources/cores/`), so a fresh install already
  has ChoralRoot, Felucca and Melodee;
- user: `~/Library/Application Support/fm1emu/cores/`, so a new or third-party firmware is installed by
  dropping a file there, no rebuild of the plugin.

The plugin scans both at start and when its menu opens, and shows them in a **dropdown on the
settings bar** (name and version from the descriptor): pick one and the instrument switches to that
firmware in place. No file dialog. Switching tears the old core down (its flash image is saved to that
core's own folder first), loads the new one and powers it on; the panel relabels its buttons from the
new core's descriptor, the parameter list (4.5) changes to the new core's map, and the host is told the
parameters changed (`updateHostDisplay` with the parameter-info flag; Live handles this). The plugin
state (4.7) records the core's identifier and version so a Live set reopens on the firmware it was saved
with, and says which one is missing if it is not installed.

Each core keeps its own flash image (`.../fm1emu/<core-id>/flash.bin`), so switching firmwares is like
swapping units, not reflashing one: coming back to ChoralRoot finds its user sounds and loops as left.

**The globals problem, solved by the same mechanism.** The firmware unit is a single C file full of
globals, so a core can exist once per loaded image. Because cores are dylibs, each plugin instance
`dlopen`s **its own copy** of the module (copied to a unique path under the plugin's cache folder;
dyld gives each path its own globals). Two instances of the plugin can run two ChoralRoots, or a
ChoralRoot and a Felucca, in one Live set. The copy is deleted when the instance goes away. The
out-of-process variant (a helper per instance over shared memory, which would also isolate a crashing
third-party firmware from Live) stays as a later option behind the same ABI.

### 4.4 The core ABI
`core-api/fm1core.h` is `emu_hooks.h` turned into a versioned C ABI that a dylib exports as one symbol,
`fm1core_get(uint32_t abi_version)`, returning a descriptor:

```c
typedef struct {
    uint32_t abi_version;
    const char *id, *name, *version;   /* "choralroot", "ChoralRoot", "1.0" */
    const char *source_url;            /* the firmware repo and commit the core was built from */
    uint32_t flash_size;               /* 1 MiB today */
    const char *const *button_names;   /* the firmware's own labels for the 14 buttons (ChoralRoot: FX KEY BASS ..) */
    const fm1param_t *params;          /* the Tier 2 parameter map (4.5) and its count */
    uint32_t nparams;
    emu_hal_t *hal;                    /* the module's own emu_hal (keys, LEDs, LCD) */
    /* the emu_fw_* entry points as function pointers: options, init, tick, frame, idle, audio, midi_in,
     * midi_out_take, dump, plus shutdown (flush the flash, release) */
} fm1core_t;
```

Nothing in the plugin links a firmware symbol directly; everything goes through the descriptor, which
is what makes a core a drop-in file. The glue for a core is tiny: the firmware unit, the parameter map
and the descriptor. A firmware author who wants their fork in the plugin builds one from the
`core-glue/` template and ships the `.fm1core`.

### 4.5 Parameters exposed to the host (the Roto-Control mapping)

The Roto-Control's motorised knobs follow the value of the Live parameter they are mapped to, so a
parameter must be an **absolute value with a range**, and it must move when the firmware changes it
(a panel knob turned, a preset loaded, a Live set reopened). The user's instruction for the mapping:
*map the hardware knobs, so the knobs can always be controlled by the Roto-Control, and their values
change with their function in the firmware.*

**The first page is the FM-1's eight physical knobs**, in panel order: MASTER, SELECT, PRESETS,
ALGORITHM, KNOB1, KNOB2, KNOB3, KNOB4 (`knob_master knob_select knob_presets knob_algo knob_1 ..
knob_4`), named **"Knob Master" "Knob Select" "Knob Presets" "Knob Algo" "Knob 1" .. "Knob 4"**. The names
never change: the Roto-Control binds a mapping by the parameter's name (the first version, "KNOB1:
Voicing" becoming "KNOB1: Level" with the screen, broke its mappings in Live). Each is one host parameter
whose **value is always whatever that physical knob does in the firmware on the screen or layer showing
now**, and its **value text says what that is**, "Function: value": on ChoralRoot's view Knob 1 reads
"Voicing: -1" and Knob Select "Tempo: 147 BPM"; with the PERF layer open Knob 1..4 are the current perform
mode's row ("Strum Rate: 126 ms" .. "Strum Hold: Off"; Arpeggiate: Division, Dir, Gate, Swing), with the
FX layer the effect's row (Reverb: Size, Damp, Type, the amount), with KEY the tonic, scale, transpose
and Single Notes, in the sound editor the cells of the lane on screen, in Options the row's value; on
Felucca's HOME the engine's four knobs, on its ENV page Attack .. Release, with FX held its macros. MASTER
is the pot (absolute 0..1023) everywhere.

- **The core says what each knob is.** ABI 4 adds `int32_t (*knob_target)(int role)` to `fm1core_t`:
  for a knob role (`EMU_E_SELECT` .. `EMU_E_K4`, `EMU_E_MASTER`) the index of the map entry that knob
  turns now, or -1 when it has no value there. The glue computes it by reading the firmware's own knob
  dispatch without turning (ChoralRoot: `cu_knob`'s tests in order over `cr_ui.c`'s layer, Options, editor
  and view state; Felucca / Melodee: `ui_input`'s, over HOME, the page, the held layer, the menu and
  dialogs), and `param_epoch()` moves whenever any knob's target changes: it is recomputed as a signature
  of all eight at every call, so no screen, layer, page, mode, part or popup change can be missed. The
  per-screen tables are in the headers of `core-glue/*/core_*_params.c`.
- **Knob targets that are not named parameters.** An entry flagged `FM1P_HIDDEN` exists so a knob can
  target it but gets no host slot of its own: ChoralRoot's sound editor pages (the editor entries,
  visible only with `FM1_EXPOSE_EDITOR`), the FX layer's effect picker, an Options row, and the cells no
  named entry covers (an engine's own and deep pages, MIX 2: "Edit Knob 1..4", rewritten from the cell on
  screen as Felucca's E1..E8 are rewritten from the engine); Felucca / Melodee's FX macros, T1..T4 levels
  and page cells ("Knob 1..4": SLICER, the operator pages, GLOBAL's CLK, the FM6 / CZ-1 pages).
- **Fixed host info: continuous, quantised inside.** A knob parameter is continuous 0..1 with the
  default number of steps and default 0.5 whatever it targets; the value maps linearly onto the current
  target's range (v = min + round(x (max - min))). So nothing a host caches (title, short title, step
  count, default: what JUCE's VST3 wrapper compares in `updateParameterInfo`, what an AU host reads as
  parameter info) ever changes, and the plugin never announces `parameterInfoChanged` for a knob (Live
  rescans a plugin's parameters on every such call; with a discrete target per screen it would have been
  needed on every screen change). Only the opt-in Tier 2 meta slots and a core switch (the buttons'
  names) still announce it.
- **The plugin follows.** The epoch is checked at every UI frame's read-back (every 15 ms of device
  time), so a screen change is seen within a frame whichever thread caused it (a panel click, a host
  write, a MIDI message). It re-reads the seven targets, retargets the knob parameters whose target
  (entry, range, name) changed and pushes their values without a gesture. A value pushed without a
  gesture reaches a VST3 host as `performEdit` outside `beginEdit` / `endEdit`, which Live ignores (it
  kept Knob 1 at the voicing's value after the editor opened: the next Roto-Control turn wrote that stale
  value onto the editor's Level), so every drain that pushed one also calls
  `IComponentHandler::restartComponent(kParamValuesChanged)` (`plugin/HostRefresh.cpp`, through JUCE's
  `VST3ClientExtensions::setIComponentHandler`): the host reads back what JUCE's edit controller now
  holds, as after a preset load, without a touch. The Roto-Control's motors move to the new positions and
  its displays show the new texts. For 150 ms of device time after a retarget, host writes to that knob
  are dropped (the motor still sat on the old target's value) and the knob is told its value again.
- **Only the knobs report, and only the one turned is a touch.** Live's Configure mode collects every
  parameter the plugin reports as changed, so with the Tier 2 slots off (the default) only the eight knobs
  can ever report. A knob turned on the unit (the panel GUI, its keys, a fine turn: the device's detent
  counters, minus the host's own relative detents) reports on that knob, inside a gesture, first in its
  batch. A knob whose target's value changed for another reason (PRESETS loads a sound whose sends
  differ, and KNOB4 shows the chord reverb send; a MIDI CC) is updated without a gesture; a knob whose
  target did not change reports nothing; a value the host already has is not sent again. A relative knob
  turned on the unit is *nudged*: the host is told 0.5 + detents / 24 inside a gesture (its motor follows,
  Configure collects it), then it springs back as below. (Before this, the named slots reported with a
  gesture on every firmware change: one PRESETS turn put four sends and KNOB4 into Configure, which is why
  KNOB4 later seemed to "register nothing": it was already there, collected by the PRESETS turn.)
- **Never a dead end: a knob with no value turns.** Where the knob is a navigation knob on the current
  screen (SELECT scrolling Options or the editor's lanes, PRESETS and ALGORITHM browsing sounds, an
  empty column, an action), its parameter becomes a relative control (value text "turn"), at
  0.5: a host change is converted into detents (the change of the normalised value x 24 per full travel,
  rounded, the remainder kept; a setting) sent to the device's encoder as a panel turn, and after 400 ms
  of device time without a host change the parameter is re-centred to 0.5 (reported without a gesture;
  a re-centre never produces detents). ChoralRoot's sound lists (PRESETS: the chord sound, ALGORITHM:
  the bass sound) stay relative: their length follows the engine and the user's presets.
- **Stepped encoders (a setting: "Encoders: Absolute / Stepped", "Steps" 8..64, default 24; in the
  state).** The Roto-Control can make a knob click in 24 steps, which feels like the FM-1's detented
  encoders, but each click then moves the parameter 1/24 of its travel: absolute, one click on Tempo
  (20..300) is about 12 BPM, and a short range does nothing for several clicks, then jumps. In Stepped
  mode the seven encoder parameters (not Knob Master: it is the pot, absolute always) take a host write
  as clicks: the change against the host's previous value (what it last wrote or was told: where the
  motor sits), in steps, becomes that many detents of `Device::enc`, the panel's path, for every
  target, bound or relative, menus included, exactly as the real encoder. The quantisation of the
  controller is unknown, so a value on the k/(N-1) grid (0 and 1 both steps) counts as that grid's index
  against the previous value's index on the same grid, likewise on the k/N grid, and a value on neither
  (a controller adding 1/N where it sits) counts its distance in steps (`KnobParameter::stepDelta`); a
  value echoed back quantised to either grid is therefore 0 clicks. The firmware's value is then pushed
  to the host as any value change (inside a gesture only for a turn on the unit; else without one, with
  the VST3 re-read), always at its exact place, so the next click is +-1 from where the motor was put
  and a long range never runs out of travel; a relative target re-centres after 400 ms as before (no
  clicks). The settle window still drops writes (no detents) and the knob is told its value again, from
  which the next click counts. Switching the mode drops pending writes and turns nothing.

The named parameters (Tier 2 below) are an **opt-in**, `-DFM1_TIER2_SLOTS=79` (default 0): with them a
specific parameter can be mapped directly whatever the screen shows, at the price that every firmware
change of one reports on its slot (Configure collects them too). The default list is the eight knobs and
the panel's buttons and keys (Tier 1): 8 + 41 = 49; with the slots 8 + 79 + 41 = 128, Live's limit.

*The panel follows the knobs.* The GUI snapshot carries, per knob, whether it is bound, its target's
value as a place 0..1 in its range, the device's detent count and the function's name: a bound knob's
pointer sits at its value on MASTER's 270 degree sweep and moves whenever the value does (host,
Roto-Control, panel, firmware), with the function as a small caption under the printed name (KNOB1 /
VOICING); a relative knob's pointer turns 15 degrees per detent from any source.

*Tier 1: the panel (same for every core, 41 parameters).* The 14 buttons and the 27 note keys as
booleans (Roto-Control buttons, automation, or a MIDI-less chord player). A key's value is its held
state. A button's value goes through the **Button params** setting (state; the editor bar), because a
controller button mapped in a host is usually a toggle (1, and it stays 1 until the next press), which
read as "held" is a button held forever (OCT acts on release, so every second press; both OCTs = panic;
a layer button locks its layer at 300 ms): **Tap** (default) turns every rising edge into one press of
90 ms of device time (a per-button countdown on the audio thread, the device's between hook, so it is
exact in device ms; well over the firmware's 9 ms debounce and its 15 ms frame, under the 300 ms hold;
a rise during a tap queues one more after a 30 ms release; the length is a hidden state setting), the
falling edge nothing; **Hold** is value = held (momentary buttons: layer locks, long presses);
**Toggle-hold** toggles held on each rising edge. The panel's mouse and keys are real presses in every
mode.

*Tier 2: the firmware's parameters by name (per core, absolute, bidirectional).* Each core ships a
parameter map, a table of entries (`core-api/fm1core.h` `fm1param_t`; abridged):

```c
typedef struct {
    const char *name;            /* "Strum Rate", "Arp Division", "Chord Level" */
    int32_t min, max, def;       /* the firmware's own range and default */
    const char *const *names;    /* enum value names ("1/8", "UP", ..) or NULL */
    const char *unit;            /* "ms", "%", "dB", "" */
    int32_t (*get)(void);        /* read the firmware's current value (audio thread) */
    void (*set)(int32_t);        /* write it through the firmware's own path (audio thread) */
} fm1param_t;
```

For ChoralRoot the map is built from tables that already exist in the firmware, not invented:

| group | source in the firmware | count |
|---|---|---|
| perform parameters per mode (Strum, Slop, Arp, Pattern, Harp): rate, division, direction, range, gate, swing, retrig, pattern, rotate, amount, hold | `cr_engine.h` `cr_param_t`, `CR_PAR_MIN` / `CR_PAR_MAX`, `cr_set_param` / `cr_get_param` | **29** of 55 (the ones each mode's engine uses: Strum 4, Slop 5, Arp 7, Pattern 8, Harp 5); the current mode's `CU_PERF_KNOB` row is what the knobs carry in the PERF layer |
| chord and global: voicing, transpose, Single Notes, bass voicing, BPM, Key Mode, scale, sticky / latch | `cr_settings.h` `cr_settings_t` and the engine's setters (`cr_set_sticky`, the tempo setter) | **27**: Voicing, Tempo, Transpose, Chord Level, then Perform, Perform Mode, Latch, Key Mode / Tonic / Scale, Single Notes, Split Point, Play Style, Ext Addition, Secret Chords, Velocity, Bass, Bass Mode / Register / Level, Metronome, Click Level, Time Signature, Loop Length / Quantize / Count-In / Level |
| FX: the chord part's sends (drive, chorus, delay, reverb), the bass part's, the shared bus parameters | Felucca's `params.c` table `TP` (`param_desc_t`: label, format, min, max, default) and the FX layer's knob row (`cr_ui.c` line 378) | **17**: FX on, 4 chord sends, 4 bass sends, 8 bus parameters |
| sound editor pages (ENV, LFO, MOD, MIX, the engine's eight): chord part and bass part | `cr_pages.c` `CP_PAGES` / `CP_LABEL` over `TP` | **2 x 15 = 30** (ENV, LFO, MOD, MIX without Level), knob targets in the editor (`FM1P_HIDDEN`); slots of their own only with `-DFM1_EXPOSE_EDITOR=ON` (to stay under Live's 128) |
| knob targets only (`FM1P_HIDDEN`) | the editor's other cells (`cr_edit.c` `ce_view` / `ce_param`), Options (`opt_get` / `opt_set`), the FX picker | **6**: Edit Knob 1..4 (rewritten per cell), Option (rewritten per row), FX Effect |
| **total** (built) | | **73** visible (103 with the editor), 109 in the map; with the opt-in, 79 host slots: 8 knobs + 79 + 41 = 128 |

Order: the eight knobs, then the voicing, BPM, transpose and chord level, the perform parameters,
globals, FX, then the panel booleans. Felucca and Melodee: 73 visible (the part's level and sends, ENV,
the engine's E1..E8, LFO, VOICE, ARP, SCL / CHORD, Part, Tempo, the buses, PATTERN, MOD 1 and 2), plus
12 hidden knob targets (the FX macros, T1..T4 levels, Knob 1..4).

**Writing: always through the firmware's own path.** A host change of "Strum Rate" must leave the
screen, the knob row's hot cell, the settings record (what SAVE and the flash persist) and the trace
exactly as a panel turn would. So each `set` prefers the UI-level function the panel uses
(`cu_param_turn` and its relatives in `cr_ui.c`), driven with the number of steps from the current value
to the target; the engine setter is used directly only where no UI path exists (and then the glue
marks the UI's mirror dirty). The same rule for Felucca and Melodee cores: `editor.c`'s parameter
setters, not the raw track fields.

**Reading and feedback.** Once per UI frame (15 ms of device time) the engine compares every mapped
`get()` with the value it last reported to the host and pushes the differences with
`setValueNotifyingHost`, throttled to one update per parameter per 30 ms. Live then moves the
Roto-Control's motor. A change that originated from the host is not echoed back (the engine remembers
the value it wrote). Preset loads and flash restores therefore sweep every knob to its new position,
which is the behaviour wanted.

**Normalisation.** Host value 0..1 maps linearly onto `min..max` with the firmware's step; enum
parameters are discrete (`AudioParameterChoice`) so the Roto-Control's haptics can click per value;
the display string uses the firmware's own formatter (`param_format`, the knob row's text), so Live
and the Roto-Control's screens show "1/8", "UP", "120 ms" as the LCD does.

**The Roto-Control side.** Its PLUGIN mode learns Live device parameters per plugin (pages of eight
knobs and eight buttons, stored on the unit), reads their names and values, and sets the haptic feel
per control in Roto-Setup. The plugin's first page is the eight physical knobs, so learning that page
once gives the FM-1's own panel on the Roto-Control on every screen (the names and motors follow);
the named parameters on the following pages are for mapping one parameter for good. A relabel is a
parameterInfoChanged: confirm against the Roto-Setup manual how quickly the unit re-reads names (the
values and motors follow regardless).

**Automation caveat.** When Live is recording automation on an armed parameter, plugin-originated
updates (a panel knob turned on the plugin's GUI) are written as automation, as with any plugin. That
is correct behaviour, but the feedback loop must never fire during `setParameter` itself, or Live
sees a parameter fight.

### 4.6 MIDI
- **In:** notes 53..79 press the keys (note - 53 = key index, as on the device), with a transpose
  setting; every incoming packet is also handed to the core's `emu_fw_midi_in`, so the firmware's own
  MIDI handling works (ChoralRoot's CHORD / BASS channels, CCs, and MIDI clock in: Options > MIDI
  Clock = In follows Live's clock, which the plugin generates from the host transport).
- **Out:** the core's `emu_fw_midi_out_take` packets become the plugin's MIDI output, so Live can route
  ChoralRoot's chord and bass notes to other instruments. Live 12 passes MIDI out of VST3 / AU effects
  and instruments on the track's MIDI output.

### 4.7 State, the flash, and presets per firmware
The core's 1 MiB flash image (settings, user sounds, loops, FM6 and CZ banks) **is** the plugin state:
`getStateInformation` stores it (zlib-compressed; it is mostly 0xFF) together with the core id and
version, the theme and the plugin's own settings, so a Live set reopens with the instrument exactly as
left. ChoralRoot's backup JSON format can be imported and exported through `cr_backup.c`.

**Everything a firmware owns lives in that firmware's folder**, so switching cores never mixes them up:

```
~/Library/Application Support/fm1emu/
  cores/                      user-installed .fm1core modules (4.3)
  choralroot/
    flash.bin                 the working flash: what a new instance of this core starts from
    presets/
      Warm Pads.fm1preset     a named snapshot of the whole flash (gz), plus a JSON header
      Live Set A.fm1preset
    backups/                  dated copies made before a Reset flash or a preset load
  felucca/
    flash.bin
    presets/ ...
  themes/                     custom colour themes (4.8; themes are not per firmware)
```

A **preset** is a snapshot of the whole flash: all user sounds, loops, FM6 / CZ banks and settings at
once (the firmware's own SAVE keeps writing single sounds inside the flash as on the device; a preset
is the plugin's layer above that, "the whole unit as it is now"). The settings bar has a **preset
dropdown next to the firmware dropdown**, listing the current core's `presets/` folder: pick one to
load, Save / Save as / Rename / Delete, no file dialog. A preset file carries the core id and version
it was made with, so the list only shows presets that fit the loaded firmware, and the plugin can
warn before loading one from an older firmware version (the flash formats are versioned on the
firmware side; `cr_settings.c` migrates older records, so this is a warning, not a refusal).

Presets also appear to the host as the plugin's program list (VST3 / AU presets), so Live's own
preset browser and the Roto-Control's preset buttons, if it has them, can step through them.

Import / Export buttons move single `.fm1preset` files in and out for sharing, and Export can also
write ChoralRoot's backup JSON so a preset made in the plugin can be restored onto a real FM-1 with
the installer page (`tools/fm1_install.py --restore`), and a backup from a real unit can be imported
as a preset. That round trip is a phase 2 check.

### 4.8 GUI
A native JUCE port of `emu.c`'s panel: the designer geometry, the LEDs lit / dim from `emu_hal.led`,
the LCD as a 240x240 texture refreshed on a 60 Hz timer when `lcd_writes` moves, mouse as in the
emulator (click, right-click latches, wheel / drag on knobs), the computer key map from `keymap.c`
(plus a fallback row `C V B N M ,` for F5 .. F10, which hosts and macOS often keep, printed as "F5/C";
a held key pressed once, released on its key-up or, when the host swallows that, by a 30 Hz check of
the physical key), and the big LCD toggle. The plugin window is resizable with the same letterboxed scaling.

Considered and rejected: a JUCE 8 WebView reusing `tools/emu/web/index.html`. It saves drawing code but
pushes a framebuffer through a JS bridge at 60 fps and adds a second runtime to debug.

**Colour themes.** The real FM-1 ships in several colours, and the emulator draws one (dark grey:
`emu.c` lines 300-310, `C_PLATE`, `C_BED`, `C_EDGE`, `C_CAP`, `C_LEDOFF`, `C_LABEL`, plus the knob cap
`0x35353C` and the pressed key `0x45454E`). The port replaces those constants with a theme:

```c
typedef struct {
    uint32_t base;         /* the body plate (bed and edge lines are derived: darker / lighter) */
    uint32_t membrane;     /* the silicone keys and buttons; pressed = lightened, LED-off = darkened */
    uint32_t knob;         /* the encoder and pot caps; the pointer line is derived for contrast */
    uint32_t label;        /* optional: printed labels; auto-contrast against base when unset */
} fm1theme_t;
```

Three colours are what the user picks (base, membrane, knobs); everything else is derived so that a
theme always stays legible: bed and edge from the base, pressed and LED-off from the membrane, the
pointer and labels by contrast. Advanced overrides for the derived colours live in the theme file but
not in the picker.

- **Presets:** sampled from M-VAVE's product photos in `reference/MVaveOfficialColors/` and kept in
  `themes/presets.json` (starting values; phase 3 tunes each by eye against its photo). The knobs are
  black on every real unit; the membrane colour is what varies, and on some units the recessed key bed
  is a darker shade of it, so the theme carries an optional `bed`:

  | preset | base | membrane | bed | knob |
  |---|---|---|---|---|
  | Black | #3A3A3C | #2E2E30 | #2C2C2C | #2A2A2A |
  | Black/Green | #4C4B4E | #6FCCBD | #65CDBC | #424344 |
  | Cool Gray | #E3D9CF | #3A3C3D | #1A1E1F | #1C1C1C |
  | Orange | #C4734F | #92594D | #834C41 | #171717 |
  | Purple | #C19AD5 | #7D72B5 | #443B80 | #1C1C1E |
  | White/Blue | #F2ECE7 | #587592 | #193046 | #181918 |
  | Emulator | #1C1C20 | #2B2B31 | #141417 | #35353C |

  Label colour flips with the base: white labels on the dark bodies, dark on Cool Gray and White/Blue,
  as the photos show.
- **Custom:** a colour picker per group in the plugin's settings bar; custom themes are saved as JSON in
  `~/Library/Application Support/fm1emu/themes/<name>.json` and listed with the presets.
- **State:** the theme name and its three colours are stored in the plugin state next to the flash
  image, so a Live set reopens with the instrument looking as it was saved, even if that custom theme
  file is gone. A global default theme for new instances is a user preference.
- The LEDs' lit colours (white, red, orange, green) stay fixed: they are the LEDs, not the paint.

### 4.9 Formats and targets
VST3 + AU + Standalone through JUCE's CMake API; CLAP via `clap-juce-extensions` once the rest works.
macOS arm64 first (this machine; Live 12 Suite and REAPER are installed for testing). Linux and Windows
need only the glue's few POSIX / `os/lock.h` bits replaced, which the web build's `compat/` already shows
how to stub.

## 5. Phases

Each phase ends with something runnable and a check that proves it.

**Phase 0: bootstrap (half a day).** Create the repo, add ChoralRootFM1 as a submodule at its current
commit, CMake that runs the generate step and builds the firmware unit as a static library, LICENSE,
README with the licensing summary (what is Felucca's, Melodee's, ChoralRoot's). Check: `cmake --build`
produces `libcore_choralroot.a`.

**Phase 1: engine + replay test (2-3 days).** `core-api` with the descriptor, the ChoralRoot glue
built as a `.fm1core` dylib exporting `fm1core_get` (the static library stays for the tests), a core
loader (`engine/cores.cpp`: scan the two folders, load a module, the per-instance copy), `engine/device.cpp`
with the single-thread clock and the resampler, and `tests/replay_test` that runs `cr_dmaj.txt` through
a loaded module and compares with `build/host/emu --headless --wav`. Check: bit-for-bit equal WAV at
44.1 kHz; the 48 kHz output null-tests against a resampled reference within a stated tolerance; a test
loads the ChoralRoot module twice and drives the two with different scripts, proving separate globals.

**Phase 2: plugin without a custom GUI (3-5 days).** JUCE processor with the Tier 1 panel parameters
and the ChoralRoot Tier 2 parameter map (perform, chord, FX, globals), the feedback loop, MIDI in /
out, state, generic editor. Load in Live 12, map the Roto-Control's first page, play chords from a MIDI
clip. Check: Live set reopens with a user sound intact; a preset saved from the dropdown, the flash
reset, and the preset loaded back gives the same user sounds (and a backup JSON exported from it
restores onto a real FM-1); turning "Strum Rate" on the Roto-Control
changes the value the LCD shows and the trace prints; turning the plugin's own knob moves the
Roto-Control's motor; loading a preset sweeps the knobs; no dropouts at 64-sample buffers over ten
minutes. A headless test drives each mapped parameter min to max and back and checks `get()` equals
what was set and that the firmware's settings record changed accordingly.

**Phase 3: the panel GUI (3-5 days).** Port `draw_panel`, LEDs, LCD, mouse and keyboard, with the
colours routed through `fm1theme_t`; the theme presets, the three-colour picker, custom theme files, and
the theme in the plugin state. Check: with the "Emulator" theme the panel matches the emulator window
pixel-for-pixel at integer scales (screenshot diff against `build/emu/test/*.ppm` for the LCD region);
each preset renders without any derived colour falling below a contrast floor (an automated check over
the preset list); a custom theme survives save, reopen and a deleted theme file.

**Phase 4: more cores (1-2 days each).** Felucca, then Melodee: a glue `emu_fw.c` per repo built from
`tests/ui_test.c`'s stubs and `tests/hostsim.c`. Core selection in the plugin. Sloop if it builds on the
host. Check: each core boots to its home screen in the Standalone and passes a key-press sound test.

**Phase 5: robustness and release.** Per-instance core copies for multiple instances (4.3), the user
cores folder with a third-party core test (a renamed copy of the ChoralRoot module dropped in and
picked from the dropdown), CLAP, notarised builds, a Releases page.

**Phase 6 (research, parallel, no deadline): a binary core.** Feasibility study for a pi32v2 CPU
emulator: disassemble `V15-FM-1.fwsc` with kagaimiq's Ghidra processor module, count the instruction
forms used, estimate the peripheral surface from `firmware/hal/*.h`. Outcome is a go / no-go document,
not code. The core ABI already has a slot for it.

## 6. Risks and open questions

- **UI frame in the audio thread** (4.1): measured, not assumed; fallback exists.
- **The parameter map is firmware-specific maintenance** (4.5): every core needs one, and a ChoralRoot
  commit that renames a setter or a settings field breaks the glue at compile time (good: loud, not
  silent). Keep the map in one file per core with the firmware commit it was written against.
- **Setting through the UI path** (4.5): some ChoralRoot values may only be reachable through a layer
  that must be open on screen (the FX layer's bus parameters, for instance). Where that is so, the
  glue uses the engine or settings setter directly and refreshes the UI mirror; this is found per
  parameter in phase 2's headless test, not guessed.
- **Live's parameter count**: Live exposes a VST3's parameters, but its device panel and the
  Roto-Control's learn list get unwieldy past about 128; the editor tier stays off by default.
- **Dylib cores** (4.3): the per-instance copy trick must be proven early (phase 1 builds the
  ChoralRoot core as a dylib and loads it twice in a test); if dyld ever dedups copies, the fallback is
  the out-of-process helper. Code signing: a plugin that copies and loads dylibs at runtime must sign
  the copies with the same identity or the hardened runtime refuses them; the notarised build (phase 5)
  settles this, and the development build runs unsigned.
- **Switching cores in a running host**: changing the parameter list while Live has automation or a
  Roto-Control page mapped to the old one is the kind of thing hosts handle unevenly; the state stores
  the core so it only happens when the user asks.
- **Generate step**: Pillow needs `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib`; CMake sets it.
- **Submodule hygiene**: everything the plugin needs from ChoralRoot is reached through
  `tools/emu/emu_fw.c` and `tools/build.py`. If a later ChoralRoot commit changes `emu_hooks.h`, the
  core ABI version bumps and the glue follows; nothing is patched inside the submodule.
- **Roto-Control page counts**: sources disagree (8 pages vs 8 banks); verify on the unit.

## 7. Who does what

Per the working arrangement: this plan and the phase briefs are Claude's; the code of each phase is
written by Opus subagents from a brief naming the files, the ABI and the acceptance check; each phase
is verified by running its check (and the Standalone, headless or in the background, never in front).
