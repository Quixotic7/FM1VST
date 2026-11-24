# .rfm1 files: your firmware for host apps

An `.rfm1` file is an FM-1 firmware built to run inside an app (RemoteFM1 for iOS first) instead of on the FM-1. It is
the same build FM1VST makes for the plugin, your firmware's sources and FM1VST's glue for it, compiled to
WebAssembly. The app runs it in an interpreter, so one file works on every platform, and users can add it themselves:
download it from your release page and put it in the app's firmware folder.

The format is in [RFM1-FORMAT.md](RFM1-FORMAT.md). In short: a Wasm module with a JSON manifest (id, name, version,
licence, the source URL with its commit, authors), your credits, the licence texts, and a fixed set of exports.

## What you need

- Your firmware as a core in FM1VST: a glue folder in `core-glue/` with its `fm1_add_core` call (see
  `core-glue/common/README.md`, "Adding a core"). If FM1VST already builds your firmware, this is done.
- [Emscripten](https://emscripten.org) (`emcc`, and its `emnm` and `wasm-opt`; tested with 6.0.13), CMake 3.22 or
  newer, and the Python 3 your generate step uses (Pillow and fontTools for the Felucca-family generators).

## Build one

1. In your core's glue file, after its `fm1_add_core`, one line:

   ```cmake
   fm1_add_rfm1(<id> AUTHORS "Your Name")
   ```

   Without `-DFM1_RFM1=ON` this line does nothing, so it can stay in the glue for every build.

2. Configure a build of just your core, with the .rfm1 on (no plugin, no JUCE), and build it:

   ```sh
   cmake -S . -B build-rfm1 -DFM1_RFM1_ONLY=ON -DFM1_PUBLIC_BUILD=ON -DFM1_ONLY_CORES=<id>
   cmake --build build-rfm1 --target rfm1_<id>
   ```

   The file is `build-rfm1/rfm1/<id>-<version>.rfm1`. (`FM1_ONLY_CORES` takes a glue folder name or a
   `core_<name>.cmake` file's name, so only your firmware's submodule has to be checked out. `rfm1_all` builds every
   core whose glue has the line.)

`fm1_add_rfm1` builds the same units as the plugin's core, with the same defines, includes and generate step, plus
`core-glue/common/rfm1_shim.c` (the exports; with the optional live panel labels, `rfm1_labels_epoch` / `rfm1_label`:
[DYNAMIC-LABELS.md](DYNAMIC-LABELS.md); with `WANTS ain1`, the audio input's `rfm1_audio_in` / `rfm1_audio_in_info`:
[VIRTUAL-CARTS.md](VIRTUAL-CARTS.md) section 8). It takes the manifest's id, name, version, licence and source URL from
your descriptor, so they always match. Its options (cmake/FM1Rfm1.cmake has them all):

| Option | What |
| --- | --- |
| `AUTHORS <names..>` | the manifest's authors |
| `MEMORY_MIB <n>` | the module's fixed memory (default 8, at most 32): raise it if the link says the data does not fit |
| `STACK_KIB <n>` | the C stack inside that memory (default 1024) |
| `LICENSE_FILES <name=path..>` | the licence texts to carry. Default: your `LICENSE` (or `COPYING`), every file in `LICENSES/`, and `LICENSING.md` |
| `SOURCE_URL <url>` | instead of the descriptor's `source_url` |
| `DEFINES`, `FLAGS`, `INCLUDES` | more, for the Wasm build only |
| `RUN_ARGS <args..>` | options for the test's `rfm1_run` (`--sysex bank.syx`, `--settle 5000`) |
| `VIRTUAL` | a Fantasy CFW, host-only ([VIRTUAL-CARTS.md](VIRTUAL-CARTS.md)): the manifest says `"virtual": true`; `MEMORY_MIB` up to 512, the file under 16 MiB, the `rfm1_host_1` imports allowed |
| `NEEDS <cap..>`, `FOLDERS <name=r\|rw..>` | (with `VIRTUAL`) the manifest's `needs` (`fs1`: the shared folder's files) and `folders` (`code=r data=rw`) |
| `AUDIO_OUT <n>` | more output channels (aout1, RemoteFM1's RFM1-FORMAT.md section 9): 4, 6, 8 or 10 in all. The module exports `rfm1_audio_out_info` / `rfm1_audio_out` from the core's `fm1core_audio_out_info` / `fm1core_audio_out` (fm1core.h). An fm1-base app's `usb_audio_channels` gets them through `plat_usb_ch` (core-glue/plat/plat_glue.h): LOOM-8's tracks on 3..10 |
| `WANTS <cap..>` | host capabilities the module uses where a host has them, the manifest's `wants`: `ain1` (audio input, [VIRTUAL-CARTS.md](VIRTUAL-CARTS.md) 8), `cpu2` (core 1 on a host thread: below), `rd1` (readouts: below), `tk1` (the Move takeover: below) |
| `TAKEOVER_NAME <name>`, `TAKEOVER_CLAIMS <claims..>` | (with `WANTS tk1`) the manifest's informative `"takeover": {name, claims}` ([RFM1-TAKEOVER.md](RFM1-TAKEOVER.md) 3) |
| `NO_SKIN` | leave out the skin of your repository's `RemoteFM1Metadata.json` ([RFM1-METADATA.md](RFM1-METADATA.md)) |
| `NO_LABELS` | leave out its button and knob labels |

A skin and labels of your own: a `RemoteFM1Metadata.json` at your repository's root (the background images, the
colours, the panel's button and knob names), packed into the file at build time: [RFM1-METADATA.md](RFM1-METADATA.md).

To try a core without editing its glue: `-DFM1_RFM1_EXTRA_CORES=<id>` (or `all`).

### What does not work in Wasm

The module runs on one thread, has no file system, and may only use the WebAssembly that wasm3 0.5.0 runs (no SIMD,
threads, reference types, tail calls or exceptions; the build sets the feature flags). So:

- A glue on the coroutine (`core-glue/common/coro.h`: a firmware main loop that never returns, as Dinghy's, Melodee's,
  Tonitor's and LOOM-8's fm1base glue run it) builds too, with nothing to add to the glue. Natively the coroutine is
  `coro.c` (`ucontext`); in an .rfm1 it is `core-glue/common/coro_asyncify.c`, the same API on Binaryen's Asyncify,
  driven from inside the module (no JavaScript, no import). The link (`tools/rfm1/rfm1_link.cmake`) tells the two
  kinds of core apart by their objects (`emnm`: does a unit call `fm1coro_*`?). A core that does not links exactly as
  before. A core that does gets `coro_asyncify.c` and a names section, then Emscripten's `wasm-opt` with
  `--asyncify` (only the functions that can reach the yield are instrumented; the resumer, `fm1coro_rfm1_switch`, is
  in the removelist), the build's features, the Asyncify exports removed, `-O3` and the names stripped. A warning
  from `wasm-opt` (a removelist name that is not there) or anything named `asyncify` left in the module fails the
  build. The coroutine's C stack (512 KiB) and its Asyncify buffer (64 KiB) are allocated inside the fixed memory.
  (S1Vave, and OberVave, Frodhes and GR!MM tried this way: 6.5-7x real time in `rfm1_run` on an M5 Mac. S1Vave's
  .rfm1 plays the same samples as its native core, bit for bit, over `rfm1_run`'s whole run.)
- Lua's errors (`setjmp`/`longjmp`, which would need wasm exceptions) can run on the same Asyncify pass, unwind only:
  a Lua port's configuration includes `core-glue/common/lj_asyncify.h` in .rfm1 builds (`LUAI_THROW`/`LUAI_TRY`),
  and the link, seeing a unit call `lj_throw`, adds `core-glue/common/lj_asyncify.c` with `lj_try_impl` in the
  removelist (Ripple: `core-glue/ripple/`). Nothing may yield an `fm1coro` while Lua frames are on the stack.
- A native-only header your firmware's host build includes can get a stand-in in `core-glue/common/wasm_compat/`
  (on the include path of .rfm1 builds only). ChoralRoot's `<os/lock.h>` is there.
- `FM1CORE_RFM1` is defined (to 1) in .rfm1 builds, and `__EMSCRIPTEN__` as always with emcc, if your glue needs to
  tell.

### Core 1 (`cpu2`)

The FM-1 has two cores; an .rfm1 runs on one thread, so a firmware's second-core job (voices into a bus of their own,
an effect) runs inline in its host build. A host with a thread to spare (RemoteFM1 on the Ableton Move) can run that
job beside the rest, as the unit does, when the module is built with `WANTS cpu2`:

- **The core's side** is `core-glue/common/rfm1_cpu2.h`, which a core's rfm1 back end calls where the device calls its
  core 1 (felucca-bsp's `fm1_cpu1_run` / `fm1_cpu1_wait`, fm1-base's `plat_cpu2_run` / `plat_cpu2_wait`):
  `rfm1_cpu2_run(fn, arg)` (1: handed over; 0: run it yourself), `rfm1_cpu2_wait()` after a 1 (0: done; else run it
  yourself, in the same block), `rfm1_cpu2_core()` (1 inside a job core 1 runs: for scratch kept per core). Every unit
  of such a build has `FM1CORE_RFM1_CPU2`; elsewhere the header gives the one-core answers inline. The FM-1's rules
  hold: one job at a time, core 0 leaves the job's data alone until the wait, a job never hands a job on, never
  yields an `fm1coro`, never allocates, and the samples must not depend on which side ran it.
- **The module's side** (the shim, `RFM1_CPU2`): no import, so every host runs the file as before. `rfm1_cpu2() -> ptr`
  gives a record `{u16 version 1, u16 size 12, u32 run, u32 wait}`: two indices in the module's function table, the
  entries `rfm1_cpu2_run` / `rfm1_cpu2_wait` call through (`run`: `(i32 fn, i32 arg) -> i32`; `wait`: `() -> i32`). As
  built they are the one-core answers (run 0). A host with a thread for core 1 puts its own two functions into those
  table entries of the instance after instantiating it, makes a twin (the same module, instantiated again with the
  instance's memory: its own globals, so its own stack pointer, and its own table, whose entries stay the one-core
  answers), gives the twin a stack in the module's memory (`rfm1_c1_stack(top)`, from `rfm1_alloc`; at least 64 KiB)
  and runs each job on its own thread as `rfm1_c1_call(fn, arg)` on the twin. A job that traps is reported not done:
  the module runs it itself.
- `rfm1_check` requires all three exports or none, and `cpu2` in `wants` with them. The manifest's `wants` is
  informative: hosts ignore names they do not know.
- Used by ChoralRoot (`cr_cpu1.h`, from 0.42 with its rfm1 branch) and FIZZ (RemoteFM1's `tools/rfm1/fizz/fizz_unit.c`).

### Readouts (`rd1`)

A host can show what a knob controls and its value while it turns, a sound's name and a message (RemoteFM1 on the
Ableton Move: its knob popup). Built with `WANTS rd1`:

- **The firmware's side** is `core-api/rfm1_ext.h` (MPL-2.0: vendor it): `RFM1_READOUT(ctl, title, value)`,
  `RFM1_READOUT_BAR(ctl, title, value, v, lo, hi)`, `RFM1_READOUT_SUB(ctl, title, value, sub, v, lo, hi)`,
  `RFM1_PRESET(title, name)`, `RFM1_MESSAGE(text)`. `ctl` is the DYNAMIC-LABELS slot (0..6 the knobs) or -1 (the
  control in use). Every unit of an rd1 build has `FM1_HOST_EXT`; without it (the FM-1, a native host) each macro is
  `((void)0)` and its arguments are not evaluated, so the device image is the same bytes. Call them from the UI.
- **Without a line:** a core with the Tier 2 map's `knob_target` and `params` (ChoralRoot) gets a readout for each
  knob whose detents it took in a frame and said nothing about: its target's name, `text(get())`, the bar from the
  range (the bridge). Its knobs' live labels come from the same targets' names (upper case, 10 characters) when the
  core has no labels of its own; a knob with no target keeps the static label. The firmware's own readout wins.
- **The module's side** (the shim, `RFM1_RD1`): `rfm1_readout() -> ptr`, one record at a stable address:
  `{u16 version 1, u16 size 48, u32 seq, i8 control, u8 kind (1 param, 2 preset, 3 message), u8 flags (1 bipolar),
  u8 ms10 (0: the host's default), i16 frac (0..1000, -1 none), char title[17], char value[17]}` (printable ASCII,
  NUL-terminated; `value` holds "value sub"). A host calls `rfm1_readout` once after init and reads `seq` after each
  `rfm1_frame`: a new one is shown. No import; hosts without rd1 ignore the export. `rfm1_check` wants `rd1` in
  `wants` with it. `rfm1_run --readouts` prints each readout and the knobs' labels when they move;
  `rfm1_run --turn K1@2000:3` turns a knob (SELECT ALGO PRESETS K1..K4) by detents at a device ms;
  `--expect-readout ctl_3_E5_CUT_=_412_Hz` fails the run unless a readout `ctl C title = value` contains it ('_' a
  space). `fm1_add_rfm1(.. WANTS rd1 READOUT_TEST <turns and expectations>)` makes that the ctest `rfm1_rd_<id>`.
- **An adapter, without a map:** a glue that reads a firmware's own tables defines `fm1core_rd_knob(role, title,
  value, n, &frac, &flags)` (rfm1_ext.h; weak): the bridge asks it, for a core with no Tier 2 map, about each knob the
  core took in the frame; with `fm1core_labels_epoch` / `fm1core_label` beside it the same adapter gives the live
  labels. The pattern: core-glue/dinghy/grimm_rd.h (GR!MM 2, Glitch, Arctic Scream).
- **Built with it:** ChoralRoot (its own lines and the bridge), Melodee and Felucca (the bridge over their Tier 2
  maps, core_melodee_params.c / core_felucca_params.c), GR!MM 2, Glitch and Arctic Scream (grimm_rd.h), EVILGENIUS
  (core_evilgenius.c: FIZZ's own label and turn models), Dungeon Tracker, GHOULBOX and Mini-Tone (Felucca's map in their
  .rfm1 only, with each fork's dispatch differences as the map's options: fw_map_fork.h), SLOOP, Clipper, KARMA,
  M-PYRE, ORBIT and Stallion (core-glue/sloop/sloop_rd.h: the four columns as the screen drew them), Elasticat (its own
  lines, with a tree that has them: EC_SOURCE); and, through `core-glue/common/rd_cols.h` (a firmware's drawn knob
  row copied after each frame: labels, and a readout when the turned knob's drawn value changes), SLOOP 3, CoralFlower,
  a22, h1, FMav-1, M-Vsynth2, JamesOS, SID-1, FMEL-1, AURORA, bytemania, FM-KICK, Golf, Tekstuur, Mighty Arp, FoMni,
  HYP-1, Nachtfahrt, ZEN-1, PERF-1, Niagara Drift, FiMba, carnac-os, SiDJ, X0X, Fart Machine, Quartz, Berlinan,
  SY-1, ECHOFIELD, Chimera Vocal, Dabke, CULTBOX, orgigs, M-vance, Golana, FMini-1, cursed os, obssynth, MONO2K,
  Matryoshka, Speak37, SPELLBOX and Viscera. The audio is bit-identical to the released files over the same run.

### The Move takeover (`tk1`)

A firmware's own Ableton Move interface ([RFM1-TAKEOVER.md](RFM1-TAKEOVER.md)): it draws the Move's 128 x 64 screen
and its LEDs and gets the Move's raw input for the parts it claims. Built with `WANTS tk1`:

- **The firmware's side** is `core-api/rfm1_move.h` (MPL-2.0: vendor it): `rfm1_tk_register(name, claims, &ops)` once
  from init (`ops`: `input(pkt[4])`, `state(on, claims, flags)`), `rfm1_tk_screen()` / `rfm1_tk_screen_done()`,
  `rfm1_tk_led`, `rfm1_tk_white`, `rfm1_tk_claims`, `rfm1_tk_say`, `rfm1_tk_shown`, the claim bits `RFM1_TK_*`, the LED
  indices `RFM1_TK_L_*`, the Move's surface numbers (`RFM1_TK_NOTE_PAD0`, `RFM1_TK_CC_SHIFT` ..) and
  `rfm1_tk_claim_of(pkt)`. Every unit of a tk1 build has `FM1_HOST_TK`; without it (the FM-1, a native host) each call
  is an inline no-op (`rfm1_tk_screen()` is 0), so the device image is the same bytes. Keep the Move interface in its
  own source files (or behind `#ifdef FM1_HOST_TK`).
