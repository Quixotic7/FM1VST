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
(a panel knob turned, a preset loaded, a Live set reopened). The device's encoders are relative and
their meaning changes with the screen, so they cannot be those parameters. Instead the plugin exposes
**the firmware's own parameters**, read from and written to the firmware's state. This is possible
precisely because the core is built from source: the glue sits in the same translation unit as the
firmware and can reach its tables and setters.

**Two tiers of host parameters, per core.**

*Tier 1: the panel (same for every core, 42 parameters).* The 14 buttons and the 27 note keys as
momentary booleans (Roto-Control buttons, automation, or a MIDI-less chord player), and MASTER as an
absolute 0..1023 value. The seven relative encoders are **not** host parameters: they stay reachable
from the plugin's own panel GUI and keyboard map for navigating menus. (A hidden "turn" parameter per
encoder can be added later for automation of menu moves; it is not for the Roto-Control.)

*Tier 2: the firmware's parameters (per core, absolute, bidirectional).* Each core ships a parameter
map, a table of entries:

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
| perform parameters per mode (Strum, Slop, Arp, Pattern, Harp): rate, division, direction, range, gate, swing, retrig, pattern, rotate, amount, hold | `cr_engine.h` `cr_param_t`, `CR_PAR_MIN` / `CR_PAR_MAX`, `cr_set_param` / `cr_get_param` | **29** of 55 (the ones each mode's engine uses: Strum 4, Slop 5, Arp 7, Pattern 8, Harp 5), plus **4** meta parameters "Perf Knob 1..4" (the current mode's `CU_PERF_KNOB` row, relabelled on a mode change) |
| chord and global: voicing, transpose, Single Notes, bass voicing, BPM, Key Mode, scale, sticky / latch | `cr_settings.h` `cr_settings_t` and the engine's setters (`cr_set_sticky`, the tempo setter) | **27**: Voicing, Tempo, Transpose, Chord Level (the live page), then Perform, Perform Mode, Latch, Key Mode / Tonic / Scale, Single Notes, Split Point, Play Style, Ext Addition, Secret Chords, Velocity, Bass, Bass Mode / Register / Level, Metronome, Click Level, Time Signature, Loop Length / Quantize / Count-In / Level |
| FX: the chord part's sends (drive, chorus, delay, reverb), the bass part's, the shared bus parameters | Felucca's `params.c` table `TP` (`param_desc_t`: label, format, min, max, default) and the FX layer's knob row (`cr_ui.c` line 378) | **17**: FX on, 4 chord sends, 4 bass sends, 8 bus parameters |
| sound editor pages (ENV, LFO, MOD, MIX, the engine's eight): chord part and bass part | `cr_pages.c` `CP_PAGES` / `CP_LABEL` over `TP` | **2 x 15 = 30** (ENV, LFO, MOD, MIX without Level), **off by default** (`-DFM1_EXPOSE_EDITOR=ON`), to stay under Live's 128 |
| **total** (built) | | **77** with the editor off (107 on), in 86 host slots: 42 + 86 = 128 |

Target: about 80 Tier 2 parameters for ChoralRoot with the editor off, 42 + 80 = 122 in total.
Order: the perform parameters of the current mode first (so the Roto-Control's first page is the four
knobs the PERF screen shows plus voicing, BPM, transpose and chord level), then FX, then globals,
then the panel booleans.

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
per control in Roto-Setup. The plugin's job is to make the first 16 parameters the ones a player
wants on pages one and two, give them short names that fit the Roto-Control's displays, and keep every
value absolute and current. Confirm page and bank behaviour against the Roto-Setup manual when phase 2
reaches Live; the retailer and forum descriptions disagree on the counts.

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
emulator (click, right-click latches, wheel / drag on knobs), the computer key map from `keymap.c`,
and the big LCD toggle. The plugin window is resizable with the same letterboxed scaling.

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
