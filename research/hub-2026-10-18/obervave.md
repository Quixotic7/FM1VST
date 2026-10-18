# OberVave 1.5 (zvenson/obervave @ v1.5, 6194c3b)

Source: hubsrc/obervave. Host build tried: hubbuild/obervave.

## 1. License, tag, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` says GPL-3.0-only; 45 files carry `SPDX-License-Identifier: GPL-3.0-only`.
- Tag v1.5 ("Release 1.5 (FM-1_955) and SMK-37 1.5-beta"). Package identity FM-1_955.
- 126 tracked files, 48 C/H, ~14.2 k lines (with the 1.4 k-line font header and the ~1 k splash header). The synth
  itself is small: `obervave.c` 1952 lines plus `obv_*.h` ~2.5 k.
- Third party: Terminus font (OFL), logo fonts (URW / Inter, only in the SVG). The repo has no sample assets.

## 2. Lineage
- **Dinghy platform** (zvenson's own template, which is Felucca's HAL with SLOOP's changes). LICENSING.md: "The platform's
  wiring `platform.c`, the host mode `tests/`, the docs, the build ... zvenson, as Dinghy". `platform.c` begins with
  "OberVave's platform (Dinghy's)".
- Layout: `firmware/hal/*` (Felucca HAL), `firmware/src/platform.c` (boot, USB, OTA, settings sectors, ISR wiring), the
  app `obervave.c` + `obv_*.h` (one unity build: `obervave.c` includes `platform.c` first), `tests/host.c` (the fake FM-1).
- It is **not** Felucca's UI or engine stack: no ui.c / seq.c / storage.c. App API (platform.c header):
  `static void app_audio(int32_t *out, uint32_t n)` (the audio ISR, Q15 stereo, n = 256) and `static void app_main(void)`,
  which never returns and calls `platform_poll()` every pass.
- The SMK-37 board layer came from genkigenki's sloop-fm1 smk37 branch (`BOARD_SMK37`). That build is not needed for FM1VST.

## 3. Host builds
- **Native host sim**: `tests/host.c`, included by `platform.c` when `OBV_HOST` is set (`#include "../../tests/host.c"`).
  It is headless and script-driven: it writes PPM screens and a WAV. Build:
  `cc -O2 -DOBV_HOST -Ifirmware/src -Ifirmware/gen -o build/host/obervave firmware/src/obervave.c` (`tests/run_host.sh`).
  - Clock: `platform_poll()` is one millisecond. It does `fm1_ms++`, applies the script events, and renders the audio
    due in 256-frame blocks (`app_audio`), all inside the app's main loop.
  - Panel: `fm1_in.notes` (27 key bits, 0 = F3), `fm1_in.buttons` plus `host_edges` / `host_rel` (the
    `fm1_input_edges(&released)` API), `host_enc[7]` (`fm1_enc_take`), LEDs as `fm1_led_key(id, on)` (id 0..13 buttons,
    14..40 keys) plus a glow layer `fm1_led_key_glow`, `fb[240*240]` RGB565 in native byte order (`lcd_blit` swaps the
    panel's bytes back), MIDI in `mq[64][3]` (`midi_get`), MIDI out `midi_send` (it printfs).
  - Button order (`platform.c` BTN_*): OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC. This differs from
    `EMU_B_*` (FX SEL ENV ... OCT- OCT+), so a remap is needed. Encoder order: SELECT ALGO K1..K4 PRESETS (`EMU_E_*` is
    SELECT ALGO PRESETS K1..K4).
  - MASTER: on the device the ISR scales by `master_q12` from the ADC. host.c leaves it at unity, and sim.c applies its
    own.
- **WASM sim**: `web/sim/` (`sim.c`, `sim.js`, `worklet.js`, `index.html`) with the built output committed in `docs/sim/`
  (`obervave.wasm`). It is built with `zig cc --target=wasm32-freestanding` plus Binaryen `wasm-opt --asyncify`, and is
  the hub's "play in browser" pattern (after Makkuro37 / winampFM). `sim.c` defines `OBV_HOST` and `OBV_WASM`, reuses
  host.c's panel statics and supplies its own `platform_poll()`. It is **the template for an FM1VST glue**: `sim_render`
  calls `app_audio` 256 frames at a time and serves 128-frame quanta; each ms it resumes the main loop once.

## 4. Build steps
- **None generated**: `firmware/gen/{felucca_font.h, obv_tables.h, obv_splash.h}` are committed (`tools/gen_tables.py`
  regenerates the tables, but is not needed). No Pillow, no assets, nothing fetched.
- The version is `APP_VER` from `tools/build.py --release`, default "0.0"; it is not in a source file. The README and
  the commit say 1.5, so the core passes `-DAPP_VER="1.5"`.
- `fm1_add_core` today requires `tools/build.py` with a `generate()` and GEN headers. OberVave has build.py but **no
  `generate()`**, so it needs a "no GEN" variant.

## 5. Blockers for a native macOS build
- `__attribute__((section(".pool")))` in `gfx.c`, `obv_fx.h` and `obervave.c:1456`. Mach-O rejects it, so the upstream
  host build is Linux-only. Fix it from the glue with `-D'section(x)=aligned(4)'` (verified: builds clean), or
  `#define __attribute__(x)` as Felucca's hostsim does.
- The main loop never returns, so the core needs a **coroutine**. The device's `app_main` loops forever and
  `platform_poll()` is its yield point; the WASM build uses Asyncify. For a core: `ucontext`
  (`makecontext` / `swapcontext`, deprecated on macOS but working on arm64 / x86_64) or a ~20-line per-arch context
  switch, about 256 KiB stack. A lock-step thread would break the ABI's "core never starts a thread" rule.
- Inline asm: only `RING_PUBLISH() __asm__ volatile("" ::: "memory")`, which is a compiler barrier and harmless. The
  hardware asm is in hal/, which the host does not build. No CPU1, no pi32v2 intrinsics.
- Integer only (no FPU on the chip), small tables.

## 6. Persistence and audio
- Device NOR layout (`platform.c`, non-SMK): settings + 32 programs at `0xFC000` (one 4 KiB sector, header
  `[magic "DNGY"][len][crc32]`); 8 pattern "blobs" at `0xF4000..0xFBFFF`; 99-program store, 2 sectors at `0x97000`
  (`psect`); OTA staging `0xE0000..0xE4FFF`. Sector format: 12-byte header, payload up to 4084 B.
- host.c keeps these as **RAM arrays** (`host_store`, `host_blob[8]`, `host_psect[2]`). It has no NOR image, and sim.c
  exposes the same arrays as "blobs" for localStorage. A core can either serialise them into a 1 MiB image at the
  device offsets in the device's sector format (and parse them back before boot), or replace host.c's
  `settings_*` / `blob_*` / `psect_*` with `platform.c`'s own `sector_load` / `sector_save` over a NOR image.
- Audio: 44.1 kHz only (tables), stereo int32 Q15, `HALF_FRAMES` 256 (5.8 ms). The core must buffer one 256-frame
  block and serve it as 2 x 128.

## 7. Host sim tried
- Built with Apple clang 1.5 s after adding `-D'section(x)=aligned(4)'`. Without it: 3 errors (the Mach-O section
  specifier).
- Ran headless 0.18 s for the 6.5 s built-in script: `out.wav` (286 k frames, peak 6688, 2 ch 44.1 kHz) and two
  screens (program 01 DARK ARP, the PERFORM page), with MIDI out logged. Works.

## 8. FM1VST glue
- Closest existing glue: **none** of felucca / melodee / choralroot (they are Felucca-UI shaped). The model to copy is
  `web/sim/sim.c`: input functions that poke host.c's statics, plus a per-ms main-loop pass.
- Effort: **M** for the first Dinghy-family glue (coroutine, BTN / ENC remaps, LED id -> `emu_hal.led` / `led_dim`
  through the keymap, fb byte swap into `hal->lcd`, MASTER, NOR shadow of the RAM stores, MIDI out queue in place of
  `midi_send`'s printf). Then **S** for each further Dinghy app.
- Risks: the coroutine (stack size, an app that calls `platform_poll` from nested loops: the splash and the save
  dialogs do); flash semantics differ from the device unless the image shadow uses the device's sector format;
  `OBV_HOST` paths that differ from the device (MASTER, USB audio); no Tier 2 parameter map yet (its 171-line
  `obv_params.h` table would make one easy).
