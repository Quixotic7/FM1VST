# SPDX-License-Identifier: GPL-3.0-only
# cursed os 0.20.0 (https://github.com/cursedsound-music/CS_FM1, tag v0.20.0 = d4415c3): cursedsound's fork of JIANT FM
# V10.19, the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core), after core_jiant.cmake. The generated headers are tools/build.py generate()'s; the
# version: firmware/src/cos.c's COS_VERSION (FELUCCA_VERSION stays JIANT's V10.19, as the firmware has it); the
# identity FM-1_420 (core_csfm1.c). The golden check: fw_common.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
set(_cs ${CMAKE_SOURCE_DIR}/cores/CS_FM1)
set(_cs_ver 0.20.0)
fm1_add_core(csfm1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cs} UNIT core_csfm1.c
             DESC core_csfm1_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "cursed os by cursedsound, on JIANT FM by Juan Jiant, on Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only (cursedsound's own files also MIT). ACID's 303 voice: X0X by charlesvestal (GPL-3.0-only, the Open303 parts by Robin Schmidt, MIT). FMDRUM's transient samples and curves: the LXR drum machine's firmware by Erica Synths, GPL-3.0-only with its section 7 terms. FM6: msfa (Google Inc., Pascal Gauthier), Apache-2.0. The icons: the Fukiai font (MIT). The UI's text: IBM Plex Mono, Michroma, Chakra Petch, Inter Tight, Press Start 2P (SIL OFL 1.1). VOICE's English words: a subset of The CMU Pronouncing Dictionary (cmudict 0.7b, Carnegie Mellon University), BSD-2-Clause."
             GEN ui_fonts.h ui_font_x.h cos_font.h ui_icons.h ui_keycaps.h ui_palettes.h ui_shapes.h felucca_tables.h
                 felucca_lyrics.h felucca_fm6.h gen_drum_maps.h dice_bars.h gen_bass.h
             VERSION ${_cs_ver})
fw_golden(csfm1 ${_cs} web/emu/felucca_web.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL and LICENSING.md, cursedsound's MIT offer, Open303's MIT, msfa's Apache-2.0, the Fukiai
# font's MIT, the rasterised fonts' OFL, the CMU dictionary's BSD-2-Clause. Not the JieLi SDK's / the radio files'
# Apache-2.0 nor Optimist's MPL-2.0 (the Bluetooth segment: device packages only), nor DotGothic16 (the page's).
# RUN_ARGS: cursed os powers on with empty patterns and its keys are trigs, mutes and sections, not notes (docs/GUIDE.md),
# so rfm1_run's chord alone is silent. The guide's quick start instead: SAVE four times (SAVE > START), OCT+ loads a
# loop (HYPNO, the session seed 1), PLAY, HOME; the "chord" is white key 16 (G5), which HOME leaves alone.
# rd1: its knob row as drawn, live labels and readouts (core_csfm1.c, common/rd_cols.h)
fm1_add_rfm1(csfm1 WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press FX@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_DJ_FILT_=_+1 --expect-readout ctl_6_HIGH_=_+1
             AUTHORS "cursedsound" "juanjiant-bit (JIANT)" "Leo Kuroshita (Hügelton Instruments) (Felucca)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "MIT (cursedsound's own files in {name})=LICENSES/MIT-cursedsound.txt"
                           "MIT (Open303 in {name})=LICENSES/MIT-Open303.txt"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "BSD-2-Clause (CMU dictionary in {name})=LICENSES/BSD-2-Clause-CMUdict.txt"
                           "OFL-1.1 (IBM Plex Mono in {name})=LICENSES/OFL-IBMPlexMono.txt"
                           "OFL-1.1 (Michroma in {name})=LICENSES/OFL-Michroma.txt"
                           "OFL-1.1 (Chakra Petch in {name})=LICENSES/OFL-ChakraPetch.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (Press Start 2P in {name})=LICENSES/OFL-PressStart2P.txt"
             RUN_ARGS --press SAVE@2000 --press SAVE@2300 --press SAVE@2600 --press SAVE@2900 --press OCT+@3300
                      --press PLAY@3700 --press HOME@4100 --settle 4500 --keys 26)
