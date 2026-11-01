# core-glue/ripple: Ripple, the first Fantasy CFW

| core | firmware | submodule | licence |
|---|---|---|---|
| `ripple` | Ripple 0.4 (Charles Vestal) | `cores/fm1-ripple` @ v0.4 (34956a1), with `base` = fm1-base @ 2d8ed74, `base/bsp` = felucca-bsp @ 773b0b9 | MPL-2.0; Lua 5.4.7, Plaits / stmlib, the currents SDK, the MIDI effects: MIT |

Ripple runs droplets (Lua) that play currents (Faust, C, C++). Here it is a host-only `.rfm1`
([docs/VIRTUAL-CARTS.md](../../docs/VIRTUAL-CARTS.md) section 7): `fm1_add_rfm1(ripple VIRTUAL NEEDS fs1 FOLDERS
code=r data=rw MEMORY_MIB 16)`. Its droplets come from the shared RemoteFM1 folder: `RemoteFM1/code/ripple/droplets/*.lua`
and `RemoteFM1/code/ripple/lib/*.lua`.

- **The build path:** fm1-base's glue (`../fm1base/fm1base_hal.h`, `../plat/plat_glue.h`) on Ripple's emulator path,
  `PLAT_HOST` + `PLAT_WEB`, as `base/tools/build.py --web` compiles it: every built-in current compiled in and found by
  name (flow.c's web lookup; `core_ripple.cmake` writes the registry and the wrapper units), the 20 Plaits currents
  included; no native `.cur` files (JieLi code), one core, no USB disk mode, no managers' SysEx.
- **No coroutine:** plat_glue.h's step function, one pass per device ms. Lua's errors are the module's only Asyncify
  user (`../common/lj_asyncify.h`, `.c`; `tools/rfm1/rfm1_link.cmake` adds them when a unit calls `lj_throw`), so
  nothing can yield while Lua frames are on the stack.
- **Overlays** (`core_ripple.cmake`, checked at configure time, MPL headers kept, a clang VFS overlay): `app/app.c`
  (the list's "disk mode" is "reload folder"; "empty the disk" reads the folder again) and `app/lua/luaport.h` (Lua's
  errors on Asyncify in the `.rfm1`, `LUAI_MAXSTACK` 20000, the count hook at every new state). Lua's heap is 1 MB
  (`RIPPLE_HEAP_KB=1024`; the FM-1's is 112 KB).
- **Runaway droplets:** `ripple_lua_hook.c`, a count hook: 5,000,000 Lua instructions a main-loop pass, then "droplet
  took too long" in the droplet (its error screen), raised again until the pass ends so a droplet's own `pcall`
  cannot keep it alive.
- **The folder sync** (`core_ripple.c`, `rs_*`): at power-on, when the host's `fs_changed()` moves, or on "reload
  folder", a state machine driven from the main loop's passes lists `/code/droplets` and `/code/lib`, reads each
  `*.lua` (64 KB at most) and makes the disk's `/droplets` and `/lib` match: written where the bytes differ (8.3
  names, as the emulator's `rp_web_put`), deleted where the folder has none. `/data` (`data.save`) stays on the
  disk, which is in the flash image: saves carry it. No folder: the disk is left alone, silently.
- **The built-in volume** (`ripple_gen.py`): the shipped droplets and a stand-in `.cur` (the header alone) per
  compiled-in current, its kind, voices and state size filled in at power-on from the current's descriptor
  (`current.list()`, `current.fits()`).
- **The test** (`ctest -R rfm1_ripple`): `test_folder/` (four droplets of ours and a library): boot, the folder read,
  `a1tone` started (Plaits through the delay, a `require` from lib), the chord sounds.

`conformance_ripple.txt` (the native plugin build's tests) has not been run yet. Natively the file API answers
FS_ENOFOLDER (`../common/rfm1_host.c`) until FM1VST hosts the folder.
