# Built on rfm1-labels (2026-11-24): not published

All in `build-labels-out/` of this worktree (build output, not committed), emcc 6.0.13, `-DFM1_RFM1_ONLY=ON`
(`-DFM1_PUBLIC_BUILD=OFF` for the hosted-despite-gate ones: evilgenius and the SLOOP family, as RFM1Manager builds them):

```
345b964fe93bb7413d1cf5bf5c46ba866f52d81d44551edaed622e016a349d34  arcticscream-2.1.4.rfm1
4c2d1b39504c94f57588ad0e5daa3feefe24c1cfa12c0414d02d7a7feb219402  clipper-1.3.1.rfm1
fabb18c31f24ea593d7a43c3f05e848c247f14a5f9a7378f3c478bb58926f8d3  dungeontracker-1.5.rfm1
ae15fcd0f0dbfd21afe5a0835195f18382160f0672d7a8f043f7b37b23ad52ad  evilgenius-0.1.4.rfm1
f4c2be96c8ddcd8f80436651f0a96a6c0ea476816c39aefcdbaf76c0db0ed941  felucca-1.8.2.rfm1
680d20c87b06139f32aa5b6489e35461362ded9d43af0c23320e4a9e35d72f49  ghoulbox-2.4.rfm1
e33672fc37a45dbdb328f2ee5422c273aae4f17db8deab172feb36116530ab31  glitch-2.0.2.rfm1
3285a4239bb0c3d80d4761676f7aee7b680594a5bbb9fa95ca1c2d43bd3298aa  grimm-2.0.2.rfm1
83bbb4af4e340f1d61023c49a9306b6a8ae42576a046c8723f757f57fd5a39d8  karma-3.1.5.rfm1
717d4e97767d3928144e6c8754f8c8362cddc3c89346fb01872db5fe657f9a5a  melodee-1.6.1.rfm1
708ae3b18be68c573e004e3c3e2d4eb420e9b387f5c94632635714029af74b08  minitone-1.0.5.rfm1
c609ada6c40566b590ab56640581ef9576ec3915d16c4ced8de21d33a39f98bb  mpyre-1.0.3.rfm1
08f945dd767d6c939867e39533d8e002a068384baac484deb29b9863e2511087  orbit-1.0.0.rfm1
5d421b9a4caba50e4b821f7c232001ced1b1645fbdda5b94ee5a30127c6adbdc  sloop-2.8.5.rfm1
587d589f690dfa6e2297e2b183d99e84a4449503603e233e37d853033cc1cc27  stallion-1.1.rfm1
```

And a trial, from Elasticat's own branch (not the pinned tree):
`/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/elasticat-builds/elasticat-0.6.97-readouts-trial.rfm1`,
sha256 `8c0794ce22c9927b836cdf7532b88ad2195d97d3997793fd386b8859341b79a2` (elasticatfm1 branch `labels-readouts`
bd1c75d, worktree `Elasticat-tmp/elasticatfm1-readouts`; built with `-DEC_SOURCE=` that worktree).

## Evidence, per firmware

Every file passes `tools/rfm1_check.py` ("readouts (rd1)"). Each has a ctest `rfm1_rd_<id>`: knob turns (and button
presses) and the readouts they must give, control, name and value text exactly; all pass, with `rfm1_<id>` and
`rfm1_labels`. Sound: the same run (knob turns, page presses, ALGORITHM, the chord) on the new file and on the released
file (RFM1Manager/cache/release/current) gives bit-identical PCM for every one, Elasticat's trial included (with its
audio-in and folder; the folder a scratch copy of core-glue/elasticat/test_folder). Readouts were compared with the
firmware's own screen (`rfm1_run --screen`): the contact sheets and run logs are in `screens/` (each screen with the
readouts that run gave under it).

| file | how | screens checked | test expects (examples) |
|---|---|---|---|
| melodee | Tier 2 map (WANTS rd1); Prophet HOME cells in % | HOME (Prophet, FM6) | `HOME CUTOFF = 35 %`, `Part = Part 2`, `E3 MLVL` |
| felucca | Tier 2 map (WANTS rd1) | HOME | `E5 CUT = 412 Hz`, `Tempo = 121 BPM` |
| grimm, glitch, arcticscream | grimm_rd.h | HOME, ENV, GRID | `ATTACK = 7 ms` (arctic `1.3 s`), `TRACK = T2`, `SOUND = MONK DRONE` |
| evilgenius | FIZZ's adapter (qd_ctl_labels, qd_view_turn, qd_view_edit) | CHNC and NOTE defaults | `CHANCE = 100%`, `CONDITION = !FILL` |
| dungeontracker, ghoulbox, minitone | Felucca's map + per-fork options (fw_map_fork.h) | HOME, ENV, LFO, FX, SCL, ARP, EDIT, GLO held | `Attack = 1.2 ms`, `Part = Part 2`, `Chorus Send = 42 %` |
| sloop, clipper, karma, mpyre, orbit, stallion | sloop_rd.h (the columns as drawn) | HOME, ENV, LFO, FX, SEL, ARP, EDIT, GLO held | `ATK = 1.1 ms`, `REL = 47 ms`; orbit `ROOT = C#` |
| elasticat (trial) | its own RFM1_READOUT_BAR lines | LOOP | `PITCH = +2`, `SLOT = OFF`, `START = 1`, `END = 128` |

What gives nothing (no wrong name, no readout; the host keeps its own labels): karma's HOME loop screen, M-PYRE's
FX / ARP / AMP screens, ORBIT's own ENV / LFO / FX / EDIT screens, Dungeon Tracker's DICE page and EDIT dialog,
GR!MM's step locks (SEL held on SEQ's pages), any layer, menu or REC screen of the SLOOP family.

## To try one on the Move (for whoever makes the Move builds)

The Move does not run an .rfm1 directly: remoteFM14Move's `tools/build_fw.sh` compiles one into an `.rfm1m`. It looks for
the `rfm1_readout` export in the module and, when it is there, builds the Move side's readout glue (`-DRFM1M_RD1`,
fw/fwglue.c `rfm1m_rd1`) by itself: nothing in remoteFM14Move needs changing for these files. For each firmware:

1. Take the file from `build-labels-out/<id>-<version>.rfm1` (sha256 above); Elasticat: the trial file above.
2. In remoteFM14Move, run `tools/build_fw.sh` on it, as `tools/build_move_catalog.py` / `tools/catalog_fw.sh` do for
   a catalog file (the same version number as the released one: install it as a replacement of that firmware, or give
   it another name, so the Move's catalog copy is not confused with it).
3. On the Move: turn a knob that the table above names (e.g. Felucca HOME, KNOB 1): the popup shows the name, then the
   value (`tests/rd1_e2e.sh` there checks ChoralRoot the same way); the knob labels follow the page.

Not done here (as asked): no Move build, no ssh, no install.