- **The module's side** (the shim, `RFM1_TK`): `rfm1_tk() -> ptr`, the 1468-byte record of RFM1-TAKEOVER.md 4.2
  (version 1; its offsets are static asserts in `rfm1_shim.c`), `rfm1_tk_input(pkt) -> i32` and
  `rfm1_tk_state(on, claims, flags)`, which call the registered ops. The firmware draws into a buffer of its own;
  `rfm1_tk_screen_done` copies it into the record and moves `scr_seq` only when the picture changed, `rfm1_tk_led`
  moves `led_seq` only when the colour changed, each `rfm1_tk_say` moves `say_seq`. No import: hosts without tk1
  ignore the exports.
- `rfm1_check` requires all three exports or none, `tk1` in `wants` with them, and a well-formed `takeover` entry when
  there is one; with `--run <rfm1_run>` (the build and the `rfm1_<id>` test pass it) it boots the module and checks
  the record after init (version, size, a printable NUL-terminated name, claims within 0xFF). `rfm1_run` checks the
  record of every module that exports `rfm1_tk`; `rfm1_run --tk-info` prints it as JSON.
- `rfm1_run --tk` drives the takeover as a Move host does: `rfm1_tk_state(1, claims, flags)` at `--tk-at` (100 ms;
  `--tk-speech`: flags bit 0), Move surface events from `--tk-ev` / `--tk-script` (`pad:5@200+50`,
  `button:shift@400`, `knob:1@320:3`, `step:`, `touch:`, `note:`, `cc:`, `raw:`; only the claimed controls' reach the
  firmware), the release of everything held and `rfm1_tk_state(0, ..)` at `--tk-off-at`; it prints each change of the
  record (`tk-claims@MS`, `tk-screen@MS`, `tk-led@MS: I R G B`, `tk-say@MS`) and `--tk-screen F` (`-`: stdout) saves
  the screen as a PBM. `--help` has the grammar.
