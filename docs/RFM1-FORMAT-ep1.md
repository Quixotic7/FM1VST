# Text for RemoteFM1's RFM1-FORMAT.md: engine parameters (ep1)

This file is not part of FM1VST's documentation proper. It holds the text RemoteFM1's format document
(`RFM1-FORMAT.md` in the RemoteFM1 repository, which deploys a website) should get for ep1, written here so that
repository is not edited from this branch (BUILD-PLAN.md's "RFM1" row, stage 1). Paste it in as its own section after
the takeover's (tk1), and add the row of section 4 below to the manifest's `wants` list.

---

## Engine parameters (`ep1`, optional)

A module may describe its sound engines' parameters to its host, by id, so a host can show them on its own controls
and set them absolutely, timed with the audio (RemoteFM1 on the Ableton Move: the ENGINE knob page). The protocol is
engine-params SPEC.md ("ep1"); FM1VST builds it with `fm1_add_rfm1(... WANTS ep1)` from a firmware's tables
(`core-api/rfm1_ep.h`).

### Exports

| Export | Signature | What it does |
| --- | --- | --- |
| `rfm1_ep` | `() -> ptr` | the EP1R record's offset (0: no engine parameters). Called once after `rfm1_init`; the record never moves |
| `rfm1_ep_set` | `(op, layer, pid, value) -> i32` | op 1 SET: the parameter's value (clamped to its range), as the firmware's own editor writes it, without drawing anything. 0 done; -1 no such layer or pid, or not on the layer's engine now; -2 a LOCK on a NOLOCK parameter; -3 an op the module does not take yet (2 LOCK, 3 FREE, 4 FREE_ALL: a later version) |
| `rfm1_ep_watch` | `(layer, page) -> ()` | the page (an index into the description's pages, -1 none) whose eight cells the record keeps |

All three or none. The manifest's `wants` then lists `"ep1"` (and lists it only then). Hosts that do not know ep1
ignore the exports; the module runs everywhere. Every call comes from the module's thread, between device
milliseconds, never during `rfm1_audio`, like `rfm1_midi_in`.

### The record (EP1R, 264 bytes, little-endian)

| Off | Type | Field |
| --- | --- | --- |
| 0 | u16 | version: 1 |
| 2 | u16 | size: 264 (readers accept larger) |
| 4 | u32 | desc_seq: moves when the description is made again |
| 8 | u32 | desc_ptr: the EP1D description |
| 12 | u32 | desc_len (at most 65536) |
| 16 | u32 | lay_seq |
| 20 | u8 | nlayers (1..8), then 3 zero bytes |
| 24 | 8 x 4 | each layer: u8 engine (an index into the description's engines, 0xFF none: empty or off), u8 MIDI channel (0..15, 0xFF none), u8 flags, u8 0 |
| 56 | u32 | val_seq |
| 60 | u32 | val_ptr: `int16 val[nlayers][nparams]` in the description's parameter order; -32768 where that layer's engine has no such parameter |
| 64 | u32 | cell_seq |
| 68 | u16 | the watched page (0xFFFF none) |
| 70 | u8 | the watched layer, then a zero byte |
| 72 | 8 x 24 | the watched page's cells, knob 1..8: u16 pid (0 none), char name[11], char text[11]: the parameter's name and its value's words as the firmware's own screen says them now |

Each part's seq moves (+1, never back to 0) after its data is written and only when something in it changed. The
module refreshes the record after every `rfm1_frame` and every `rfm1_ep_set`, so a host reads it after those calls
with no call of its own.

### The description (EP1D)

Binary, little-endian, packed, at most 64 KiB, in this order: a 32-byte header (`"EP1D"`, version 1, header size 32,
nparams, nengines, nlayers, npages, ntables, the total length, an FNV-1a 32 of everything after the header), the
layers (16 bytes each: a name of at most 10 printable ASCII characters, flags), the engines (16 bytes: an id stable
across versions, a name, its page count and first page; an engine's pages are consecutive), the pages (32 bytes: an id,
a name, eight parameter indices or 0xFFFF, a group), the parameters (32 bytes: a pid 1..0xFFFE whose meaning never
changes, a name, flags BIPOLAR LIST DYN NOLOCK SOUND GLOBAL RETIRED, min, max, def, a format INT / FIXED / TABLE / NOTE
/ PERCENT / ONOFF and its argument, a table index, a unit of at most 4 characters, a step, the MIDI CC that reaches it
or 255), the tables (8 bytes: the first string's offset, a count, the value of the first) and the strings
(NUL-terminated, at most 10 characters each). The field-by-field layout and how a host draws each format are
engine-params SPEC.md section 3 (with SPEC-NOTES.md's corrections); FM1VST's `tools/rfm1_check.py` checks a module's
record and description with `--run`.

### The manifest's `wants`

| Name | The module |
| --- | --- |
| `ep1` | exports `rfm1_ep`, `rfm1_ep_set` and `rfm1_ep_watch` (engine parameters) |
