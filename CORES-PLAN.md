# FM1VST cores: the latest releases plus 18 more firmwares (plan, 2026-10-18)

The owner wants FM1VST to run the latest ChoralRoot, Felucca and Melodee, plus 18 more firmwares from
[the FM-1 Firmware Hub](https://thefirmwarehub.com): OberVave, X0X, AMB-1, SLOOP, CoralFlower, FoMni, FM-106,
FiMba-1, JIANT FM, sloopDX, Frodhes, GHOULBOX, Rainbow Mode, ORBIT, DUELL, PurpleMonkey, Fart Machine One and No Man
Sky FM.

Status: phase 0 is done (branch cores-abi5: ABI 5, core-glue/common, per-core CMake, tests/core_conformance; the
three cores at their pinned versions pass everything). Phase 1 is done (branch cores-all: the six families merged,
all 21 cores built and bundled, ABI 6 for the credits, the suite passing in the default, Release and public builds).
Phase 2 is a plan. The per-firmware research is in `research/hub-2026-10-18/<id>.md`;
this file is its summary.

## 1. What the survey found

All 21 are open source and **all 21 built natively with Apple clang and ran headless here** (screen and sound), each
in seconds, with nothing installed. FM-106 needs a 6-line host shim, and No Man Sky a small driver. None has inline
asm outside its HAL or another macOS blocker. Dual-core code runs inline on the host.

| Firmware (hub version) | License | Built on | Host sim upstream | Family below |
|---|---|---|---|---|
| Felucca 1.6.1 | GPL-3.0 (engines MIT) | the original | native + wasm (`web/emu/felucca_web.c`) | A |
| Rainbow Mode 1.1.5-rb | GPL-3.0 | Felucca 1.1.5 | native + wasm (felucca_web) | A |
| GHOULBOX 2.3.2 | GPL-3.0 (+ OFL fonts) | Felucca 1.4.1 | native + wasm (felucca_web) | A |
| JIANT FM 0.9.9 (0912abb) | GPL-3.0 (+ CMUdict BSD-2) | Felucca 1.1.5.1 + SLOOP songs | native + wasm (felucca_web) | A |
| DUELL 0.2.20-beta | GPL-3.0 + naming terms | Felucca main | felucca_web (native via its check) | A |
| ChoralRoot 0.25.1 | GPL-3.0 | Felucca 1.0 + Melodee ports | native emulator + wasm | B |
| PurpleMonkey 0.11 | GPL-3.0 | ChoralRoot 44453d0 + SLOOP drums | native emulator + wasm | B |
| CoralFlower 0.2 | GPL-3.0 | ChoralRoot 0.22 | native emulator + wasm | B |
| X0X 1.1.0 | GPL-3.0 | Felucca fork + Charles Vestal's `plat.h` | native (`host/x0x_host.c`) + wasm | C |
| FoMni 0.4 | GPL-3.0 | X0X's `plat.h` | native + wasm | C |
| FiMba-1 0.6.1 | GPL-3.0 | FoMni | native + wasm | C |
| Fart Machine One 1.0.0 | GPL-3.0 (code) | FoMni | native + wasm | C |
| AMB-1 1.3 | **MPL-2.0** | fm1-base (Vestal) on felucca-bsp | native + wasm | C' |
| SLOOP 2.7.1 | GPL-3.0 + Hügelton icons (§7) | early Felucca | native tests (`hostsim`, `ui_pages_test`) | D |
| sloopDX 4.0 (eeec0a6) | GPL-3.0 | SLOOP 2.7 | native tests | D |
| ORBIT 0.7.1 | GPL-3.0 + **OP-1 material** | SLOOP / Felucca | native preview + hostsim | D |
| No Man Sky FM 0.4 (main) | GPL-3.0 (+ credit request) | SLOOP 2.7.1 | wasm only | D |
| OberVave 1.5.1 | GPL-3.0 | Dinghy (zvenson) | native `tests/host.c` + wasm | E |
| Frodhes 0.9.2 | GPL-3.0 | Dinghy | native + wasm | E |
| FM-106 Beta4 | GPL-3.0 (KR-106 port: **provenance unchecked**) | Dinghy + a second core | native (needs a shim) | E |
| Melodee 1.4.3 | GPL-3.0 (+ BSD-3 uPD933) | Felucca 1.0 + x0x 909 + Prophet / SID / CZ | native (`hostsim`, `ui_test`) | F |

## 2. What breaks today

- **ChoralRoot 0.25.1:** `emu_hal_t` gained fields before `led[]` (`key_vel`, `key_lean`), then `led_mid`, `led_faint`,
  `pa2`, function pointers and more. Since the core ABI includes ChoralRoot's `emu_hooks.h`, that's an ABI break by
  itself. Also:
  - the version string moved to `choralroot.c`;
  - two new generated headers;
  - `CU_FX` was restructured, so the params map needs fixing;
  - the chord-key layout switch changes the panel labels;
  - the `EMU_C1=thread` path must stay off.
- **Felucca 1.6.1:** the pinned glue no longer compiles: new LCD power calls, `menu_items.c`, `master.c`, and a
  host-buildable `main.c`. The engines moved into a submodule. The 1.6.1 tree ships `felucca_web.c`, a complete host
  unit, so the glue should use it.
- **Melodee 1.4.3:**
  - **A silent memory overrun:** the glue declares two LED dim planes and 1.4.3 writes a third (hidden by `-w`).
  - The firmware glue has 7 errors (`PATTERNS`, `load_pat16`, the `AF_*` fonts).
  - The params map has 64 errors (the FX rework).
  - One extra generator: `tools/gen_909_tables.py`.
- **Every fork carries its own `emu_hal_t`** (CoralFlower's differs from the pinned one), and **every family now has
  more LED levels** than `emu_hal_t` carries:
  - breath / mid in the Felucca 1.6 family;
  - 3 planes in Melodee;
  - mid / faint in ChoralRoot;
  - a backlight layer in SLOOP.

## 3. Phase 0: ABI 5, FM1VST's own panel (one agent, first)

The ABI stops including a firmware's header and owns its panel struct:

- **`fm1panel_t`** (in `core-api/fm1core.h`):
  - Host → core: `keys`, `key_vel[27]`, `key_lean[27]`, `buttons`, taps, `enc[7]`, `master`, `ready`.
  - Core → host: `led_level[41]` (0..255 per matrix id) plus PLAY green, `lcd[240*240]` (RGB565 as the panel takes
    it), `lcd_writes`, and `btn_id` / `enc_id` / `enc_dir`.

  Each glue translates between this and its firmware's own HAL. A per-LED level maps onto every family's planes and
  onto RemoteFM1's five levels.
- **`core-glue/common/`:** what every glue needs, written once:
  - the 1 MiB NOR image with the staged boot (ABI 2);
  - the `exit` / `atexit` redirection;
  - a 256 → 2 × 128-frame audio FIFO (SLOOP, Vestal and Dinghy render 256);
  - a tick-unit adapter (24 MHz ticks vs µs);
  - a ucontext coroutine for firmwares whose `app_main` never returns (Dinghy);
  - a version string passed in at build time.
- **`fm1_add_core`:**
  - each core's CMake moves into `core-glue/<id>/core.cmake`, picked up by a glob, so parallel work never edits the
    top-level CMakeLists;
  - options: no-GEN, extra generators, `APP_VER`, recursive submodules, compile flags (the `.pool` section attribute
    that Mach-O rejects).
- **The engine, plugin, RemoteLink and tests** move to `fm1panel_t`. The existing three cores are rebuilt against
  ABI 5 at their pinned versions first, so nothing regresses before the upgrades.
- **Per-core tests** (one harness, every core):
  - it boots headless;
  - the screen isn't blank;
  - a key press makes sound;
  - the flash round-trips (save, reboot, still there);
  - a double load works and the core halts cleanly.
  - Where the firmware ships its own host sim, its screen after a scripted run is compared with ours.

## 4. Phase 1: the families (six agents in parallel, each in its own FM1VST worktree and branch)

| Agent | Glue | Firmwares | Notes |
|---|---|---|---|
| A | **felucca_web**: one generic core that `#include`s the firmware's own `web/emu/felucca_web.c` and drives its `web_*` exports | Felucca 1.6.1, Rainbow Mode, GHOULBOX, JIANT FM, DUELL | add MIDI out (usb.c's queue in the same unit); second core off; seeded randomness for tests |
| B | **choralroot** (updated) | ChoralRoot 0.25.1, PurpleMonkey 0.11, CoralFlower 0.2 | each fork's own `emu_hal_t` → `fm1panel_t`; velocity / lean fed; layout-dependent labels |
| C | **plat** (+ fm1-base) | X0X 1.1.0, FoMni 0.4, FiMba-1 0.6.1, Fart Machine One 1.0.0, AMB-1 1.3 | small per-firmware adapters (FiMba's LED call); AMB's submodules; fm1-base's sense for later apps |
| D | **sloop** (hand glue from `hostsim` + a main loop) | SLOOP 2.7.1, sloopDX 4.0, ORBIT 0.7.1, No Man Sky FM | user sample slots read in place from flash; no tags for two: pin commits |
| E | **dinghy** (coroutine) | OberVave 1.5, Frodhes 0.9.2, FM-106 Beta4 | RAM stores copied into the NOR image in the device's sector format; FM-106's host shim as an overlay |
| F | **melodee** (updated hand glue) | Melodee 1.4.3 | the LED overrun first; params map rebuilt |

- **New cores start without a parameter map** (`params` NULL, `knob_target` NULL: the knobs work as relative
  turns). The three existing cores keep and repair theirs.
- **A seventh step merges the branches** (`.gitmodules` conflicts only), runs every test, and builds the plugin with
  all 21 cores.

## 5. Phase 2 (later, per firmware)

- Parameter maps and knob targets for the most-played cores (the Roto-Control).
- Factory presets per firmware.
- A theme per firmware.
- Checking each against its own hub "Play in browser" sound.

## 6. Licensing gate (before any public FM1VST release that bundles these cores)

Building them all for personal use is fine. Bundling them in a published FM1VST build redistributes their contents,
so these need an answer first:

| Firmware | Question | Current answer |
|---|---|---|
| ORBIT | CLUSTER engine translates OP-1 firmware DSP and data; no switch to leave it out | leave ORBIT out of public builds unless its author clarifies |
| FM-106 | where its KR-106 port comes from, and that port's licence | check |
| Fart Machine One | 11 bundled recordings, said to be from Pixabay; redistribution rights unverified | check, or ship without the recordings |
| SLOOP (and forks with its icons) | Hügelton icon atlas, all rights reserved under a GPL §7 permission | check the permission covers a plugin, or use replacement icons |
| AMB-1 | MPL-2.0 | combinable with GPL unless marked "Incompatible With Secondary Licenses": check the headers |
| DUELL | naming terms for unofficial builds | an unmodified build at a release tag is probably fine; ask the author |
| No Man Sky FM | credit request | credited in the About box (the descriptor's credits, ABI 6) |
| X0X | 909 hi-hat / ride / crash samples "ER-99 via 9W9", GPL-3.0 | gated until ER-99's author confirms they are not TR-909 ROM recordings |

Gated today (left out of FM1_PUBLIC_BUILD): choralroot and melodee (Casio CZ-1 tones, Sequential Prophet-5
programs), fm106 (Juno-106 patch data), duell (naming terms), rainbow (character look-alikes), fartmachine
(recordings), orbit (OP-1 material, the Hugelton atlas), sloop and sloopdx (the Hugelton atlas), nomansky (game
art), x0x (the cymbal samples). A public build bundles the other ten: felucca, ghoulbox, jiant, purplemonkey,
coralflower, fomni, fimba, amb, obervave, frodhes.

## 7. Rules

- The firmware trees are pinned, **unmodified** submodules.
- No downloads besides the submodules; JUCE is reused from the existing fetch.
- Agents never install the plugin (`FM1_COPY_PLUGIN=OFF`) and never touch the FM-1.
- Two agents never edit the same file: each owns its `core-glue/<family>/`, its `core.cmake` files and its
  submodules.
