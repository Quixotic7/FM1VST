# Faster .rfm1 in wasm3, and native host helpers, design 2026-11-02

RemoteFM1's iOS and Android apps run `.rfm1` firmwares in wasm3, an interpreter (apps may not JIT). That is several
times slower than the native cores, and Rainbow Mode 1.1.5-rb crackles on an A15 iPhone. This note measures where the
time goes, what the shared build and the family glue can win **without touching any firmware's source**, and
specifies an optional set of native **host helpers**. The owner's decisions are at the end; **Qn** marks point to them.

Everything here is on branch `rfm1-speed` (FM1VST). Measured with `rfm1_run` (wasm3 0.5.0, the same interpreter and
flags as RemoteFM1IOS `RFMKit/Sources/CWasm3`) on an M-series Mac, frames every 33 ms as the iOS app draws an
`.rfm1`. "x real time" is device time over wall time: 10x means a tenth of one core.

## 1. Where the time goes

`rfm1_run --profile` gives each export's share of the run and its longest call (in microseconds). `--fprofile N`
(a build with `tools/rfm1_run/wasm3_fprof.patch` and `-DRFM1_FPROF`, and an `.rfm1` built with
`-DFM1_RFM1_NAMES=ON`) samples the wasm functions themselves.

| Firmware | x real time | tick | frame | idle | audio | worst ms (us) |
| --- | --- | --- | --- | --- | --- | --- |
| Rainbow Mode 1.1.5-rb | 6.5x | 0 % | 8 % / 4969 | 91 % / 9770 | 1 % | 9770 |
| JIANT 10.26 | 9.4x | 0 % | 1 % / 758 | 98 % / 2017 | 1 % | 2017 |
| ChoralRoot 0.29 | 18.6x | 2 % | 23 % / 925 | 5 % | 70 % / 383 | 1032 |
| AMB-1 1.3 | 21.2x | 94 % / 364 | 0 % | 5 % | 1 % | 658 |
| X0X 1.2.0 | 26.5x | 24 % | 4 % / 6679 | 70 % / 784 | 1 % | 6782 |
| Felucca 1.8.1 | 32.7x | 0 % | 4 % / 391 | 93 % / 214 | 3 % | 391 |
| Ripple 0.4 | 44.8x | 74 % / 242 | 1 % | 24 % / 2394 | 1 % | 2395 |
| FoMni 0.4 | 50.8x | 90 % / 344 | 0 % | 8 % | 1 % | 346 |

The export alone doesn't say screen or sound: each family renders its sound in a different export (the Felucca family
in `frame`/`idle`, inside the web unit's millisecond; the plat and fm1base families in `tick`; ChoralRoot in
`audio`). The function samples do:

| Firmware | Screen | Sound | Hot functions |
| --- | --- | --- | --- |
| Rainbow Mode | **71 %** | 27 % | `kid_paint` → `kid_pic_px`, `kid_friends_px`, `kid_hello_line`: every pixel computed by a function call |
| JIANT | **~65 %** | ~15 % | `text_run`, `cv_alpha_hc`, `og_line_a`, `lz_blit`, `bg_outline` |
| ChoralRoot | ~20 % | **~65 %** | `xf_reverb`, `fm6_render`, the audio ISR; `cr_draw`, `cr_text` |
| Felucca | ~8 % | **~70 %** | the audio ISR, `rev_hall`, `analog_osc`, `master_out` |
| AMB-1, FoMni | ~1 % | **~90 %** | `en_render`, `om_render` |

So: **Rainbow Mode and JIANT are screen-bound; the rest are sound-bound.** Three more findings:

- Almost no time is in the C library: `strlen` is 3.9 % of Rainbow Mode (it measures a string per pixel), and
  nothing else reaches 0.2 % in any of the eight. No firmware spends time in `sinf`, `expf` and the like: they use
  their own tables.
- `memcpy` and `memset` are already native. The build has always had `-mbulk-memory`, so they compile to
  `memory.copy` / `memory.fill`, which wasm3 runs as `memmove` / `memset` (`op_MemCopy`, `op_MemFill`).
  RemoteFM1IOS's wasm3 has the same ops (it differs from FM1VST's only by two patches: `calloc` memory and 32 export
  names per function).
