# RemoteFM1Metadata.json: a firmware's own skin and labels in its .rfm1

A firmware author can give the .rfm1 that FM1VST builds of their firmware a look of its own and their own panel
labels, without building the format themselves: a `RemoteFM1Metadata.json` at the **root of their repository**. When
`fm1_add_rfm1` (cmake/FM1Rfm1.cmake) packs the file it reads that file from the checkout it builds (the submodule at
its pinned commit: never a branch tip, never the network).

The whole file (schema 1) also has a `card` for the RemoteFM1 catalog, which RFM1Manager reads; FM1VST reads `skin`
and `labels` only and ignores `card`.

```json
{
  "schema": 1,
  "skin": {
    "background": "art/skin-portrait.png",
    "landscape": "art/skin-landscape.png",
    "dim": 0.2,
    "theme": { "body": "#1B1B22", "key": "#2C2C36", "knob": "#E8E2D0", "led": "#FF7A2F", "bezel": "#0E0E12" }
  },
  "labels": {
    "buttons": { "FX": "DRUMS", "SEL": "TRACK", "SEQ": "STEPS", "GLO": "SHIFT" },
    "knobs":   { "K1": "CUTOFF", "K2": "RESO", "K3": "DECAY", "K4": "MIX" }
  }
}
```

## skin -> the .rfm1's skin sections

[RFM1-FORMAT.md](RFM1-FORMAT.md) section 8, as given (tools/rfm1_skin.py, run by tools/rfm1_pack.py):

- `background` (portrait, or any shape) and `landscape` (optional): PNG or JPEG, each at most 4 MiB, the longer side
  at most 4096 px. Paths are relative to the repository's root and stay inside it (no `..`, no absolute paths, no
  symlink out). -> `rfm1.skin.background` / `rfm1.skin.background.landscape`, the bytes as they are.
- `dim`: 0 .. 0.8. `theme`: all fourteen roles of RemoteFM1's design/tokens.json (body label bed key hi lo keyText
  knob mark bezel lcd ink led ledOff) or the five a user picks (body key knob led bezel), each `"#RRGGBB"`.
  -> `rfm1.skin` = `{"skin":1,"dim":..,"theme":{..}}`.
- A skin needs an image or a theme; otherwise there is none.

The pack prints each image's path and SHA-256. The manifest is not changed (RFM1-FORMAT.md section 4 names no field
for it).

## labels -> the descriptor's button names and static live labels

tools/rfm1_labels.py writes them as a header that core-glue/common/rfm1_shim.c is compiled with:

- `buttons`: by printed name, FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ (any case). The author's
  entries replace the glue's `button_names` for those buttons (the host is given a copy of the descriptor with them);
  the others keep the glue's.
- `knobs`: SELECT ALGO PRESETS K1 K2 K3 K4 MASTER. They and the buttons are a static table behind `rfm1_labels_epoch`
  / `rfm1_label` ([DYNAMIC-LABELS.md](DYNAMIC-LABELS.md): slots 0..7 and 8..21), served **only while the core has no
  live labels of its own** (no `fm1core_labels_epoch`, or its epoch 0). A core with live labels keeps full control.
- A label: 0..10 characters, upper case, printable ASCII; `""` = the control has no job (drawn blank or dimmed).
  Short labels draw best (up to 5 characters at full size).

## Errors, and the owner's switches

Nothing in this file fails a build. Each bad field (a path with `..`, a missing or too large image, a bad colour, a
`dim` out of range, a label too long, in lower case, not ASCII, for an unknown control) is a warning in the build
output and is left out; the rest is used. A file that is not JSON is one warning and is ignored. `rfm1_check` reports
what a file has (`skin: dim 0.2, theme 5 roles, background 1080x1920 PNG, ..`, `labels: 4 buttons, 3 knobs`).

In the glue, `fm1_add_rfm1(<id> .. NO_SKIN)` leaves the skin out (an image whose licence is in doubt) and `NO_LABELS`
the labels. A firmware without the file, or with both switches, builds exactly as before, byte for byte.

To add a skin to an .rfm1 already built: `tools/rfm1_skin.py add firmware.rfm1 -o out.rfm1 --background bg.png
[--landscape wide.png] [--theme t.json] [--dim 0.2]`, or `--metadata <repo>/RemoteFM1Metadata.json`; `show` lists one.

Tests: `tools/rfm1/rfm1_skin_test.py` and `tools/rfm1/rfm1_labels_test.py --emcc .. --run ..` (ctest `rfm1_skin`,
`rfm1_labels` in an `FM1_RFM1` build).
