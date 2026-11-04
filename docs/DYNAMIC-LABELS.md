# Live panel labels

A firmware names the FM-1's 14 buttons once, in its descriptor (`button_names`, read at load). Live labels say, while
it plays, what every control does **now**: SHIFT held, a page, a mode, a step held. Hosts draw them on the panel over
the static names. Nothing changes in the ABI (still 6) or the .rfm1 format (still version 1): a core or a host that
knows nothing of them works exactly as before.

## The model: 49 slots (core-api/fm1core.h `FM1CORE_LBL_*`)

The same slots and meaning as FIZZ's SysEx LABELS reply (FMToneSeq `docs/SYSEX.md`, "`20` LABELS"), so a host has
one model for a core it runs and for a unit that sends them.

| `which` | Controls | Define |
| --- | --- | --- |
| 0..7 | the knobs by role, `FM1P_E_*`: SELECT, ALGO, PRESETS, K1..K4, MASTER | `FM1CORE_LBL_KNOB0` + role |
| 8..21 | the 14 buttons, `FM1P_B_*`: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ | `FM1CORE_LBL_BTN0` + label |
| 22..37 | the white keys W1..W16 (F3 G3 A3 B3 C4 .. G5, ascending) | `FM1CORE_LBL_WHITE0` + n |
| 38..48 | the black keys B1..B11 (F#3 .. F#5, ascending) | `FM1CORE_LBL_BLACK0` + n |

`FM1CORE_LBL_N` = 49; `FM1CORE_LBL_LEN` = 11 (10 characters and the NUL).

A label: 0..10 characters, upper case, printable ASCII.

- `""`: **the control has no job here**: the host draws it blank or dimmed.
- NULL (an .rfm1: pointer 0): **not given**: the host's own. A button: its `button_names` entry, then the printed
  name. A knob: its printed role name (or what the host shows from `knob_target`). A key: nothing.
  (NULL is the one addition to FIZZ's model, where every entry is a string: a core can label just a few controls.)

Short labels draw best: up to 5 characters at the full size; hosts shrink, then cut with an ellipsis.

## A native core: two optional module symbols

Beside `fm1core_get`, found like `fm1core_latency_frames` (dlsym, the plugin's `LoadedCore::symbol`), not in the
descriptor:

```c
FM1CORE_EXPORT uint32_t fm1core_labels_epoch(void);       /* FM1CORE_LABELS_EPOCH_SYMBOL */
FM1CORE_EXPORT const char *fm1core_label(int32_t which);  /* FM1CORE_LABEL_SYMBOL */
```

- **The epoch** moves (any change; it wraps past 0) whenever any of the 49 labels changed. **0 = no live labels
  (yet)**: hosts draw as without the symbols. So start it at 0 and make it 1 once the labels are ready (after `init`).
- **`fm1core_label(which)`**: the label now, or NULL. The string must stay valid until the next call into the core
  (hosts copy it): a static table, or a buffer the core rewrites only from its own clock.
- Both are called on the clock thread (as `tick` / `frame`), never at the same time as an entry point.
- Define them in a unit the core links anyway (beside `fm1core_get`, in the glue): a static-library link pulls a unit
  in only for a strong reference, and the hosts' references are weak (see the web player's `-u` below).

## An .rfm1: two optional exports

`core-glue/common/rfm1_shim.c` gives every .rfm1 built by `fm1_add_rfm1` both, calling the core's symbols through
weak references (a core without them links, and answers epoch 0, label 0):

| Export | Signature | What it does |
| --- | --- | --- |
| `rfm1_labels_epoch` | `() -> i32` | `fm1core_labels_epoch()`; 0 when the core has none |
| `rfm1_label` | `(which) -> ptr` | `fm1core_label(which)`: a C string in the module's memory; 0 = NULL (also for `which` outside 0..48) |

A firmware's `RemoteFM1Metadata.json` labels ([RFM1-METADATA.md](RFM1-METADATA.md)) are compiled into the shim as a
static table for slots 0..21, served only while the core has no live labels (no symbols, or its epoch 0); the epoch
is then the shim's own, moved at every switch between that table and the core's labels and at every move of the
core's epoch. Its buttons also replace the descriptor's `button_names` for those buttons.

Format version 1 unchanged: hosts already ignore exports they don't know (`docs/RFM1-FORMAT.md` section 1).
`tools/rfm1_check.py` checks their signatures when present. A host treats a missing export (a file built before) as
"no live labels", bounds-checks the pointer and caps the length, as for `button_names`.

(`docs/RFM1-FORMAT.md` is a copy of RemoteFM1IOS's: its section 2 needs these two rows, in RemoteFM1IOS first.)

### Building an .rfm1 with them

Nothing to add: `fm1_add_rfm1` compiles `rfm1_shim.c` with the core's units and links the objects directly (no
archive), so a core's `fm1core_labels_epoch` / `fm1core_label` anywhere in its units are found:

```sh
cmake -S . -B build-rfm1 -DFM1_RFM1_ONLY=ON -DFM1_PUBLIC_BUILD=ON -DFM1_ONLY_CORES=<id>
cmake --build build-rfm1 --target rfm1_<id>
python3 tools/rfm1_check.py build-rfm1/rfm1/<id>-<version>.rfm1
```

Check that the file has them: the export list (`wasm-objdump -x`, or RemoteFM1's `inspect(bytes).labels`). By hand,
the same as fm1_add_rfm1: `emcc -mcpu=mvp -msign-ext -mnontrapping-fptoint -mbulk-memory -mmutable-globals -O3
-std=gnu11 -ffp-contract=off -fno-common -DFM1CORE_RFM1=1 -Icore-api -c core-glue/common/rfm1_shim.c` with the core's
objects, linked `--no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sINITIAL_MEMORY=<n> -sALLOW_MEMORY_GROWTH=0
-sEXPORTED_FUNCTIONS=[] -Wl,--gc-sections`, then `tools/rfm1_pack.py` (RemoteFM1's
`web/engines/rfm1/fixtures/build_labels.sh` does exactly this for its test core).

## Hosts

Read the epoch after each `frame()` (or at the UI's rate: 30 Hz is plenty). When it differs from the last one read
(the first read counts), read all 49 labels and redraw. Per control: a live string wins; `""` = no job (dim); NULL =
the static fallback above. The host's "firmware's button names" setting off: everything printed, live labels too.
The printed name stays small beside a renamed button.

- **RemoteFM1 web player**: done (RemoteFM1 `docs/BUTTON-LABELS.md`, "Live labels"; `engine.labels()` in
  `web/engine/API.md`). Its native engine (`wasm_glue.c`) reads the symbols through weak references and links each
  core library with `-Wl,-u,fm1core_labels_epoch -Wl,-u,fm1core_label` so the member defining them is pulled in.
- **FM1VST plugin: to do.** `LoadedCore::symbol(FM1CORE_LABELS_EPOCH_SYMBOL)` / `(FM1CORE_LABEL_SYMBOL)` for a native
  core (and the two exports for an .rfm1 through its WASM host), polled at the editor's timer on the clock's side;
  PanelComponent draws the merge (the buttons, the knob legends).
- **RemoteFM1IOS / RemoteFM1Android: to do.** Their .rfm1 runners call the two exports when present; their panels
  draw the merge. For a unit over USB, FIZZ's SysEx LABELS replies fill the same 49 slots.
