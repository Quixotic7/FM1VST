# Built on rfm1-labels: not published

All in `build-labels-out/` of this worktree (build output, not committed), emcc 6.0.13, `-DFM1_RFM1_ONLY=ON`
(`-DFM1_PUBLIC_BUILD=OFF` for the hosted-despite-gate ones, as RFM1Manager builds them). 34 files, all built after the
last shim change (2026-11-24, batch 3):

```
b6ebdd6634d5f80c16686a007dd0c4bea891c35c051224ea27e3eb98ab98c92f  a22-0.1.0.rfm1
ca9cc5eb831691004d18d415bf47a84c5f56cdadda2b2ac08c5bf12b697b7d86  arcticscream-2.1.4.rfm1
4e9e6c31777dde51a5453e2d35402866ae113404dbc5c906be284e29665172c5  aurora-0.8.0.rfm1
dd9359f7fc9e7479dd0e2e34b7ac6579481ca69c3a006e95da85716e5fb32bab  bytemania-1.3.2.rfm1
8af40720534bcd8832727e2b955cb95c0b6ef456079500efb1bdcf1124d0b56c  clipper-1.3.1.rfm1
3a5975c70aa82dcf462727edc3f9f0e604ddf55ddd6b9e9a6f3fed6009291b61  coralflower-0.7.1.rfm1
99a03bf65cd07ee48b46f0f949b83690fcba9c230f8e8bb2135e842a11176d9a  dungeontracker-1.5.rfm1
f62debc00afbda4611aba431ba93fdfe1688c93c72a8e33847481bd29a952e22  evilgenius-0.1.4.rfm1
68074889c9c7447241ff7f151351a72e8ed2f39c93e4b84cb8a3d2e973c0395d  felucca-1.8.2.rfm1
835395f557a6dc849be24149dbef3cbc8503ecd65aa82598ace79c561502b8d0  fmav1-0.6.4.rfm1
1816c3d2cb6322c2d4e0eede553a75005fa37b8ff28a0a023b41275b86e50937  fmel1-1.3.rfm1
c6f1efae6f3758ae8ce1db4793436739336588ac289488fbccbcab082eaba0ba  fmkick-0.5.1.rfm1
e685c513bb858f701badb0263f75d8e8b6df06441e5e04c50f5ee449bfcaf041  fomni-0.5.rfm1
b22ad8b06abaf762174ffbb3c67d106f298d93625dec34af8d74c6fa5d8f63d7  ghoulbox-2.4.rfm1
3bd692a5a4d2fa2b37a63e0795328fe5ed641c6ca0055b60d2bc9bf49a358ab0  glitch-2.0.2.rfm1
10eb335f65d2b8ac473f2833d1b02fa4f29ceaa566fbe2c4e66b1e846c58f10a  golf-1.0.rfm1
bfe7e1659761f981285c05b3a114b6e834d74fab3312fa377799943f729fcba5  grimm-2.0.2.rfm1
e37cefe64e3484acc5d74100ca33b16234ff7cc52a93fb9c84559a7de24eb1e2  h1-0.3.0.rfm1
991a2d61cd715c91f0641bd1592efeb4ae33ad5b96eb595ff8d77e7f4056b098  hyp1-1.5.54.rfm1
ac5cfe757b9ace64c91a8cfce6eead22ec1fc4e6f652d6884b361b37d9e7e83b  jamesos-1.0b.rfm1
6a840312d896162a11ce38aff244ba843685a197a36f575b530b9637776a2219  karma-3.1.5.rfm1
5780b76001854015fe0994ffddea80a9d697d1928dc0988b1ba7147bcb305e3b  melodee-1.6.1.rfm1
236a0a379080e452391d6eccb8feeaae9a81602257fc4e9742463275f814a72c  mightyarp-1.5.rfm1
6d37c2a76210b753619e658aa765bebbb2089dc3525caf09083402cd6ede32a0  minitone-1.0.5.rfm1
178ed9ecaea1b23ad3a70ecb79a3b5c1571a4d0f37ab7c8f9a119941e2087df5  mpyre-1.0.3.rfm1
fbecbf35491f1ab8e4510527a5d58183ed4ae3b33e38394563eb2493f418474e  mvsynth2-1.5.2.rfm1
bb0231059573d292b6f69c1cf1d39742c59c1cff8cdbf17f9f140a2d18bd75e9  nachtfahrt-1.0.2.rfm1
3502cf743e4a5e7fcfef68d28a90225d34d1ff2e8ff17283d3663e49fe24ba1c  orbit-1.0.0.rfm1
971c068e9422893316b471766552852336404a5a4a8d26e725e101994fa1b25c  sid1-1.0.1.rfm1
c9c064c6b75dc48146ecbc1902a8ccfa0be7d6bf316669e0785d32dbc4397567  sloop-2.8.5.rfm1
c2512b074d0cf232d4db1df26eae8c8bfb76e1342eb9c8f73b022edeaf6ea6bc  sloop3-3.4.0.rfm1
ed74b337255a747475fcee879a4e5c94000d06b626403b5f2b554c8f8833ada0  stallion-1.1.rfm1
949edc40be37f911559bef5ae388bd2dce7f967d11537b920afded6f8ab30f42  tekstuur-2.0.rfm1
375247c4ab0f756ea96162b90693ce2042697e8d0648c8b2052e506b5f9a5cc2  zen1-1.2.rfm1
```

