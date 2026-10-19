# core-glue/common: what every core's glue uses

A core is a firmware compiled as a loadable module behind `core-api/fm1core.h` (ABI 6; the host loads ABI 5 too). Its glue lives in
`core-glue/<family>/` and translates between the firmware's own HAL and the ABI. This folder is the part every glue
shares. Every core is built with `core-api/` and this folder on its include path, `nor.c`, `fifo.c` and `coro.c`
linked in, and the version defines of `version.h` set (cmake/FM1Cores.cmake).

## The ABI in one paragraph

The host loads the module, reads the descriptor (`fm1core_get(FM1CORE_ABI)`), calls `options` (the flash: a file,
`FM1CORE_FLASH_STAGED` after `flash_stage(bytes)`, or RAM only), `boot_options` (optional), sets `panel->ready = 2`
and calls `init`. Then the clock runs, one thread, in this order: `tick(ms)` once per device ms, then `frame()` at
ms 0 and every 15 ms or `idle()` otherwise, then `audio(stereo, 128)` for each block due at 44100 Hz. The host writes
the panel's input fields (`keys`, `buttons`, the taps, `enc[]`, `master`, `key_vel[]`, `key_lean[]`) between calls.
The core reads them at the top of its next tick / frame / idle and has written its output (`led[col][line]`
brightness 0..255, `led_play_green`, `lcd[]` + `lcd_writes`) when init / frame / idle return. A firmware's `exit()`
halts the core (`halted()`), it never ends the host. The flash is a 1 MiB RAM image the host reads (`flash()`,
`flash_dirty()`, `flash_sync()`).

## The helpers