- The slowest firmwares overall are sound-bound and gain nothing below: Hortator 2.8x, Obervave 5.4x, GRiMM 5.7x,
  S1VAVE 6.5x, Zen-1 7.1x. Each costs the same per device ms whichever export runs.

## 2. What the build and the glue win

Measured for the 32 firmwares this branch builds (all but Melodee and SLOOP, whose checked-out sources need other
glue). Each `.rfm1` is run before and after; the whole WAV and the final screen are compared byte for byte.

| Change | Where | Gain (x real time) | Output |
| --- | --- | --- | --- |
| **Host-paced frames** (below) | `core-glue/felucca_web/fw_core.h` | **Rainbow +57 %, JIANT +70 %, Caravel +32 %, Ghoulbox +15 %, fmini +11 %**, Felucca +4 % | sound identical; screen drawn at the host's frames |
| **LTO** (`-DFM1_RFM1_LTO=ON`) | `cmake/FM1Rfm1.cmake` | +1 to +6 % (FoMni +5 %, ChoralRoot +3 %, Felucca +1 %) | identical |
| `wasm-opt -O3` again after the link (`-DFM1_RFM1_POST_OPT=-O3`) | `tools/rfm1/rfm1_link.cmake` | 0 % | identical |
| `wasm-opt -O4` | same | 0 % (noise) | identical |
| More inlining (`-O3 -aimfs 20 -fimfs 200 --inline-functions-with-loops`) | same | -10 to +3 %, files up to 12 % bigger | identical |
| `-fno-math-errno` (`-DFM1_RFM1_EXTRA_CFLAGS=...`) | `cmake/FM1Rfm1.cmake` | 0 %: the same bytes (already the default for wasm) | identical |
| `-mbulk-memory` | | already on since the first `.rfm1` | |

Why the flags give so little: **every module already gets `wasm-opt -O3`**. emcc runs Binaryen at the link
(`wasm-opt --post-emscripten -O3 ...`, seen with `emcc -v`), for every core, not only the Asyncify ones. The time is
in each firmware's own loops, which a second pass cannot shorten.