And a trial from Elasticat's own branch (not the pinned tree):
`/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/elasticat-builds/elasticat-0.6.97-readouts-trial.rfm1`, sha256
`0b65f3aa2e96cc2842baa4a513e96769e4fc8999aa832d9e091df72f062cf2bb` (elasticatfm1 branch `labels-readouts`, 53148e4,
worktree `Elasticat-tmp/elasticatfm1-readouts`; built with `-DEC_SOURCE=` that worktree).

## Evidence

Every file passes `tools/rfm1_check.py` ("readouts (rd1)") and its ctest `rfm1_rd_<id>` (knob turns and page presses,
the readout's control, name and value text expected exactly), with `rfm1_<id>` and `rfm1_labels`: all pass in every
build folder (build-rd1, -grm, -eg, -fk, -sl, -ec, -b3, -b3d, -b3p). Sound: the same run (page presses, knob turns,
ALGORITHM, the chord) on each new file and on the released file (RFM1Manager/cache/release/current) gives bit-identical
PCM for all 34 and the Elasticat trial. The readouts were compared with each firmware's own screen (`rfm1_run
--screen`, a contact sheet per firmware: the screens with the readouts that run gave printed under each, in
`screens/` and `screens/b3/`; batch 3's tours run with `--keys none`, as a key would change a sequencer screen).

| firmware | batch | how | screens compared | the test expects (examples) | screens that give nothing (no label, no readout) |
|---|---|---|---|---|---|
| melodee | A | Tier 2 map (WANTS rd1); Prophet HOME cells in % | HOME (Prophet, FM6) | HOME CUTOFF = 35 %; Part = Part 2; E3 MLVL |  |
| felucca | A | Tier 2 map (WANTS rd1) | HOME | E5 CUT = 412 Hz; Tempo = 121 BPM |  |
| grimm, glitch, arcticscream | 1 | grimm_rd.h | HOME, ENV, GRID | ATTACK = 7 ms (arctic 1.3 s); TRACK = T2; SOUND = <name> | step locks (SEL held on SEQ) |
| evilgenius | 2 | FIZZ's adapter (qd_ctl_labels, qd_view_turn, qd_view_edit) | CHNC, NOTE defaults | CHANCE = 100%; CONDITION = !FILL |  |
| dungeontracker, ghoulbox, minitone | 2 | Felucca's map + per-fork options (fw_map_fork.h) | HOME ENV LFO FX SCL ARP EDIT GLO-held | Attack; Part = Part 2; Chorus Send | DICE page (dungeontracker) |
| sloop, clipper, karma, mpyre, orbit, stallion | 2 | sloop_rd.h (the columns as drawn) | HOME ENV LFO FX SEL ARP EDIT GLO-held | ATK, REL (orbit ROOT, CHORD) | karma's HOME, M-PYRE's FX/ARP/AMP, ORBIT's own ENV/LFO/FX/EDIT, layers, REC, menus |
| sloop3 | 3 | main.c's view V | HOME ENV LFO FX SEL ARP EDIT SEQ | DEC = 3.6s; REL = 47ms | splash, visualiser |
| coralflower | 3 | ui_knob_info | HOME ENV LFO FX SEL ARP EDIT SEQ | KEY = Db; RELEASE = 33 ms | intro, update countdown |
| a22, h1 | 3 | h1_control (all seven knobs) | HOME ENV LFO FX SEL ARP EDIT SEQ | DECAY; RELEASE |  |
| fmav1 | 3 | page_param / plabel / param_value | + GLO, SAVE | ATTACK = 114ms; RELEASE = 469ms | the patchbay overview |
| mvsynth2 | 3 | VS_PAGES / HOME's knobs, vs_param_label / ptext | HOME ENV LFO FX SEL ARP EDIT GLO | ATTACK = 1; AMOUNT = +1 | bank, SAVE, GLOBAL |
| jamesos | 3 | br_control / br_control_text; levels via br_level_law | + KIT LEVEL, MIXER | ATTACK = 0.77MS; RELEASE = 40.4MS; TRACK 2 = -0.4 dB | the SOUNDS list |
| sid1 | 3 | KNOB[page] / knob_text | all pages | ATTACK = 8; RELEASE = 1.5S |  |
| fmel1 | 3 | CTL[page] / fmt_val (draw_plate) | all pages | TONE = 73; RELEASE = 19 |  |
| aurora | 3 | PAGES[page].p / fmt_value | all pages | ATTACK = 31; RELEAS = 81 |  |
| bytemania | 3 | knob_text (main, editor) | main, editor, CC, ENV | A = 13; D = 5 | CC MAP, SAVE, NAME, ENV |
| fmkick | 3 | knob_info | LFO FX EDIT GLO SEQ | TARGET = TYPE; LENGTH = 17 | HOME (no knobs) |
| golf, tekstuur | 3 | PAGE / par_text (par_name) | all pages | golf TYPE = LOW 12, DRIVE = 21; tekstuur ATTACK = 71, DELAY = 21 |  |
| mightyarp | 3 | HOME's macros, page_knob / par_name / param_text | all pages | RELEASE = 2.1 S |  |
| fomni, hyp1, nachtfahrt | 3 | VIEW_KNOB + knob_name / KNOB_LABEL + knob_text | all pages | fomni Reverb = 31; hyp1 Level = 24; nachtfahrt Echo = 56 | nachtfahrt's REC grid |
| zen1 | 3 | the scene's knob bar (amb_knob_text), else VIEW_KNOB | the scene | Mood = Morning; Space = 56 |  |
| elasticat (trial) | 2 | its own RFM1_READOUT_BAR lines | LOOP | PITCH = +2; SLOT = OFF; START = 1; END = 128 |  |

Not built, for the reason given: VESPER (its web mirror disagrees with the LCD: `screens/b3/vesper_mismatch.png`, the
readouts said KEY F# and STEP 02 where the screen shows F and 10), Mutable Dreams (its cards exist only while a screen
reader listens, label and value in one string), PIGEON (Felucca's map names the wrong things on its KO screens), No Man's
Sky (its knob band has numbers, no names), ctl1 (its .rfm1 is the author's ctl).

## To try one on the Move (for whoever makes the Move builds)

The Move does not run an .rfm1 directly: remoteFM14Move's `tools/build_fw.sh` compiles one into an `.rfm1m`, and when
the module exports `rfm1_readout` it builds the Move side's readout glue (`-DRFM1M_RD1`, fw/fwglue.c `rfm1m_rd1`) by
itself: nothing in remoteFM14Move needs changing for these files. For each firmware:

1. Take `build-labels-out/<id>-<version>.rfm1` (sha256 above); Elasticat: the trial file above.
2. In remoteFM14Move run `tools/build_fw.sh` on it, as `tools/catalog_fw.sh` does for a catalog file. The version is the
   released one's: install it in place of that firmware (or under another name, so the Move's catalog copy and this
   one are not confused).
3. On the Move: open a page the table names and turn a knob: the popup shows the knob's name, then its value; the knob
   labels follow the page. A screen in the last column shows the Move's own labels and no popup.

Not done here (as asked): no Move build, no ssh, no install.
