# FM1VST

The FM-1 emulator from [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) as a plugin,
planned as VST3, AU and Standalone. The emulator compiles the firmware's own C source against a host
HAL, so the plugin plays exactly what the device plays. The plan, the architecture and the phases are
in [FM1-VST-PLAN.md](FM1-VST-PLAN.md).

Status: phase 1. The ChoralRoot firmware builds as a loadable core module (`build/cores/choralroot.fm1core`)
behind a versioned C ABI, and a JUCE-free engine loads it, runs its device clock and resamples its output to the
host rate. The emulator's own headless scripts replay through the engine bit for bit. No plugin yet (phase 2).

## Layout

| path | what |
|---|---|
| `core-api/fm1core.h` | the core ABI (`FM1CORE_ABI 1`): the descriptor `fm1core_t` (id, name, version, source, flash size, button labels, parameter map, the panel state `emu_hal_t`, every `emu_fw_*` entry point, `shutdown`, `halted`) and the one symbol a module exports, `fm1core_get`. The threading contract is in its header comment. |
| `core-glue/choralroot/` | the ChoralRoot core: `core_choralroot.c` (the submodule's `tools/emu/emu_fw.c`, unmodified, with `exit` / `atexit` redirected so a firmware reboot halts the core instead of the host) and `core_choralroot_desc.c` (the descriptor) |
| `engine/` | C++17, no JUCE: `cores.{h,cpp}` (find and load modules, one private copy per instance), `device.{h,cpp}` (the single-thread device clock, input, LEDs, LCD, `render` at any host rate), `resampler.h` (4-point Lagrange) |
| `tests/` | `core_smoke`, `replay_test`, `double_load_test`, `halt_test`, and `script_runner` (the emulator's script grammar on a `Device`) |

## Cores

A core is the firmware compiled as a module (`*.fm1core`, a Mach-O bundle exporting only `fm1core_get`). The engine
looks for them in a bundled folder (the build's `build/cores/`; later the plugin's `Contents/Resources/cores/`) and in
the user folder `~/Library/Application Support/fm1emu/cores/` (created when first scanned): dropping a module there
installs it. Because the firmware's state is C globals, every loaded instance runs its own copy of the module,
copied to `~/Library/Caches/fm1emu/instances/<uuid>/` and deleted when the instance goes away.

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

**The reference emulator.** `replay_test` compares against the submodule's own headless emulator,
`cores/ChoralRootFM1/build/host/emu --headless --script ... --wav ...`. The build target `reference_emu` builds it
with the submodule's `tools/emu/build.sh` when it is missing (it needs SDL2: `brew install sdl2`; it writes only to
the submodule's gitignored `build/`), and the test runs it to make `build/reference/*.wav` when they are missing or
stale. Without SDL2, configure with `-DFM1_REFERENCE_TESTS=OFF` (or it is turned off with a warning) and
`replay_test` is not built.

Requirements: CMake 3.22+, clang, a C++17 standard library (macOS only for now: the core uses `os/lock.h`, the
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
