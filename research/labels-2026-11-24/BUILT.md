# Built on rfm1-labels (2026-11-24): not published

In `build-labels-out/` of this worktree (build output, not committed), emcc 6.0.13, `-DFM1_RFM1_ONLY=ON -DFM1_PUBLIC_BUILD=ON`:

```
345b964fe93bb7413d1cf5bf5c46ba866f52d81d44551edaed622e016a349d34  arcticscream-2.1.4.rfm1
f4c2be96c8ddcd8f80436651f0a96a6c0ea476816c39aefcdbaf76c0db0ed941  felucca-1.8.2.rfm1
e33672fc37a45dbdb328f2ee5422c273aae4f17db8deab172feb36116530ab31  glitch-2.0.2.rfm1
3285a4239bb0c3d80d4761676f7aee7b680594a5bbb9fa95ca1c2d43bd3298aa  grimm-2.0.2.rfm1
717d4e97767d3928144e6c8754f8c8362cddc3c89346fb01872db5fe657f9a5a  melodee-1.6.1.rfm1
```

Each passes `tools/rfm1_check.py` ("readouts (rd1)"). Tests (ctest): `rfm1_rd_melodee`, `rfm1_rd_felucca`, `rfm1_rd_grimm`,
`rfm1_rd_glitch`, `rfm1_rd_arcticscream` (knob turns, the readout's control, name and value text expected exactly),
`rfm1_<id>`, `rfm1_labels`: all pass. Sound: the same run (knob turns, page presses, the chord) on the new file and on
the released file (RFM1Manager/cache/release/current) gives bit-identical PCM for all five (and for melodee / felucca
also against this branch's build without rd1). Readout text checked against the screen (`--screen`) for Melodee's
Prophet and FM6 HOME, Felucca's ANALOG HOME and GR!MM's ENV page.

Example (`rfm1_run --readouts`), Melodee: `"HOME CUTOFF" "35 %"`, then ALGORITHM to part 2 (an FM6): the knobs' labels
become `E3 MLVL E4 MRAT E5 MEG E7 DTUN`, `"E3 MLVL" "+1 %"`. Felucca: `"E5 CUT" "412 Hz"`, `"Tempo" "121 BPM"`.
GR!MM: `"ATTACK" "7 ms"`, `"TRACK" "T2"`, PRESETS `"SOUND" "MONK DRONE"` (a preset readout), the page buttons named by
the page a tap opens (`TAPE SCALE ENV LFO SOUND SONG MIXER 1 SOUNDS ARP GRID`).

The Move: this repo builds no .rfm1m. remoteFM14Move's tools/build_fw.sh turns on its rd1 glue when the module exports
rfm1_readout, so the Move build of these files needs nothing new there (not run: that checkout is in use).
