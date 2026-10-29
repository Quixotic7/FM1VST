<!-- A copy: the original is RemoteFM1IOS's docs/RFM1-FORMAT.md (github.com/Quixotic7/RemoteFM1IOS, at 72b9d13).
     Keep the text identical; change the original first. -->
# The .rfm1 firmware file (version 1)

An `.rfm1` file is an FM-1 firmware built to run in a host app (RemoteFM1 for iOS first), not on the FM-1. It is the
firmware's host build (the fm1core ABI, as FM1VST's core glue makes it) compiled to **WebAssembly**. The host runs it in
an interpreter, so the same file runs on any platform, and each instance gets its own memory.

A firmware author can add an `.rfm1` build to their releases (FM1VST's `fm1_add_rfm1`: its `docs/RFM1.md`). Users download
the file and put it in the app's firmware folder.

## 1. The file

A WebAssembly binary module (it starts with `\0asm`, version 1) with:

- **A custom section named `rfm1`**: the manifest, UTF-8 JSON (section 4). Required. A module without it is not an
  `.rfm1`.
- **Optional custom sections `rfm1.credits`** (UTF-8 text: the credits a host shows in its About screen) **and
  `rfm1.license.<n>`** (n = 0, 1, …: licence texts, UTF-8, the first line the licence's name).
- The exports of section 2, the imports of section 3, and nothing else that a host must provide. Hosts ignore
  exports this spec doesn't list (a toolchain's own, e.g. Emscripten's `emscripten_stack_*`, `__indirect_function_table`).

File name: `<id>-<version>.rfm1` (e.g. `felucca-1.6.1.rfm1`). Hosts don't rely on the name.

## 2. Exports

All pointers are offsets into the module's memory (wasm32, 32-bit). Integers are i32 unless marked.

| Export | Signature | What it does |
| --- | --- | --- |
| `memory` | memory | the module's one linear memory |
| `_initialize` | `() -> ()` | optional (a WASI reactor's constructors); the host calls it once, first |
| `rfm1_version` | `() -> i32` | the version of this format the module follows: 1 |
| `rfm1_get` | `(abi) -> ptr` | `fm1core_get(abi)`: the descriptor (fm1core.h) or 0 if the module doesn't speak that ABI |
| `rfm1_layout` | `(i) -> i32` | sizes and offsets of `fm1panel_t` and `fm1core_t` in this module (table in section 5); -1 past the end |
| `rfm1_alloc` / `rfm1_free` | `(n) -> ptr` / `(ptr) -> ()` | memory for strings and buffers the host passes in |
| `rfm1_options` | `(path, no_flash, save, headless) -> ()` | the descriptor's `options` (path: a C string in memory) |
| `rfm1_boot_options` | `(fail, reason, stage) -> i32` | `boot_options`; 1 if the descriptor has none |
| `rfm1_init` | `(demo) -> ()` | `init` |
| `rfm1_tick` | `(ms) -> ()` | `tick` |
| `rfm1_frame` / `rfm1_idle` | `() -> ()` | `frame` / `idle` |
| `rfm1_audio` | `(out, frames) -> ()` | `audio`: `frames` stereo int16 frames written at `out` |
| `rfm1_midi_in` | `(pkt) -> i32` | `midi_in` (a USB-MIDI packet as a u32) |
| `rfm1_midi_out_take` | `(pkt_out) -> i32` | `midi_out_take` |
| `rfm1_halted` | `() -> i32` | `halted`; 0 if none |
| `rfm1_shutdown` | `() -> ()` | `shutdown`; nothing if none |
| `rfm1_flash_stage` | `(bytes, n) -> i32` | `flash_stage` |
| `rfm1_flash` | `() -> ptr` | `flash`: the flash image, `flash_size` bytes |
| `rfm1_flash_dirty` / `rfm1_flash_sync` | `() -> i32` | `flash_dirty` / `flash_sync` |
| `rfm1_stats` | `(shed_out, cpu_out) -> ()` | `stats` |
| `rfm1_ui_info` | `(buf, n) -> ()` | `ui_info` |

Each export calls the descriptor's function of the same name with the same meaning as the native ABI (fm1core.h,
ABI 6). Exports exist because a host can't call a wasm32 function-table index (the descriptor's function pointers)
through an interpreter's public API: the host reads only data out of the descriptor (strings, the panel, the keymap,
`flash_size`, button names), never its function pointers. The descriptor's Tier 2 parameter functions are not
reachable in version 1 (a host ignores `params`).

