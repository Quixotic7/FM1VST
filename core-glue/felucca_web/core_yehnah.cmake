# SPDX-License-Identifier: GPL-3.0-only
# Yehnah 0.1.0 (https://github.com/Mello-D/yehnah, tag v0.1.0 = e8b59f6): Mello-D's chord-harp fork of JIANT FM, the
# felucca_web family's core (fw_core.h over the firmware's own web/emu/yehnah_web.c, unmodified; cmake/FM1Cores.cmake
# fm1_add_core), after the author's own glue (tools/rfm1/) and core_jiant.cmake. The generated headers are
# tools/build.py generate()'s; the version: firmware/src/yehnah.c's YEHNAH_VERSION; the identity FM-1_069
# (core_yehnah.c). The golden check: fw_common.cmake.
#
# GATED (CORES-PLAN.md 6), as FM-106: the JUNO engine (eng_juno.c JUNO_PRESETS) compiles in 99 patches of the Roland
# Juno-106 factory bank (A11 .. B88, names and settings), mapped by tools/gen_juno106.py from FM-106's
# fm106_presets.h (decoded from the 106's SysEx), with no build switch to leave them out. Also Roland-derived, a list
# of chords (no code): HARP's J-6 chord sets (firmware/src/harp_j6.h, the Roland J-6's 100 chord sets as its manual
# voices them, via benbadio's hera-fm1). The owner's call (2026-11-17): built as the author ships it, hosted only.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
set(_yn ${CMAKE_SOURCE_DIR}/cores/yehnah)
set(_yn_ver 0.1.0)
fm1_add_core(yehnah ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_yn} UNIT core_yehnah.c
             DESC core_yehnah_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             GATED "the JUNO engine compiles in the Roland Juno-106 factory bank (eng_juno.c JUNO_PRESETS, via FM-106) of unverified rights, no build switch to leave it out"
             CREDITS "Yehnah by Mello-D, on JIANT FM by Juan Jiant, on Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. Engines from Clipper, SLOOP, FoMni, FiMba-1, FMini-1 and guitchordi (GPL-3.0), Optimist's Plaits port (MIT), msfa (Google Inc., Pascal Gauthier, Apache-2.0). The icons: the Fukiai font (MIT). The UI's text: Space Grotesk, IBM Plex Mono, Michroma, Chakra Petch, Inter Tight (SIL OFL 1.1). VOICE's English words: a subset of The CMU Pronouncing Dictionary (cmudict 0.7b, Carnegie Mellon University), BSD-2-Clause. JUNO's factory bank: the Roland Juno-106's, via FM-106 (PatttF). HARP's J-6 chord sets via hera-fm1 (benbadio). Roland, Juno-106 and J-6 are names of their owners."
             GEN ui_fonts.h ui_font_x.h ui_icons.h ui_keycaps.h ui_palettes.h ui_shapes.h yehnah_tables.h
                 yehnah_fm6.h dice_bars.h
             VERSION ${_yn_ver} DEFINES YEHNAH_VERSION="v${_yn_ver}")
fw_golden(yehnah ${_yn} web/emu/yehnah_web.c YEHNAH_VERSION="v${_yn_ver}")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL and LICENSING.md, msfa's Apache-2.0, the Fukiai font's MIT, the rasterised fonts' OFL, the
# CMU dictionary's BSD-2-Clause (the
# Plaits port's MIT notice is in eng_plaits.c's header). Not the JieLi SDK's Apache-2.0 (device packages only) nor DotGothic16 (the page's).
fm1_add_rfm1(yehnah AUTHORS "Mello-D" "juanjiant-bit (JIANT)" "Leo Kuroshita (Hügelton Instruments) (Felucca)"
             HOSTED_DESPITE_GATE "Juno-106 factory patch data compiled in: built as the author ships it, hosted on RemoteFM1FW only, the owner's call of 2026-11-17"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "BSD-2-Clause (CMU dictionary in {name})=LICENSES/BSD-2-Clause-CMUdict.txt"
                           "OFL-1.1 (Space Grotesk in {name})=LICENSES/OFL-SpaceGrotesk.txt"
                           "OFL-1.1 (IBM Plex Mono in {name})=LICENSES/OFL-IBMPlexMono.txt"
                           "OFL-1.1 (Michroma in {name})=LICENSES/OFL-Michroma.txt"
                           "OFL-1.1 (Chakra Petch in {name})=LICENSES/OFL-ChakraPetch.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt")
