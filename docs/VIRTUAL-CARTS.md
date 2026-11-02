# Fantasy CFW (virtual carts), design 2026-11-01

A **Fantasy CFW** (our working name: virtual cart) is an `.rfm1` that runs only in RemoteFM1 hosts (RemoteFM1.com, the iOS and Android apps, FM1VST),
never on an FM-1. It is not bound by the device's 128 KB-class RAM or its flash: it may use more memory, more CPU,
and read files from a **shared RemoteFM1 folder**, norns-style: one `audio/` folder every cart reads, so users don't
copy samples per firmware. The first one is fm1-ripple, reading its Lua droplets from that folder.

This adds to [RFM1-FORMAT.md](RFM1-FORMAT.md) (version 1) without changing it. The owner answered the open
questions on 2026-11-01; the decisions are at the end, and **Qn** marks point to them.

## 1. Marking a cart virtual, and how old hosts refuse it

Two signals, one for people and one for machines:

- **Manifest:** `"virtual": true` and `"needs": ["fs1"]` (the host capabilities the cart uses; `fs1` is section 4).
  `rfm1` stays `1` and `rfm1_version()` stays 1: the exports and the boot are unchanged.
- **Imports:** a virtual cart imports the file API from a new module, `rfm1_host_1`. Every host today (iOS
  `RFMWasmCore.cpp`, its Android copy, the web `engine.js inspect()`) refuses a module that imports anything outside the
  six WASI functions, at parse time, before running a byte. So **old hosts refuse cleanly**, with their existing
  "imports something unsupported" error. New hosts read `needs` first and say "needs a newer RemoteFM1" instead.

A cart that uses only the larger limits (section 2) but no files still imports nothing new. It sets `"virtual": true`
anyway, and old hosts refuse it by its memory (over 32 MiB) or, if it fits in 32 MiB, simply run it. That is harmless:
an old host running a cart that fits the old limits is fine.

What hosts and the catalog do with the flag:

