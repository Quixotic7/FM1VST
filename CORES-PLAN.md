# FM1VST cores: the latest releases plus 18 more firmwares (plan, 2026-10-18)

The owner wants FM1VST to run the latest ChoralRoot, Felucca and Melodee, plus 18 more firmwares from
[the FM-1 Firmware Hub](https://thefirmwarehub.com): OberVave, X0X, AMB-1, SLOOP, CoralFlower, FoMni, FM-106,
FiMba-1, JIANT FM, sloopDX, Frodhes, GHOULBOX, Rainbow Mode, ORBIT, DUELL, PurpleMonkey, Fart Machine One and No Man
Sky FM.

Status: phase 0 is done (branch cores-abi5: ABI 5, core-glue/common, per-core CMake, tests/core_conformance; the
three cores at their pinned versions pass everything). Phase 1 is done (branch cores-all: the six families merged,
all 21 cores built and bundled, ABI 6 for the credits, the suite passing in the default, Release and public builds).
Phase 2 is a plan. 2026-10-21 (branch cores-oct21): the pins moved to each firmware's latest stable release (the
table's versions), and a 22nd core, LOOM-8 (Charles Vestal's groovebox on fm1-base), joined the fm1base glue, gated
(section 6); the owner un-gated it the same day. 2026-10-22 (branch cores-fw): four more public cores, Mini-Tone,
FMini-1 and Skiff on the felucca_web glue and GR!MM on the dinghy glue (`research/cores-2026-10-22/SURVEY.md`), and
DUELL un-gated (section 6). 2026-10-22 (branch cores-pigeon): PIGEON 0.4.1 (The Shrimp Blimp's sampler on Felucca
1.5.1) joined the felucca_web glue, public; its 500 KiB sample bank lives in the 1 MiB NOR as on the device (section
6's table). 2026-10-22 (branch cores-tonitor): Tonitor 0.53 joined on a glue of its own (core-glue/tonitor), public
(`research/cores-2026-10-22/SURVEY.md`). The per-firmware research is in `research/hub-2026-10-18/<id>.md`; this file is its summary.

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
| Mini-Tone 1.0.1 (added 2026-10-22) | GPL-3.0-only (Braids / DaisySP / Rings MIT, msfa Apache-2.0, reSID GPL-2.0+) | Felucca 1.5 | native + wasm (felucca_web) | A |
| FMini-1 1.8.0 (added 2026-10-22) | GPL-3.0-only (Felucca-Engines MIT) | Felucca 1.6.1 | native + wasm (felucca_web) | A |
| Skiff 1.4 (added 2026-10-22) | GPL-3.0-only | Felucca 1.1.5 + most of 1.6.1 | native + wasm (felucca_web) | A |
| ChoralRoot 0.26.1 | GPL-3.0 | Felucca 1.0 + Melodee ports | native emulator + wasm | B |
| PurpleMonkey 0.11 | GPL-3.0 | ChoralRoot 44453d0 + SLOOP drums | native emulator + wasm | B |
| CoralFlower 0.4.2 | GPL-3.0 | ChoralRoot 0.22 | native emulator + wasm | B |
| X0X 1.1.0 | GPL-3.0 | Felucca fork + Charles Vestal's `plat.h` | native (`host/x0x_host.c`) + wasm | C |
| FoMni 0.4 | GPL-3.0 | X0X's `plat.h` | native + wasm | C |
| FiMba-1 0.6.1 | GPL-3.0 | FoMni | native + wasm | C |
| Fart Machine One 1.0.0 | GPL-3.0 (code) | FoMni | native + wasm | C |
| AMB-1 1.3 | **MPL-2.0** | fm1-base (Vestal) on felucca-bsp | native + wasm | C' |
| LOOM-8 0.6-beta (added 2026-10-21) | GPL-3.0-only (fm1-base MPL-2.0) | fm1-base 71128a3 on felucca-bsp | native (`app/host.c`) + wasm | C' |
| Tonitor 0.53 (c39d18f, no tags: the Hub package's commit; added 2026-10-22) | GPL-3.0-only (msfa Apache-2.0, Rings MIT, LIRA-8 BSD-3) | Felucca / SLOOP's HAL, its own unity build | a render / screen harness (`tests/host/host.c`), no main loop; no wasm | G (its own: tonitor) |
| SLOOP 2.8 | GPL-3.0 + Hügelton icons (§7) | early Felucca | native tests (`hostsim`, `ui_pages_test`) | D |
| sloopDX 4.0 (tag v4.0) | GPL-3.0 | SLOOP 2.7 | native tests | D |
| ORBIT 0.7.1 | GPL-3.0 + **OP-1 material** | SLOOP / Felucca | native preview + hostsim | D |
| No Man Sky FM 0.5 (main 32e7f4c) | GPL-3.0 (+ credit request) | SLOOP 2.7.1 | wasm only | D |
| OberVave 1.5.3 | GPL-3.0 | Dinghy (zvenson) | native `tests/host.c` + wasm | E |
| Frodhes 0.9.4 | GPL-3.0 | Dinghy | native + wasm | E |
| FM-106 Beta5 | GPL-3.0 (KR-106 port: **provenance unchecked**) | Dinghy + a second core | native (needs a shim) | E |
| GR!MM 1.0 (added 2026-10-22) | GPL-3.0-only | Dinghy | native `tests/host.c` + wasm (`web/fm1_web.c`) | E |
| Melodee 1.4.5 | GPL-3.0 (+ BSD-3 uPD933) | Felucca 1.0 + x0x 909 + Prophet / SID / CZ | native (`hostsim`, `ui_test`) | F |
| PIGEON 0.4.1 (added 2026-10-22) | GPL-3.0-only (+ libmad GPL-2.0-or-later) | Felucca 1.5.1 | native + wasm (felucca_web) | A |

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
| F | **melodee** (updated hand glue) | Melodee 1.4.5 | the LED overrun first; params map rebuilt |

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
| DUELL | naming terms for unofficial builds (NOTICE.md: a build that is not the official release must not present itself as one) | un-gated (the owner's call, 2026-10-22): the core is the unmodified v0.2.20-beta tree named "DUELL (unofficial build)" with its commit in its source link, which satisfies the naming terms; the credits say it is unofficial and carry NOTICE.md. Keep the label wherever the plugin and the site show it |
| Mini-Tone | its LICENSING.md names MIT / Apache-2.0 / OFL texts its tree lacks (only GPL-2.0-reSID.txt is there) | public (the owner's call, 2026-10-22): FM1VST supplies the texts (core-glue/felucca_web/licences/minitone/, in its credits; tools/package_release.sh copies them and checks every named file is there) |
| FMini-1 | a few factory patch names name artists or marks (EMERSON, NUMAN, MOOG BASS ..): names only, the author's own patches | public (the owner's call, 2026-10-22) |
| Skiff | SLICE's BREAK and FUNK loops are AI-generated (the YuE2 model, published under YuE2's permission for creators) | public (the owner's call, 2026-10-22); the credits say they are AI-generated |
| GR!MM | none: its LICENSING.md leaves out Felucca's DX7-like FM patches and Dinghy's logo itself | public (2026-10-22) |
| No Man Sky FM | credit request | credited in the About box (the descriptor's credits, ABI 6) |
| X0X | 909 hi-hat / ride / crash samples "ER-99 via 9W9", GPL-3.0 | gated until ER-99's author confirms they are not TR-909 ROM recordings |
| LOOM-8 | its factory 909 hi-hat (`app/samples/909_hat.wav`) is a render of 9W9's hat samples (ER-99), X0X's open question; the rest is cleared: code GPL-3.0-only on MPL-2.0 fm1-base, Braids / PSXVerb / NuSaw MIT, the four breaks by u/FCTRYBEATS with their written permission, the recorded instruments VSCO 2 CE (CC0), fonts OFL | un-gated (the owner's call, 2026-10-21): it ships with its factory `909_hat.wav`, unchanged. In the owner's words: "ship the original, it is from the emulated 909 on the schwung firmware for Ableton Move that Charles Vestal made"; the credits name that origin. X0X stays gated (not decided) |
| PIGEON | the splash logo is by Dendy0097, no licence stated, shipped by the author in every build; the samples it compiles in are CC0 (VSCO-2, VCSL) and the Hügelton Sample Pack (CC0 since Felucca 5738617); no samples or bank ship with it | public (the owner's call, 2026-10-22): the logo shipped as the author ships it, credited |
| ChoralRoot, Melodee | Casio's CZ-1 preset tones and Sequential's Prophet-5 factory programs, no licence granted | un-gated (the owner's call, 2026-10-19): they ship compiled into the firmware, credited to Casio and Sequential; no raw .syx is distributed (tools/package_release.sh checks) |

Gated today (left out of FM1_PUBLIC_BUILD): fm106 (Juno-106 patch data), rainbow (character
look-alikes), fartmachine (recordings), orbit (OP-1 material, the Hugelton atlas), sloop and sloopdx (the Hugelton
atlas), nomansky (game art), x0x (the cymbal samples). A public build bundles the other twenty: choralroot (the
default), felucca, ghoulbox, jiant, purplemonkey, coralflower, melodee, fomni, fimba, amb, obervave, frodhes, loom
(un-gated 2026-10-21), duell (un-gated 2026-10-22), minitone, fmini, skiff, grimm, pigeon, tonitor (added public
2026-10-22), and the fm1emu engine.

## 7. Rules

- The firmware trees are pinned, **unmodified** submodules.
- No downloads besides the submodules; JUCE is reused from the existing fetch.
- Agents never install the plugin (`FM1_COPY_PLUGIN=OFF`) and never touch the FM-1.
- Two agents never edit the same file: each owns its `core-glue/<family>/`, its `core.cmake` files and its
  submodules.
