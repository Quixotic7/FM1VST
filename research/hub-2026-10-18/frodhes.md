# Frodhes 0.9.2 (leonardoMirabal/frodhes @ v0.9.2, e49a49d)

Source: hubsrc/frodhes. Host build tried: hubbuild/frodhes.

## 1. License, tag, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` says GPL-3.0-only; 201 SPDX `GPL-3.0-only` files, plus 1 Apache-2.0 and
  2 MIT. Those are all inside `docs/model/research/sources/`, a vendored copy of Felucca and other references for the
  physical-model research. They are not built.
- Tag v0.9.2. Package identity FM-1_958.
- 1242 files, of which 1076 are under `docs/model/research`. Firmware proper: about 30 files and **~10.7 k C lines** (the
  444 C/H files and 136 k lines counted repo-wide are mostly the research copies).

## 2. Lineage
- **Dinghy platform** (zvenson). LICENSING.md: "The wiring `platform.c`, the host mode `tests/`, `docs/API.md` ...
  (Dinghy) zvenson"; "The app `firmware/src/frodhes.c` (derived from Dinghy's app), the engine `rhodes.c`: Leonardo
  Mirabal". Platform files (hal/, loader/, usb.c, ota.c, ...) are Felucca's with SLOOP's changes.
- The same app API as OberVave: `app_main()` never returns and calls `platform_poll()`; `app_audio(int32_t *out,
  256)`. Host switch: `DINGHY_HOST` (OberVave renamed it `OBV_HOST`).

## 3. Host builds
- **Native host sim**: `tests/host.c` (`DINGHY_HOST`). It is headless and script-driven (`tests/scripts/*.txt` with
  expects), and writes PPM screens and a WAV. Build: `make host` / `cc -O2 -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen
  -o build/host/frodhes firmware/src/frodhes.c -lm`. `platform_poll()` is one ms (script events, audio blocks) and
  **calls exit(0) at the script's end**. There is no guard to replace it (unlike OberVave's `OBV_WASM`).
- **WASM sim**: `web/web_hal.c` + `web/build.sh` (Emscripten `-sASYNCIFY`, `emscripten_sleep(1)` in `platform_poll`).
  `platform.c` includes it under `DINGHY_HOST` + `DINGHY_WEB`. Exports: `web_audio(blocks)` (256-frame blocks,
  MASTER applied), `web_btn`, `web_keys`, `web_enc`, `web_midi`, `web_master`, `web_fb`, `web_leds`, `web_store`. A live
  emulator is at leonardomirabal.github.io/frodhes.
  - **This is the cleanest route for a core**: build with `DINGHY_HOST` + `DINGHY_WEB` natively and give the unit a stub
    `<emscripten.h>` from the glue's include path: `EMSCRIPTEN_KEEPALIVE` empty, `emscripten_get_now()` returns the
    simulated ms, `emscripten_sleep()` is the coroutine yield; plus `#define main frodhes_web_main`. No overlay of
    `tests/host.c` is needed.
- Panel (both shims): `fm1_in.notes` 27 bits, `fm1_in.buttons` + `host_edges` / `host_released`, `host_enc[8]`,
  `fm1_led_key(id)` (`host_led[64]`) + `host_glow[64]`, `fb[240*240]` native RGB565. BTN order OCT- OCT+ FX SEL ENV LFO
  EDIT GLO HOME SAVE ARP SEQ PLAY REC; ENC order SELECT ALGO K1..K4 PRESETS (Dinghy's). The page table in the README
  uses K1..K4 per page.

## 4. Build steps
- **Nothing generated at build time**: `firmware/gen/{felucca_font.h, fx_tables.h, rhodes_tables.h}` are committed
  (`tools/gen_rhodes.py`, `gen_fx_tables.py` regenerate them with numpy; not needed). No assets, nothing fetched.
- `tools/build.py` has no `generate()`, so `fm1_add_core` needs a no-GEN option. Version: `APP_VER` from
  `tools/build.py --release` (default "0.0"); pass `-DAPP_VER="0.9.2"`.

## 5. Blockers for a native macOS build
- None for the host build: `gfx.c` and `fx.c` already put their `section(".pool")` behind `#ifdef DINGHY_HOST` ("Mach-O
  has no such section"). It built with Apple clang with no warnings under `-Wall`.
- The never-returning main loop: the same **coroutine** need as every Dinghy app.
- `platform.c` asm and `bootguard` `.noinit` are in the device branch only. No CPU1, no pi32v2 intrinsics.
- Integer DSP (physical model: hammer / tine / tonebar / pickup) with committed coefficient tables.

## 6. Persistence and audio
- Device NOR: settings record (<= 256 B) at `0xFC000` (Dinghy sector header magic / len / crc32); user preset banks
  `PR_BANK_A 0x97000` / `PR_BANK_B 0x98000` (A/B); pattern recorder `SQ_A 0x99000` / `SQ_B 0x9A000`
  (`APP_FL_RANGE 0x97000..0x9AFFF`).
- Under `DINGHY_HOST` the app itself switches to RAM: `presets.c` `host_bank[2][4096]`, `seq.c` `sq_host_fl[2][4096]`,
  and the settings in `host_store` / `web_store_buf`. A core must shadow these four into a 1 MiB image at the device
  offsets (device format, so a flash dump from a real unit loads) and refill them from a staged image before
  `app_main`.
- Audio: 44.1 kHz, stereo Q15 int32, 256-frame blocks. MASTER is applied by the shim (`web_master_q12`). MIDI out is
  **dropped** in web_hal.c (`midi_send` no-op) and printf'd in host.c, so a glue wanting MIDI out must redefine
  `midi_send` (it is in the shim, so only the overlay route can).

## 7. Host sim tried
- Built in 1.3 s, no diagnostics. Ran `tests/scripts/demo.txt` in 0.14 s: `demo.wav` (668 k frames, peak 17324), two
  PPM screens. Works headless.

## 8. FM1VST glue
- Closest existing: none. It would use the same **Dinghy glue** as OberVave / FM-106 (sim.c-style input poking,
  per-ms coroutine pass, 256 -> 2 x 128 audio, LED id -> keymap, fb byte swap).
- Effort: **S** once a Dinghy glue exists (the stub-emscripten.h route), **M** if it is the first.
- Risks: the RAM-only stores under `DINGHY_HOST` (the NOR shadow must use the device's sector formats, or the plugin's
  saves will not match a unit's); MIDI out lost on the web route; `APP_FL_RANGE`-style app-specific sectors differ per
  Dinghy app, so the shadow is per firmware. 0.x status ("0.9.2"), but a release with notes.
