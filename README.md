# FM1VST

The FM-1 emulator from [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) as a plugin,
planned as VST3, AU and Standalone. The emulator compiles the firmware's own C source against a host
HAL, so the plugin plays exactly what the device plays. The plan, the architecture and the phases are
in [FM1-VST-PLAN.md](FM1-VST-PLAN.md).

Status: phase 0. The ChoralRoot firmware unit builds as a static library (`libcore_choralroot.a`) and a
headless smoke test boots it, plays a note and checks for sound and LCD output. No plugin yet.

## Building

The firmware sources come in as a git submodule, so clone recursively:

```sh
git clone --recursive https://github.com/Quixotic7/FM1VST.git
# or, in an existing clone: git submodule update --init --recursive
```

Then:

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

Requirements: CMake 3.22+, clang (macOS only for now: the core uses `os/lock.h`), and a `python3` with
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