| file | what | API |
|---|---|---|
| `nor.{h,c}` | the 1 MiB NOR: (A) the image itself for a HAL without one, (B) the staged boot under a firmware's own stdio flash | A: `fm1nor_options(path, no_flash, headless, default)`, `fm1nor_open()` (power-on), `fm1nor_close()`, `fm1nor_read / erase / erase_range / prog / prog_span / write / written`, `fm1nor_flash()`, `fm1nor_dirty()`, `fm1nor_stall_take()` (the erase's silent blocks), `fm1nor_set_stall_ms`, `fm1nor_on_erase`. Both: `fm1nor_stage(bytes, n)`. B: `nor_stdio.h` / `nor_stdio_end.h` map `fopen fread fwrite fseek ftell fflush fclose` onto `fm1nor_f*` |
| `exit.h`, `exit_end.h` | `exit()` / `atexit()` inside a plugin | `fm1core_exit(code)` (handlers once, then `fm1core_exit_cleanup`, records `0x100 \| code`, returns; then `fm1core_exit_park` if set), `fm1core_atexit(fn)`, `fm1core_run_exit()` (the descriptor's shutdown), `fm1core_halted()` |
| `fifo.{h,c}` | a firmware's own audio block (any size up to 2048 frames, int16 or int32 Q15 << shift, mono or stereo) served as the ABI's 128-frame int16 stereo, sample for sample | `fm1fifo_init(&f, block, channels, FM1FIFO_S16 / S32, shift, render, ctx)`, `fm1fifo_pull(&f, stereo, frames)`, `fm1fifo_reset`, `fm1fifo_buffered` |
| `ticks.h` | the firmware's clocks from the simulated device ms | `fm1t_set_ms(ms)` (in tick), `fm1t_ms()`, `fm1t_us()`, `fm1t_ticks24()` (24 MHz), `fm1t_us_spin(step)` / `fm1t_ticks24_spin(step)` (for busy-wait sites) |
| `coro.{h,c}` | a coroutine for a main loop that never returns (Dinghy's `app_main`) | `fm1coro_new(fn, arg, stack)`, `fm1coro_resume(c)` (1 yielded, 0 finished), `fm1coro_yield()`, `fm1coro_park()`, `fm1coro_inside()`, `fm1coro_done`, `fm1coro_stack_peak`, `fm1coro_free` |
| `panel.h` | the HAL shapes to and from `fm1panel_t` | `FM1P_EMU_IN / _BOOT / _BOOTED(panel, hal)` (an emu-style HAL), `fm1p_take_key_taps / _button_taps / _enc / _role`, `fm1p_edges` (masks to press / release edges), `fm1p_vel / fm1p_lean`, `fm1p_label_of / fm1p_labels`, `fm1p_panel_default` + `FM1P_KEYMAP_INIT` (the FM-1's factory wiring), `fm1p_leds_planes(panel, planes, levels, n, play_green_pos)`, `fm1p_leds_brightest` + `fm1p_duty_level(mask)` (PWM duty planes: the brightest wins), `fm1p_led_id`, `fm1p_led_pos`, `fm1p_lcd_out` (big-endian, counted), `fm1p_lcd_out_native` (native-endian, compared), `fm1p_emu_hal_t` |
| `env.h` | no environment inside a core: `getenv` is NULL in the firmware unit (include after `exit.h`, before the firmware), so a host's EMU_C1=thread or a host sim's test switch never reaches it | `fm1core_getenv` (as `getenv`) |
| `ppm.h` | the LCD as 8-bit RGB (c * 255 / 31, as the host sims write theirs) and 240 x 240 P6 files, for the tests and the golden checks | `fm1g_rgb_of`, `fm1g_lcd_rgb`, `fm1g_write_ppm`, `fm1g_write_lcd_ppm`, `fm1g_read_ppm` |
| `sysex.h`, `sysex_ota.h` | the managers' SysEx (research/managers-2026-10-19/MANAGERS.md): one request frame kept for the firmware's own service, the replies as CIN 4..7 packets; a firmware's own ota.c for the M-UPGRADE identity | `fm1sx_in_init(&in, accept)`, `fm1sx_in_would_refuse` / `fm1sx_in_pkt` (in midi_in), `fm1sx_frame` / `fm1sx_done` (the service), `fm1sx_send` / `fm1sx_take` (the ring; midi_out_take serves it first), `fm1sx_is_fl` / `fm1sx_is_mupg`; sysex_ota.h: `FM1SX_OTA_C`, `FM1SX_OTA_IN / _OUT / _MS()`, `FELUCCA_ID` |
| `version.h` | what CMake passes in | `FM1CORE_ID`, `FM1CORE_VERSION`, `FM1CORE_COMMIT`, `FM1CORE_FAMILY`, `FM1CORE_LICENSE`, `FM1CORE_GATED_OR_NULL`, `FM1CORE_CREDITS_OR_NULL` (fm1_add_core `CREDITS` / `CREDITS_FILES`: the descriptor's `.credits`), `FM1CORE_SOURCE_URL(repo)`, `APP_VER` (= the version unless DEFINES set it) |

The tests: `tests/common/{fifo,coro,nor}_test.c` (`ctest -R common`; nor_test covers ticks and panel too).

### nor

The NOR's rules hold: an erase sets a 4 KiB sector to 0xFF and queues 45 ms of silent audio blocks (the device's IRQs
are off while it erases; `fm1nor_stall_take()` in the glue's audio returns 1 for each such 128-frame block), a program
only clears bits and must not cross a 256-byte page (-8 otherwise), out of range is -8. A firmware whose store is a
RAM copy written back whole (Dinghy) copies it into the image in the device's sector format: `fm1nor_write`, or edit
`fm1nor_image` and call `fm1nor_written(off, n)` (the dirty counter moves, a file is written through). Felucca and
Melodee use A (their `st_read / st_erase / st_prog` are one-liners); ChoralRoot uses B (its emulator keeps the image).

### exit

`exit()` now returns. Read every `exit()` call site of the firmware: a site written as noreturn (`exit(1); for(;;);`)
spins forever on the clock thread. A coroutine glue sets `fm1core_exit_park = fm1coro_park`: an `exit()` inside the
firmware's main loop then never comes back to it (the coroutine is finished where it stood). Otherwise wrap the site.

### fifo

A 256-frame firmware renders one block on the first of each two ABI blocks and serves the second from the buffer: the
stream is the firmware's, with no drift and no added latency beyond its own block.

### coro

ucontext (makecontext / swapcontext: deprecated on macOS, working on arm64; it compiles for x86_64, untested there
here: no Rosetta on this Mac). `coro.c` defines `_XOPEN_SOURCE` before any header (without it macOS's `ucontext_t` is
too small and memory gets overwritten). A switch pair costs ~1 us (the signal mask is saved). The stack is mmap'ed,
512 KiB by default, with a guard page below it. macOS's makecontext zeroes the whole stack at the first resume. No
threads: the coroutine runs only inside `fm1coro_resume`, on the caller's thread. One level only (no resume from
inside a coroutine). Pattern:

```c
static fm1coro_t *co;
static void main_entry(void *a) { (void)a; app_main(); }      /* never returns */
static void platform_poll(void) { fm1coro_yield(); }          /* the firmware's per-pass poll: one device ms */
static void delay_ms(uint32_t n) { uint32_t t = fm1t_ms(); while (fm1t_ms() - t < n) fm1coro_yield(); }
init:  co = fm1coro_new(main_entry, NULL, 0); fm1core_exit_park = fm1coro_park; fm1coro_resume(co);
tick:  fm1t_set_ms(ms); <the panel's input in>; fm1coro_resume(co);
```

### panel

The LED planes: `fm1p_leds_planes` takes packed row-bit planes (`plane[column]` bit r = the LED on row bit r, lines
0..3 = row bits 1..4) in priority order with a level each: ChoralRoot 44453d0 and Felucca / Melodee here use
`{fm1_led, fm1_led_dim}` = `{FM1P_LED_LIT, FM1P_LED_DIM}`. The newer families: ChoralRoot's mid / dim / faint =
MID / DIM / FAINT, Felucca's glow = DIM and backlight = FAINT, breath = MID or DIM (a choice). A firmware with PWM
levels writes its own 0..255. PLAY's green LED is the LED at `FM1P_PLAY_GREEN_POS` (column 8, row bit 1).

The panel table (`btn_id`, `enc_id`, `enc_dir`) is the firmware's own (Felucca's panel.c `panel` record). A HAL that
takes labels and roles directly (plat.h, Dinghy's BTN_* order) uses `fm1p_panel_default()` and maps its own order to
the FM1P_B_* labels and FM1P_E_* roles.

Velocity: `key_vel[k]` is set before a key's bit goes down; 0 means none given (the firmware's default; a mouse
click). The plugin feeds MIDI note velocity and 100 for a key on Remote FM-1.

### sysex

The device's usb.c keeps one SysEx frame (sx_frame, 640 bytes) for its main loop's services (editor.c / cr_backup.c
ed_service, ota.c ota_service) and sends their replies from a ring ahead of its MIDI out; the emulator and web units
build that plumbing out (FELUCCA_OTA 0). A glue gives each packet to the firmware as before (its own parsers: DX7,
CZ-1, the UBOOT flag) and to an `fm1sx_in_t` whose `accept` filter keeps only the services' frames (FL 7D 46 4C, the
M-UPGRADE 00 32 .., a firmware's own); while one waits, the next frame's F0 is refused (midi_in 0: the host retries
it after its next render), so nothing is lost and nothing else waits. After its frame / idle (main.c's two places)
the glue runs the firmware's service on it, with the firmware's send hook (`ota_wire_send`, `CRB_SEND`, `CZ_SEND`) on
the `fm1sx_out_t` ring. The update itself (F0 22 24 35 7F F7) is never run and the UBOOT key (F0 22 24 35 7D F7) is
ignored: neither halts a core. A firmware's "restart" (ChoralRoot's 72) is `fm1core_exit(0)` once the reply has left
the ring: the host powers the core on again from its flash image. tests/sysex_test.cpp drives every core with its
managers' requests.

## Adding a core

1. The firmware as a pinned, unmodified submodule: `cores/<Name>` (`git submodule add`, then the release tag).
2. `core-glue/<family>/core.cmake` (or `core_<id>.cmake` when a family has several cores), found by the top-level glob:

```cmake
fm1_add_core(<id> ${CMAKE_CURRENT_LIST_DIR}
             SOURCE ${CMAKE_SOURCE_DIR}/cores/<Name>
             UNIT core_<id>.c DESC core_<id>_desc.c
             FAMILY <family> LICENSE GPL-3.0-only            # [GATED "the reason"] [CREDITS "..."] [CREDITS_FILES f..]
             GEN ui_fonts.h ...                              # or NO_GEN; [GEN_ROOT felucca-base] [EXTRA_GEN "tools/x.py"]
             VERSION 1.5                                     # or VERSION_FILE / VERSION_REGEX
             FLAGS "-Dsection(x)=aligned(4)")                # (if the firmware has a .pool section attribute)
```

   cmake/FM1Cores.cmake documents every option (also `INCLUDES`, `DEFINES`, `EXTRA_SOURCES`, `GEN_PYTHON`,
   `GEN_OUTPUTS`, `PYTHON_NEEDS raqm numpy`, `SUBMODULES_RECURSIVE`).
3. The unit, `core_<id>.c` (compiled `-w`): `version.h`, `exit.h`, the firmware's sources (and its HAL on the host),
   `exit_end.h`, then the glue: `fm1panel_t fm1core_<p>_panel;` and the entry points `fm1core_<p>_*` (copy the input
   in at the top of tick / frame / idle, the LEDs and the LCD out after init / frame / idle; nothing runs once
   `fm1core_halted()`; audio is silence then). Copy `core-glue/felucca/core_felucca.c` for an emu-style HAL.
4. The descriptor, `core_<id>_desc.c` (`-Wall -Wextra`, no firmware header): copy `core-glue/felucca/core_felucca_desc.c`;
   `.id = FM1CORE_ID`, `.family = FM1CORE_FAMILY`, `.license = FM1CORE_LICENSE`, `.gated = FM1CORE_GATED_OR_NULL`,
   `.credits = FM1CORE_CREDITS_OR_NULL`, `.version = FM1CORE_VERSION`, `.source_url = FM1CORE_SOURCE_URL("https://github.com/...")`, the 14 button names
   (the firmware's own labels, FM1P_B_* order), `.params = NULL` for a new core (every knob relative).
5. `core-glue/<family>/conformance.txt` (or `conformance_<id>.txt`) for `tests/core_conformance.cpp`: what a core
   without a parameter map needs to show a flash round trip (`save` lines, a `saved` check, or `flash none <reason>`),
   a sound prelude if a key alone is silent (`sound btn PLAY 100`), the reboot (`reboot-guard N`, a `reboot`
   script). The directives are in the test's header.
6. Build: `cmake --build build -j` makes `build/cores/<id>.fm1core` and puts it into the bundles; `ctest -R <id>` runs
   `core_smoke_<id>`, `param_test_<id>` and `core_conformance_<id>`.

## Pitfalls (found while moving the three cores to ABI 5)

- **Macros in the glue leak into what follows.** The params map is included after the glue code in the same unit:
  a `#define GP ...` in the Felucca glue replaced Felucca's own `GP[]` table and crashed at load. Prefix every glue
  macro and static `fm1core_`.
- **Include order with a firmware's own `emu_hooks.h`**: define `FM1CORE_NO_EMU_NAMES` before `fm1core.h` (or include
  the firmware first): both define `EMU_LCD_W`, `EMU_B_*`, ...
- **The input at the top of frame and idle too**, not only tick: ChoralRoot's frame reads `emu_hal.master`, and the
  reference emulator's scripts set MASTER between a tick and its frame. The replay tests are bit-exact only so.
- **`init` and MASTER**: a firmware sets its default MASTER at init when the host left 0: copy the panel into the HAL
  before init and the HAL's MASTER (and the panel table) back after (`FM1P_EMU_BOOT` / `FM1P_EMU_BOOTED`).
- **GEN_PYTHON must not contain ';'** (a CMake list separator: `import build; build.generate()` silently ran
  `import build` only). The default is `__import__('build').generate()`.
- **Generators with Pillow + libraqm**: `/bin/sh` strips `DYLD_*` under SIP, so fm1_add_core runs them through
  `cmake -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib`. Check with `PYTHON_NEEDS raqm`.
- **The generated headers go into the submodule's own `build/gen`** (gitignored there), shared by every build tree.
- **Scripts end at their last command's ms + 15**: a trailing `wait` needs a `quit` after it.
- **Simulated time only**: never read the host's clock in a glue (the runs must be deterministic: the conformance
  test compares two units).
- **Large globals**: the cores build with `-fno-common` (the NOR image and the panel in bss, not `__common`).
- **A rebuilt core relinks the plugin bundles** (`LINK_DEPENDS`), so `Contents/Resources/cores` never keeps a stale
  module; tests that load bundled cores (plugin_test) see the new one.
- **Debugging a crash**: lldb cannot launch processes in this sandbox; the crash report is in
  `~/Library/Logs/DiagnosticReports/<test>-*.ips` (JSON after its first line: the faulting thread's frames).
