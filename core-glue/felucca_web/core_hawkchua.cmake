# SPDX-License-Identifier: GPL-3.0-only
# HAWK CHUA 2.1.3 (https://github.com/Plieuw/hawk-chua, tag v2.1.3 = b4fd1ce): plieuwski's chaotic synthesizer on a
# stripped-down Felucca, each voice a Chua's circuit or a noise program into resonators, waveguides and a shimmer
# reverb. Not Felucca's web/emu/felucca_web.c (HAWK CHUA has none, so not fw_core.h either, nor a browser simulator):
# core_hawkchua.c is its host build, the firmware's sources in firmware/src/felucca.c's order as its host tests
# (tests/ui_shot.c) build them, unmodified, with the glue's HAL and main.c's boot and loop (cmake/FM1Cores.cmake
# fm1_add_core). The family's descriptor (fw_desc.h), as Hortator's and MONO2K's.
# Generated as tools/build.py generate() makes them: build/gen/ui_fonts.h (Inter Tight rasterised), ui_keycaps.h,
# ui_palettes.h. FELUCCA_VERSION "v2.1.3": build.py --release 2.1.3 (the tag's); the identity FM-1_007 (build.py
# PRODUCT, every build) is core_hawkchua.c's FELUCCA_ID.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(hawkchua ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/hawk-chua UNIT core_hawkchua.c
             DESC core_hawkchua_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "HAWK CHUA by plieuwski, a stripped-down fork of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. The FX resonators: Rings by Emilie Gillet (Mutable Instruments) and DaisySP (Electrosmith, Emilie Gillet), MIT, in Felucca's fixed-point port. The noise oscillators after Befaco's Noise Plethora, the waveguides after Kelly-Lochbaum, Cook and the Synthesis ToolKit, KARMA after Stephen Kay's Karma architecture: this firmware's own code, none of theirs. Details after M-VAVE's fm1_open (GPL-3.0 with a linking exception). The UI's text: Inter Tight by The Inter Project Authors (SIL OFL 1.1). KARMA is a trademark of Stephen Kay / Karma-Lab, M-VAVE and FM-1 of their owners: HAWK CHUA is not affiliated with them."
             CREDITS_FILES LICENSES/MIT-Rings.txt LICENSES/MIT-DaisySP.txt LICENSES/OFL-InterTight.txt
             GEN ui_fonts.h ui_keycaps.h ui_palettes.h
             VERSION 2.1.3 DEFINES FELUCCA_VERSION="v2.1.3")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in (LICENSING.md's
# table): the GPL, LICENSING.md, Rings' and DaisySP's MIT (phys_dsp.c, phys_symp.c), Inter Tight's OFL (the UI's
# text in ui_fonts.h). Not the JieLi SDK's Apache-2.0 (device packages only), nor the second core's files
# (FELUCCA_CPU2 0 here). RUN_ARGS: none (the keys play from the splash's end; the default chord sounds).
fm1_add_rfm1(hawkchua AUTHORS "plieuwski" "Leo Kuroshita (Hügelton Instruments)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "OFL-1.1 (Inter Tight, the UI's text in {name})=LICENSES/OFL-InterTight.txt")