**Host-paced frames.** The Felucca family's web unit (`web/emu/felucca_web.c`, each firmware's own, unmodified) runs
main.c's frame every 15 ms of device time, whichever export the host calls. So it drew at 66 fps while the iOS app
shows 30, and iOS's "the screen waits while audio is low" (`RFMDevice.cpp`, holding `frame()` back up to 200 ms)
could not hold the redraw back: the glue drew in `idle()` too. Now, in `idle()`, a frame that falls due is held
(the glue keeps the web unit's `web_last_frame` 14 ms back), and that millisecond runs main.c's wait loop
(`ui_input`) instead. The next `frame()` draws it. Results:

- A host that calls `frame()` every 15 ms (`rfm1_run`'s default, the native ABI) sees the same cadence as before.
- RemoteFM1's 30 fps draws half the frames, and a frame iOS defers is really deferred.
- The audio ISR is untouched, so the sound is bit-identical in all 32 tests, keys included.
- The longest single call barely changes (Rainbow: one frame up to ~8 ms, 9.4 ms before). Only splitting the screen
  off the audio thread removes that (SPLIT-CARTS.md, branch `split-carts-doc`).
- `-DFM1CORE_FW_HOST_FRAMES=0` turns it off.

The other families already draw only in `frame()` (plat, fm1base, ChoralRoot), so nothing to change there.

## 3. Native host helpers

**The idea.** A host replaces a few well-known functions of the module with native code of exactly the same result.
The module keeps its own wasm code for them, so a host without helpers runs it as today.

**How, with no new import.** A new import would make every old host refuse the file (they refuse anything outside
WASI at parse time, VIRTUAL-CARTS.md 1). Instead:

1. The module **exports** each helper function under its standard name (`strlen`), as a normal export. Hosts ignore
   exports they don't know (RFM1-FORMAT.md 2).
2. The manifest lists them: `"helpers": ["strlen"]`. A host binds only what the manifest names, so a firmware's own
   function that happens to be exported as `strlen` is never replaced by accident.
3. A host that knows a helper binds its native code to that export **after loading and before compiling** the
   module. In wasm3 that is `m3_FindFunction`, then `LinkRawFunction(f->module, f, signature, native, NULL)`,
   the same call `m3_LinkRawFunction` makes for an import (about 20 lines; `rfm1_run.c` `helpers_bind`). Every call
   to it inside the module then compiles to the native one.
4. The native code checks every byte it touches against the memory's size and traps where the wasm would.

**Version 1: one helper.** `strlen` (`i(i)`). It is the only C library function with measurable time in today's
firmwares (section 1).

| Rainbow Mode 1.1.5-rb, host-paced frames | 30 fps | 15 ms frames |
| --- | --- | --- |
| without helpers (an old host) | 10.1x | 6.1x |
| with the native `strlen` | 10.8x (**+7 %**) | 7.1x (+16 %) |

The output is identical. The gain is bigger than `strlen`'s 3.9 % share because each wasm3 call also costs its
entry (zeroing the callee's locals). Other firmwares gain nothing from it: they spend no time there.

**What helpers cannot do today.** The screen-bound code is each firmware's own (`kid_pic_px`, `text_run`,
`cv_alpha_hc`, `lz_blit`), and so is the DSP. The glue reaches none of it: the web unit's `lcd_fill` / `lcd_blit` and
gfx.c are the firmware authors' files. A graphics module (`fill16`, `blit16`, `alpha_glyph`, RGB565 byte swap)
would pay off only once authors call it from their gfx code, which is a per-firmware change (Q3). So `rfm1_host_gfx`
is **not proposed now**: on today's carts it would measure as zero.

**Possible later candidates** (each its own measured case, the same rules): `memcmp`, `strcmp`, `memchr`;
and, only if authors opt in, the graphics primitives above.

## 4. What each host needs

- **iOS** (RemoteFM1IOS `RFMKit/Sources/RFMCore/RFMWasmCore.cpp`): read `helpers` from the manifest; after
  `m3_LoadModule` and the WASI links, before compiling, bind each known helper as in section 3 (`m3_env.h` for
  `IM3Function`'s module; `LinkRawFunction` is in `m3_bind.c`, not in `wasm3.h`: declare it). Native `strlen`:
  `memchr` from the pointer to the end of memory, trap if no NUL. No change for the host-paced frames: it already
  calls `frame()` at its own rate and at least every 200 ms.
- **Android:** the same in its copy of the wasm host.
- **Web player** (RemoteFM1 `engine.js`): **nothing.** The browser compiles wasm natively, and a JS helper would be
  slower than the wasm it replaces. It ignores `helpers`. (It must keep calling `frame()` at least every 15-33 ms, as
  it does.)
- **FM1VST** (`rfm1_run`): done here (`--helpers`). `rfm1_pack.py` / `rfm1_check.py` should write and check the
  `helpers` field before a release uses it (not done: the prototype binds by export name).
- **Build:** `-DFM1_RFM1_HELPERS=strlen` exports them (`-Wl,--export-if-defined=`).

## 5. Tools added

- `rfm1_run --profile`: each export's calls, share, mean and worst call; the worst device millisecond.
- `rfm1_run --frame-ms N`: frame every N ms (33: the iOS app).
- `rfm1_run --fprofile N`: the N hottest wasm functions, self and inclusive (needs `tools/rfm1_run/wasm3_fprof.patch`
  applied to a copy of wasm3 and `-DRFM1_FPROF`; never in a shipped `rfm1_run`).
- `rfm1_run --helpers`: section 3.
- `FM1_RFM1_LTO`, `FM1_RFM1_POST_OPT`, `FM1_RFM1_EXTRA_CFLAGS`, `FM1_RFM1_NAMES`, `FM1_RFM1_HELPERS`: the build's knobs
  above, all off by default.

## Open decisions (owner)

1. Ship the host-paced frames (Felucca family) in the next RemoteFM1FW release? Sound identical; Rainbow Mode +57 %
   and JIANT +70 % at 30 fps. It changes the files of the eleven Felucca-family carts.
2. Turn LTO on by default (+1-6 %, outputs identical, all 32 tests pass)? Every `.rfm1` changes bytes, so best done
   with a release that rebuilds everything anyway.
3. Native helpers: build `strlen` into the iOS and Android hosts now (+7 % on Rainbow Mode only), or wait until a
   firmware author wants graphics helpers?
4. The five slowest carts (Hortator, Obervave, GRiMM, S1VAVE, Zen-1) are sound-bound: no glue change helps them.
   Ask their authors, or mark them "heavy" in the catalog?