- `tkdemo` (`tools/rfm1/tkdemo`), the test firmware: `build/rfm1/tkdemo-1.0.rfm1` in every `-DFM1_RFM1=ON` build
  (not a plugin core; `-DFM1_RFM1_TKDEMO=OFF` leaves it out). It claims all but Track and Volume, shows the last input
  ("PAD 5", "CC 50", "K1 +3"), lights a pad while pressed, says "pad N", and toggles its claims to ALL with Shift.
  `ctest -R rfm1_tk` runs it through all of the above (`tools/rfm1/rfm1_tk_test.py`).

### Engine parameters (`ep1`)

A firmware's sound engines' parameters for its host, by frozen id (the protocol: `engine-params/SPEC.md` beside this
repository; [SPEC-NOTES.md](SPEC-NOTES.md) what the implementation changed): its layers (ChoralRoot: Chord, Bass),
its engines, each engine's pages of up to eight parameters, every parameter with its range and its words. A host
reads them without a call per frame and sets them absolutely (RemoteFM14Move: rfm1cs's ENGINE knob page). Built with
`WANTS ep1`:

- **The firmware's side** is `core-api/rfm1_ep.h` (MPL-2.0: vendor it): `const rfm1_ep_src_t *fm1core_ep(void)`
  (weak; asked once after init) gives the layer names, the engines and their pages (`rfm1_ep_page_t`: an id, a name,
  a group, eight pids), the parameters (`rfm1_ep_param_t`: pid, name, min, max, def, fmt INT / FIXED / TABLE / NOTE /
  PERCENT / ONOFF, flags BIPOLAR LIST DYN NOLOCK SOUND GLOBAL RETIRED, a unit, a step, the CC that reaches it, and for
  a TABLE its names or a `text()` the shim calls for every value) and the callbacks `layer(l, &engine, &ch)`,
  `get(l, pid)`, `set(l, pid, v)` (as the firmware's own editor writes it, quietly), optional `words(l, pid, name,
  text)` (the watched cells: a DYN parameter's words now) and `epoch()` (the description made again). Every unit of
  an ep1 build has `FM1_HOST_EP1`; without it the header declares nothing.
- **The module's side** (the shim, `RFM1_EP1`): the EP1D description (SPEC.md 3: header, layers, engines, pages,
  params, tables, strings; tables with the same words shared; FNV-1a), the 264-byte EP1R record (SPEC.md 4: the layers'
  engines and channels, every value of every layer, the watched page's cells; each part's seq moves only when it
  changed), refreshed after each `rfm1_frame` and each `rfm1_ep_set`; `rfm1_ep() -> ptr`, `rfm1_ep_set(op, layer,
  pid, value) -> 0 | -1` (SET; LOCK / FREE / FREE_ALL answer -3 until the locks' stage) with `rfm1_ep_host` set and the
  readouts it pushed dropped, `rfm1_ep_watch(layer, page)`. No import: hosts without ep1 ignore the exports.
- `rfm1_check` requires all three exports or none, `ep1` in `wants` with them and them with it; with `--run` it boots
  the module, reads the record and checks the whole description. `rfm1_run --ep-info [--out F]` prints the record
  (and writes the blob), `--ep` prints the description and every change of the record, `--ep-set L:PID=V@MS` and
  `--ep-watch L:P@MS` call the exports as a host's queue does.
- **ChoralRoot** (`core-glue/choralroot/cr_ep.c`, no change to its source): the editor's own walk (cr_edit.c's
  screens and lanes, the same the MIDI CC blocks use) paired into Move pages of eight, the editor's long labels, its
  words for every value fitted to a format or kept as a table, writes through `ce_knob`'s three ways; the ids frozen
  in `cr_ep_ids.h` (append-only; `cr_ep_test --gen` appends new cells). `ctest -R ep` runs `rfm1_ep_choralroot` (the
  whole unit natively: the ids against the firmware, every parameter of every engine of both layers written by ep1
  and by the editor's knob from the same state, byte for byte; the audio bit-identical with every value read after
  every frame) and `rfm1_cr_ep` (the .rfm1 through `rfm1_run`).

## Check it

```sh
python3 tools/rfm1_check.py build-rfm1/rfm1/<id>-<version>.rfm1     # the file against the format
build-rfm1/rfm1_run build-rfm1/rfm1/<id>-<version>.rfm1             # boot it, hold a chord, report
ctest --test-dir build-rfm1 -R rfm1_                                # both, for every .rfm1 built
```

`rfm1_check` (Python 3, standard library only) reads the whole module: the manifest's fields, the imports (only the
format's WASI list), the exports and their signatures, the memory (fixed, at most 32 MiB), and every instruction (it
finds SIMD, atomics and the rest), and a skin's sections when there are any (RFM1-FORMAT.md section 8). The build runs
it on every file it makes. A manifest that says `"virtual": true` is
checked against [VIRTUAL-CARTS.md](VIRTUAL-CARTS.md)'s rules instead, by itself: the `rfm1_host_1` imports with their
exact signatures (and `fs1` in `needs` when it imports them), up to 512 MiB of memory, a file under 16 MiB. Any other
module that imports `rfm1_host_1` fails.

`rfm1_run` runs the file in wasm3 as an app would: a fresh flash, `init`, then the device clock for 4 s, then D4 F#4 A4
(velocity 100) held for 5 s. It prints the chord's RMS, the LCD writes and how many times faster than real time it ran,
and fails if the module trapped, halted, stayed silent or never drew. `--wav out.wav` saves what it played, `--vel V`
and `--lean L` play the chord at another velocity and aftertouch; `--help` has the rest. For a Fantasy CFW,
`--folder DIR` serves `rfm1_host_1` from a RemoteFM1 folder (`DIR/code/<id>`, `DIR/data/<id>`, `DIR/audio`: sandboxed,
as the manifest's `folders` allow, each request completed one device ms later); `--press PLAY@3000` presses a button by
its label, `--info-at MS` and `--expect-ui TEXT` print and check `ui_info`, `--flash-in` / `--flash-out` boot from and
save the flash image, `--screen out.ppm` saves the screen, `--describe --labels` prints the descriptor (its
`button_names`) and the live labels before init. `--audio-in in.wav` (with `--audio-in-latency N`,
`--audio-in-off-at MS`, `--audio-in-once`) is a host's live input for a cart with `ain1` (VIRTUAL-CARTS.md 8). A cart
with more output channels (aout1) is given them as a host does, and each channel's level is printed; `--aout-wav F`
saves all of them as one WAV, `--expect-aout N` fails unless it gives N and 3..N sounded, `--no-aout` takes none. Interpreted, a core runs 10-25 times slower than native (FM1VST's own cores: 8-50x real time on an M5
Mac with wasm3).

## Attach it to your releases (GitHub Actions)

A workflow in your firmware's repository that builds the .rfm1 from the released commit and attaches it to the
release. It checks out FM1VST, puts your repository where FM1VST's glue expects it (`cores/<Name>`, the submodule
path), and builds only your core. Replace `Felucca` / `felucca` with your folder and id, and pin FM1VST to a release
that has `fm1_add_rfm1` and your glue.

```yaml
name: rfm1
on:
  release:
    types: [published]
permissions:
  contents: write
jobs:
  rfm1:
    runs-on: macos-15
    steps:
      - name: FM1VST (the glue and the tools)
        uses: actions/checkout@v4
        with:
          repository: Quixotic7/FM1VST
          ref: v0.7.0                       # an FM1VST release with fm1_add_rfm1 and your glue
          path: fm1vst
      - name: This firmware at the release's commit, where FM1VST's glue expects it
        uses: actions/checkout@v4
        with:
          path: fm1vst/cores/Felucca
          submodules: recursive
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 6.0.13
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - run: python -m pip install Pillow fonttools
      - name: Build and check the .rfm1
        working-directory: fm1vst
        run: |
          cmake -S . -B build -DFM1_RFM1_ONLY=ON -DFM1_PUBLIC_BUILD=ON -DFM1_ONLY_CORES=felucca \
                -DCR_PYTHON="$(which python)"
          cmake --build build --target rfm1_felucca rfm1_run
          ctest --test-dir build -R '^rfm1_felucca$' --output-on-failure
      - name: Attach it to the release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release upload "${{ github.event.release.tag_name }}" fm1vst/build/rfm1/*.rfm1 --clobber \
            --repo "${{ github.repository }}"
```

The version in the file name and the manifest is the one your glue reads (`VERSION_FILE` / `VERSION_REGEX` in
`fm1_add_core`). A glue that gives a fixed `VERSION` gives the same version for every release: move it to
`VERSION_FILE`, or update FM1VST's glue with your release. This template has not run on GitHub yet; the commands in it
are the ones tested locally (macOS, emcc 6.0.13).

## Licences

An .rfm1 is a binary of your firmware, so your licence's rules for binaries apply to it.

- **GPL firmware** (most FM-1 firmwares, and FM1VST's glue, which is GPL-3.0-only): the .rfm1 must come with its
  source. The manifest's `source_url` is your repository at the exact commit the file was built from, and `built_by`
  names the FM1VST version and commit whose glue it contains: link both from the release that carries the file, and
  keep those commits public. The app shows `source_url`, so a user can get the source.
- The file carries the licence texts: your `LICENSE`, `LICENSES/` and `LICENSING.md` (or `LICENSE_FILES`), FM1VST's
  licence (the glue), and the notices of the Emscripten runtime compiled into it (MIT: Emscripten, musl libc). Your
  descriptor's credits (`fm1_add_core CREDITS` / `CREDITS_FILES`) go into the `rfm1.credits` section, which the app
  shows in its About screen.
- A core FM1VST marks `GATED` (rights not settled: CORES-PLAN.md 6) is not built as an .rfm1, unless its
  `fm1_add_rfm1` says `HOSTED_DESPITE_GATE "<reason>"`: then the file is built to be hosted (RemoteFM1FW) and
  downloaded, never bundled in an app or a public native build (FM1_PUBLIC_BUILD=ON still leaves the core out). The
  owner's decisions of 2026-10-31: SLOOP and Clipper (the Hügelton icon atlas), in their words: "I would build them as
  the original authors intended and we only host the compiled .RFM1 at https://github.com/Quixotic7/RemoteFM1FW they
  are not distributed inside the app, the app just downloads and runs them."; and X0X: "Include X0X" (its 909
  cymbal-sample question left open, hosted only).