- **Hosts:** a "Fantasy CFW" badge on the card and in About. No host has a path that sends an `.rfm1` to an FM-1 today
  (the installers only flash RemoteFM1's own `.fwsc`), so nothing to block yet. Any future "put this on my FM-1"
  action must check `virtual` and refuse.
- **Catalog:** RFM1Manager copies `virtual` from the manifest into `catalog/fw-catalog.json` (SCHEMA.md gets a
  `virtual` field); the site's `build_catalog.mjs` carries it into `catalog.json`; the README table gets a mark
  (like † for hosted gated files). A virtual cart has no `.fwsc` release, so no download link for the device.
- **Build:** `fm1_add_rfm1(<id> VIRTUAL ...)` sets the flag, allows the bigger limits and links the `rfm1_host_1`
  imports (`VIRTUAL NEEDS fs1 FOLDERS code=r data=rw`). `rfm1_check.py` applies section 2's rules by itself when the
  manifest says `"virtual": true`; any other file that imports `rfm1_host_1` fails as today.
- **Wording:** users see **Fantasy CFW** (Q1). (The web player already says "cart" for DX7 cartridges, `cart.js`.)
- **Getting one:** like every other `.rfm1`: "Get Firmwares", download from the catalog (RemoteFM1FW). Q10.

## 2. Resource limits

| | Device `.rfm1` (today) | Virtual cart |
| --- | --- | --- |
| `.rfm1` file | ≤ 64 MiB accepted, a few MB typical | **< 16 MiB** (Q2) |
| Linear memory | fixed, ≤ 32 MiB | fixed, ≤ **512 MiB**, declare only what is needed (Q3) |
| Flash image (`flash_size`) | 1 MiB | ≤ 4 MiB, saved as today (section 6) |
| CPU | real time in wasm3 on the reference phone | same rule, but measured, not assumed (below) |
| WebAssembly features | wasm3 0.5.0 subset | the same subset: iOS and Android still interpret with wasm3 |

- **Memory stays fixed** (initial = maximum): every host keeps pointers into it. Big data does not belong in memory
  anyway; it streams from the folder (section 5).
- **512 MiB is allowed, with a cost to fix in the hosts first.** wasm3 (iOS, Android) `realloc`s the memory and
  `memset`s it to zero (`m3_core.c:132`), so a cart's whole declared memory becomes real RAM at load, per instance,
  even if it never touches it. Host work that comes with this: allocate with `calloc` (or `mmap`), so untouched pages
  cost nothing; and before loading, compare the declared size with what the device can give (an AU extension much
  less than the app) and refuse with "needs N MB of memory" instead of being killed by the OS. The browser already
  maps wasm memory lazily, but mobile Safari may refuse a big one. Authors declare what they use (`MEMORY_MIB`), not
  the maximum.
- **CPU:** a cart is interpreted, 10-25x slower than native. The budget: `rfm1_run` must show at least **2x
  real time on an M-series Mac in wasm3** (so an iPhone 12-class device keeps up). Hosts already read `stats`
  (shed, CPU); a host shows "this cart is too heavy for this device" when it sheds for a few seconds.
- **Runaway code:** a script cart (Lua) must bound each callback itself (Lua's count hook, `lua_sethook`), because a
  host can't interrupt wasm3 or an AudioWorklet mid-call. On the device ripple relied on the watchdog; there is none in
  a host.

## 3. The shared folder

One folder per user, the same layout on every host:

```
RemoteFM1/
  audio/            shared samples, any cart reads (wav, aiff; subfolders allowed)
  code/<cart-id>/   a cart's scripts and assets the user adds (ripple: code/ripple/droplets/*.lua, lib/*.lua)
  data/<cart-id>/   the cart's own files; the only place it writes
```

Inside the cart, paths are virtual and absolute: `/audio/...`, `/code/...`, `/data/...`. `/code` is the cart's own
`code/<id>/` and `/data` its own `data/<id>/` (ripple's droplets: `RemoteFM1/code/ripple/droplets/x.lua` is
`/code/droplets/x.lua` to the cart). `..`, absolute host paths and symlinks out of the folder are refused by the host.

**Access, by default:**

- `/audio`: read for every cart.
- `/code` (own): read. Other carts' code: no.
- `/data` (own): read and write, no quota (Q4).
- Nothing else, no network.

No size limits on the folder: 10 GB of samples is fine. Samples are copied into `audio/` once and every cart reads
them there.

A cart lists what it uses in the manifest, `"folders": {"audio": "r", "code": "r", "data": "rw"}`; the host shows it
on the card ("reads your audio folder"), and refuses an access the manifest didn't list. Writing its own `/data`
needs no prompt: the per-cart sandbox is enough (Q5).

**Where the folder lives:**

| Host | Folder | Notes |
| --- | --- | --- |
| Web (Chrome, Edge) | a folder the user picks: File System Access API, `showDirectoryPicker()` | the handle is kept in IndexedDB; the browser asks "allow" again on a later visit (a click). Reads with `getFile().slice()`, writes with `createWritable()` |
| Web (Safari, Firefox) | no folder picking that persists | fallback: "Add files" (`<input webkitdirectory>`, drag and drop) copies into the site's private storage (OPFS); writes stay there; an "Export data" button (Q6) |
| iOS app | `Documents/RemoteFM1/` | visible in the Files app (the app already has `UIFileSharingEnabled`). Users copy samples there, or AirDrop |
| iOS AU extension | the App Group container | the extension can't read `Documents`, so `RemoteFM1/` lives *in* the App Group and the app shows it in Files through a File Provider (Q7). Then the app's folder is the same one, not a copy |
| Android | inside the Storage Access Framework (SAF) folder the user already picks for States and Backups: `<picked>/RemoteFM1/` | reads with `ContentResolver.openFileDescriptor` (seekable), on a host IO thread. The Android session owns that code |
| FM1VST | `~/Music/RemoteFM1/` on macOS (`Documents\RemoteFM1` on Windows), changeable in the plugin's settings | FM1VST doesn't host `.rfm1` yet: it needs a wasm3 host first (`rfm1_run` is the start). Later (Q8) |

## 4. The file API (`rfm1_host_1`, capability `fs1`)

**Why it's asynchronous:** the web host runs the cart inside the AudioWorklet, and the site has no
SharedArrayBuffer (GitHub Pages can't send COOP/COEP headers). So a cart can't block on a read. The native hosts run it
on a core thread that renders ahead, where blocking would cause dropouts too. Every call therefore returns at once;
the host completes the request between two calls into the module (the host already calls one export at a time) and the
cart polls.

Import module `rfm1_host_1`, four functions (wasm types in brackets):

```c
/* fs_request [i32 i32 i32 i64 i32 i32] -> [i32]; fs_poll [i32] -> [i32]; fs_cancel [i32] -> []; fs_changed [] -> [i32]
   All paths: UTF-8, not NUL-terminated, virtual (/audio/kick.wav). Results < 0 are errors (below). */
int32_t fs_request(int32_t op, const char *path, int32_t path_len,
                   int64_t offset, void *buf, int32_t len);   /* -> request id > 0, or error */
int32_t fs_poll(int32_t id);     /* -> FS_PENDING, or the result (bytes, entries) once; the id is then free */
void    fs_cancel(int32_t id);   /* the host stops writing into buf; poll returns FS_ECANCELED */
```

| op | path | buf / len | result |
| --- | --- | --- | --- |
| `FS_STAT` 1 | file or folder | a 16-byte record: size (u64), mtime (u32 s), is_dir (u32) | 16 |
| `FS_LIST` 2 | folder | packed entries `{u8 is_dir, u8 name_len, name}`, from entry `offset` on, sorted by name (bytes); an entry never split | entries written; 0 = the end |
| `FS_READ` 3 | file | `len` bytes at `offset` | bytes read; 0 = the end |
| `FS_WRITE` 4 | file in `/data` | `len` bytes at `offset` (`offset` -1 = truncate and write) | bytes written |
| `FS_DELETE` 5 | file or empty folder in `/data` | none | 0 |
| `FS_MKDIR` 6 | folder in `/data` | none | 0 |

- **Errors:** `FS_PENDING` -1, `FS_ENOENT` -2, `FS_EACCES` -3 (not allowed by section 3), `FS_EIO` -4, `FS_ENOSPC` -5
  (disk full), `FS_EBUSY` -6 (too many requests), `FS_ECANCELED` -7, `FS_ENOFOLDER` -8 (no folder picked yet, or
  the user hasn't allowed it this visit). Integers in records are little-endian; names up to 255 bytes; hidden
  files (a leading `.`) are not listed.
- **Manifest:** `"virtual": true, "needs": ["fs1"], "folders": {"audio": "r", "code": "r", "data": "rw"}` (only the
  ones it uses). A host refuses a module importing `rfm1_host_1` without `fs1` in `needs`.
- **Ownership:** while a request is pending, `buf` belongs to the host. The host writes it only between export calls,
  never during one.
- **Limits:** at most **32 requests in flight**, at most **1 MiB per request**. Reads are raw bytes: the cart parses
  WAV and AIFF itself, so every host behaves the same, bit for bit. No decoding by hosts (Q9).
- **Folder changes:** `fs_changed() -> i32` is a counter the host bumps when it sees the folder change (a new picked
  folder, files added through the app). A cart rescans when it moves (ripple: the droplet list).
- **Per host:**
  - Web: the worklet posts `{id, op, path, offset, len}` to the page. The page does the File System Access or OPFS
    call and posts the bytes back, as a transferred ArrayBuffer. The worklet copies them into the module's memory
    at the start of the next `process()`.
  - iOS and Android: a host IO thread does the same with a staging buffer, and the core thread copies between ticks.
- **Determinism:** the order requests complete in is the host's. A cart must not let its sound depend on how fast a
  read completes (section 5).

## 5. Streaming large samples

Samples never have to fit in memory. A sample player cart:

1. On load: `FS_STAT` and a `FS_READ` of the header (parse the format, find the data offset). Then read the
   **first ~1 s of every playable sample** into memory (the "attack cache"). A 64-sample kit is ~11 MiB at 44.1 kHz
   16-bit stereo, so it fits.
2. On a note: play from the attack cache at once. Meanwhile request the next chunks (64-256 KiB each) into a per-voice
   ring, 2-3 chunks ahead.
3. If a chunk is late (a slow SD card, a cold network folder), the voice plays silence for it and counts an underrun.
   It never blocks. `stats` reports the underruns, so a host can show "the folder is too slow".

The **web host prefetches**: for `/audio` on the web, the page can read the whole file into a page-side cache on its
first `FS_READ`. Later chunks then answer within one render quantum. Native hosts read from local storage, which is
fast enough.

`rfm1_run` gets `--folder <dir>`, serving the API synchronously but completing each request **one tick late**, so the
test still exercises the polling.

## 6. Saves and the slot scheme

Nothing changes. A host saves what it saves today: the flash image (`flash_size` bytes) with the firmware's id,
version and the file's SHA-256 (the autosave, named states, `.rfm1state` files, the AU's `fullState`).

- What lives in the folder is **not** part of a save: samples, scripts, `/data`. A save is still small and portable.
  It refers to folder files by path. Restored on a device without them, the cart shows "missing: /audio/kick.wav" and
  plays on.
- A cart keeps small state (the selected script, settings, patterns) in its flash image as now, so it moves with a
  state file. Bigger user data (recordings, ripple's per-droplet `data.save`) goes in `/data` when the cart wants it
  shared across states, or in flash when it should follow the state. Each cart decides.
- `flash_size` up to 4 MiB keeps a state file and an AU `fullState` small: every save is a full copy of the flash
  image, so a big image would burn storage fast (Q2). Memory size does not grow saves.

## 7. fm1-ripple as the first cart

**What it is:** fm1-ripple, by Charles Vestal (v0.4, 2026-11-01).
- **Licence:** MPL-2.0 (app, fm1-base, felucca-bsp), with MIT for Lua 5.4.7, Plaits/stmlib, the currents SDK and midifx. Hosting the build is fine.
  The MPL is per file: changed files must stay public, and `source_url` points at our fork's commit.
- **Author intent:** the author already runs it in the browser ("the same code the FM-1 runs"), and Charles is happy
  with the port (Q10). It is published like every other `.rfm1`: in the RemoteFM1FW catalog, "Get Firmwares".
- **On the device:** a FAT12 disk in flash (276 KB) that shows up as a USB drive, with `droplets/*.lua`,
  `lib/*.lua`, `data/*.DAT` and native `currents/*.cur`, plus a read-only built-in volume.

**The port**, smallest change first:
- Build it with `fm1_add_rfm1(ripple VIRTUAL)` from the pinned v0.4 tag. Ripple needs fm1-base 2d8ed74 (disk mode); the
  copies of fm1-base we already have are older.
- **Disk mode becomes a folder sync:**
  - At boot, and whenever `fs_changed` moves, the cart reads `/code/droplets/*.lua` and `/code/lib/*.lua` and
    writes them into its RAM FAT disk, using the path the web emulator already uses for that (`rp_web_put`).
    The droplet list, `require` and `data.save` then work unchanged.
  - The "USB disk" entry in the list becomes "Reload folder".
- **Droplet data:** `data.save` stays on the FAT disk, inside the flash image, so it follows saves (section 6).
- **Samples:** ripple has no sample API today. A `sample.*` Lua table over section 5's streaming would be our
  addition. Not in ripple for now (Q11): ripple gets script support; a separate example sample-player Fantasy CFW
  shows the sample API, and script authors can add sample support from there.
- **Currents:**
  - The built-in currents are compiled in, including Plaits.
  - Native `.cur` files (JieLi code) can't run in a host. The list hides them.
  - Wasm currents, as the web emulator does them, are a later step.
- **Risk (to try first):** Lua raises errors with `setjmp`/`longjmp`.
  - The web emulator builds it with `-sSUPPORT_LONGJMP=wasm`, which uses wasm exceptions. wasm3 can't run those, and the `.rfm1` format forbids them.
  - The plan is a `longjmp` on Binaryen's Asyncify, next to the coroutine support `.rfm1` builds already have (`coro_asyncify.c`): unwind to the `pcall` and drop the stack.
  - **Spike result (2026-11-01): it works.** Lua 5.4.7 (ripple's 32-bit build) with try/throw on Asyncify passes
    pcall, nested pcall, errors in coroutines, xpcall, memory and stack-overflow errors, and 100000 throws with no
    leak, in wasm3 0.5.0, with WASI imports only and no EH opcodes. Every `coroutine.yield` is a throw too. Cost:
    +34 % time in wasm3 on code that doesn't throw, +59 % size (480 KB for Lua alone); throws are 2.6x faster than
    wasm EH. Needs a 64 KB unwind buffer at `LUAI_MAXCCALLS` 80 and a lower `LUAI_MAXSTACK`; Lua frames must never be
    on the stack when an `fm1coro` yields. Code: `luaport.h` + `lj_asyncify.c`; the link adds `lj_throw` to what
    triggers the Asyncify pass.
- **Host order:** the web player first. Its `engine.js` gets the `rfm1_host_1` imports and the page gets a folder
  picker. Then iOS, as the canonical format owner. Then Android and FM1VST.

## 8. Audio input (`ain1`)

Added 2026-11-02 (decided by the RemoteFM1 session; the first user is Elasticat FM-1's INPUT machine and its REC
sampling). Hosts feed **live stereo input** to a cart: the phone's microphone, an audio interface, or on iOS (AUv3)
another plugin's audio through an input bus. The web player comes first; until a host has it, `rfm1_run --audio-in`
is the stand-in.

**Manifest:** a new optional list, `"wants"`: capabilities a cart uses when the host has them and does without
otherwise. Hosts use what they can and ignore the rest; **unknown names in `wants` are always ignored**. A cart that
cannot work without input lists `ain1` in `"needs"` instead (and an old host then refuses it as for any unknown
need). Elasticat: `"needs": ["fs1"], "wants": ["ain1"]`. `wants` is allowed on any cart, virtual or not.

Hosts show "uses audio input" on the card, and ask for the microphone permission only for such carts, and only on a
click (never at load).

**Exports** (both optional; a host calls them only when the manifest lists `ain1` in `wants` or `needs` **and** the
module exports them):

```c
/* [i32 i32] -> [] each */
void rfm1_audio_in(const int16_t *stereo, uint32_t frames);
void rfm1_audio_in_info(uint32_t state, uint32_t latency_frames);
```

- `rfm1_audio_in`: interleaved stereo int16 at 44100 Hz, in a buffer the host allocated with `rfm1_alloc` (like the
  output buffer). The host calls it **immediately before `rfm1_audio(out, frames)` for the same block, with the same
  `frames`**: input frame *i* lines up with output frame *i*. The cart copies or consumes the input during the call.
  A block with no `rfm1_audio_in` before it is silence; a host may stop calling it at any time while the input is off
  (that is not an error). Mono sources arrive duplicated, L = R.
- `rfm1_audio_in_info`: called before the first `rfm1_audio_in` and whenever either value changes. `state` 0 = no
  input (off, permission denied, unplugged), 1 = live. `latency_frames` = the **round trip** at 44.1 kHz (below; 0 =
  unknown).
- **Order per block** while input is on: `rfm1_audio_in_info` (only when something changed), `rfm1_audio_in(in,
  frames)`, `rfm1_audio(out, frames)`. Turning input off, switching firmware or disposing the instance calls
  `rfm1_audio_in_info(0, 0)`; blocks then come with no `rfm1_audio_in`.
- **`latency_frames` is the round trip** (decided 2026-11-02 for every host: web, iOS, Android, FM1VST): a sound
  the cart outputs in output frame *j* comes back (speaker to microphone, or an interface looped back) in input frame
  *j* + `latency_frames`. To line a take up with what the cart played, take input frame *i* as played at output frame
  *i* − `latency_frames`. Each host includes its output side, its input side and any buffering between them; a cart
  adds nothing of its own (not its block). The web player's number: its 256-frame in-page delay + the capture latency
  + the context's baseLatency and outputLatency (RemoteFM1 `web/engine/API.md` section 6). This replaces the first
  wording ("the input side only").
- **Monitoring:** hosts do not monitor the input themselves (unless they say so in their own UI); what is heard of it
  is what the cart plays. A cart that passes the input through (Elasticat's INPUT machine) is the monitor.

**Build:** `fm1_add_rfm1(<id> ... WANTS ain1)` writes the manifest's `wants` and compiles `rfm1_shim.c` with
`RFM1_AIN1`, which exports `rfm1_audio_in` / `rfm1_audio_in_info` calling the core's optional module symbols
`fm1core_audio_in(const int16_t *, uint32_t)` / `fm1core_audio_in_info(uint32_t, uint32_t)` (weak: a core without
them gets no-ops). Without ain1 in `WANTS` or `NEEDS` neither is exported. `rfm1_check.py` accepts `wants` (a list of
names; names it does not know are ignored), checks both exports' signatures when present, and fails a module that
exports either without `ain1` in `wants` or `needs`.

**`rfm1_run`:** `--audio-in F.wav` feeds the WAV (PCM 16 / 24 / 32-bit or float; mono as L = R; played at 44.1 kHz)
through `rfm1_audio_in` immediately before each `rfm1_audio`, the same frames, looped (`--audio-in-once`: once, then
no more calls), after `rfm1_audio_in_info(1, N)` (`--audio-in-latency N`, default 0); `--audio-in-off-at MS` calls
`rfm1_audio_in_info(0, 0)` at that device ms and stops feeding; the order per block is the web player's (info when
changed, then `rfm1_audio_in`, then `rfm1_audio`). A cart whose manifest has no `ain1`, or that does not
export `rfm1_audio_in`, is run without input (said on stderr), as a host would.

**Native cores** (`fm1core.h`): the same two calls become optional descriptor fields at the next ABI bump. Not now:
until then they are module symbols only the `.rfm1` shim sees, and the plugin's native build has no input.

## Decisions (owner, 2026-11-01)

1. Name in the apps: **Fantasy CFW**.
2. 16 MiB caps the `.rfm1` file only. Saves stay small (every save is a full copy of the flash image).
3. Memory: up to 512 MiB, with the wasm3 `calloc` fix and a memory check in the hosts (section 2).
4. No quota on `/data` and no size limits on the folder: samples are copied once into `audio/`.
5. Writes to a cart's own `/data` need no prompt.
6. Safari and Firefox: copy the files into the site's own storage (OPFS).
7. iOS: `RemoteFM1/` lives in the App Group, shown in Files through a File Provider.
8. A `.rfm1` host in FM1VST: later.
9. WAV and AIFF only; hosts don't decode.
10. Charles Vestal is happy with the port. Ripple is distributed like the others: catalog, "Get Firmwares".
11. Ripple: script support first. Then an example sample-player Fantasy CFW; script authors add sample support.