The host calls every export from one thread at a time, as the native ABI requires.

## 3. Imports

Only these, all from `wasi_snapshot_preview1`; a module that imports anything else is refused:

| Import | Host behaviour |
| --- | --- |
| `fd_write` | stdout/stderr go to the host's log (rate-limited); other fds: EBADF |
| `fd_read`, `fd_close`, `fd_seek` | EBADF |
| `clock_time_get` | the host's monotonic clock. A firmware's sound and screen must not depend on it (its time is `tick`'s) |
| `proc_exit` | the instance halts (as `halted` = 0x100) |

No file system, no network, no randomness, no threads.

## 4. The manifest (`rfm1` custom section)

```json
{
  "rfm1": 1,
  "abi": 6,
  "id": "felucca",
  "name": "Felucca",
  "version": "1.6.1",
  "license": "GPL-3.0-only",
  "source_url": "https://github.com/hugelton/Felucca@<commit>",
  "authors": ["Leo Kuroshita (Hügelton Instruments)"],
  "glue_url": "https://github.com/Quixotic7/FM1VST/tree/<commit>",
  "built_by": "FM1VST <version> fm1_add_rfm1, emcc <version>",
  "memory_mib": 8
}
```

- `rfm1`, `abi`, `id`, `name`, `version`, `license`, `source_url` are required. `id` is `[a-z0-9_]{1,31}` and
  matches the descriptor's. `license` is an SPDX expression. `source_url` points at the exact source the file was
  built from (a commit), so a user can get the source their licence promises them. `glue_url` (optional) points at the
  exact build glue (FM1VST at a commit): for a GPL firmware it is part of the corresponding source too.
- The host shows `name`, `version`, `license`, `authors`, `source_url` and the `rfm1.credits` text.
- A host may refuse a module whose manifest and descriptor disagree on `id`, `name` or `version`.

## 5. `rfm1_layout`

Index → value: 0 `sizeof(fm1panel_t)`; 1–16 the offsets of `fm1panel_t`'s `keys`, `buttons`, `keys_tap`,
`buttons_tap`, `enc`, `master`, `key_vel`, `key_lean`, `led`, `led_play_green`, `lcd`, `lcd_writes`, `btn_id`,
`enc_id`, `enc_dir`, `ready`; 17 `sizeof(fm1core_t)`; 18–33 the offsets of `fm1core_t`'s `abi_version`, `id`, `name`,
`version`, `source_url`, `family`, `license`, `gated`, `flash_size`, `button_names`, `params`, `nparams`, `panel`,
`keymap`, `options`, `credits`. The host checks every value it uses against its own expectation and refuses a mismatch.

## 6. The WebAssembly the module may use

- One memory, exported, **fixed size** (initial = maximum, no `memory.grow`): the host keeps pointers into it. At most
  32 MiB; 8 MiB is typical.
- Features: the MVP plus sign-extension, non-trapping float-to-int, mutable globals, multi-value, and of bulk memory
  only `memory.copy` and `memory.fill`. **Not**: `memory.init`, `data.drop`, `table.*`, passive data segments, SIMD,
  threads/atomics, reference types, tail calls, exception handling, memory64 (wasm3 0.5.0, the interpreter on iOS,
  doesn't run them). Data segments are active, in memory 0.
- Floating point: build with `-ffp-contract=off` so the sound is the same on every host.

## 7. How a host uses it

1. Read the file; check the magic, the `rfm1` section, the imports, the exports and `rfm1_version() == 1`.
2. Instantiate it (one instance per firmware instance: an AU instance, the app's player). Call `_initialize` if
   exported.
3. `rfm1_get(6)`; check `rfm1_layout`; read the descriptor's data.
4. Boot. From a fresh flash: `rfm1_flash_stage(0, 0)`; `rfm1_options(p, 0, 0, 1)` with `p` the string
   `"fm1core:staged"` placed in memory with `rfm1_alloc`; `rfm1_boot_options(-1, 0, -1)`; the panel's `ready` = 2;
   `rfm1_init(0)`. From a saved flash: stage its bytes with `rfm1_flash_stage` instead of (0, 0). Then drive it exactly
   as a native core: `tick` / `audio` / `frame` / `idle` on the native ABI's clock, panel reads and writes in linear
   memory.
5. Saved state (an AU's `fullState`): the firmware's id, version and the file's SHA-256, plus the flash image as for a
   native core. Restoring with a different file of the same id: the host warns and boots the flash anyway; the
   firmware decides what its flash means.
