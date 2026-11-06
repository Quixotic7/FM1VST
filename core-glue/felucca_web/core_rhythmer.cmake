# SPDX-License-Identifier: GPL-3.0-only
# Rhythmer 0.1.0-beta.2 (https://github.com/JazzyRhythmer/Rhythmer, tag v0.1.0-beta.2 = fe65b26, a GitHub
# pre-release; PUBLISH.json, CHANGELOG.md and RELEASE-VALIDATION.json say 0.1.0-beta.2; the firmware's own string,
# felucca.c's FELUCCA_VERSION, is "Rhythmer dev": core_rhythmer.c). Jazzy Rhythmer's JIANT-based six-track groovebox: the felucca_web
# family's core (fw_core.h) over rhythmer_web.c, JIANT's own browser unit as of the JIANT commit Rhythmer forked from,
# pointed at the unmodified submodule's jiant/ tree (Rhythmer's tree has no web/emu of its own; see core_rhythmer.c).
# No fw_golden: the reference would be the same file.
# The generate step: tools/build.py generate()'s list, run one by one (EXTRA_GEN) because its gen_aa_icons.py reads
# the Fukiai font from jiant/web/fukiai.ttf, which Rhythmer's tree does not carry: the same file (sha256 312bc6ab..,
# as JIANT's web/fukiai.ttf) is vendored at site/vendor/jiant-editor/fukiai.ttf and given with --font.
# The break recordings (assets/breaks/amen-160.slot, think-160.slot: "Amen, Brother" by The Winstons and "Think (About
# It)" by Lyn Collins, prepared as PCM12 banks) are NOT built in: build.py adds them to the device package's flash
# image only (fm1pkg_make.with_factory_banks, RHYTHMER_EMBEDDED_BANKS), never to the app, and audio-rights.json says
# no redistribution permission is documented ("publicRedistributionCleared": false). The core's flash starts erased:
# both banks empty.
set(_rhy ${CMAKE_SOURCE_DIR}/cores/Rhythmer)
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(rhythmer ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_rhy} UNIT core_rhythmer.c DESC core_rhythmer_desc.c
             EXTRA_SOURCES rhythmer_bass303.c rhythmer_jungle.c rhythmer_tb3po.c rhythmer_six303_bridge.c
             FAMILY felucca_web LICENSE GPL-3.0-only GEN_ROOT jiant
             CREDITS "Rhythmer by Jazzy Rhythmer, built on JIANT FM by Juan Ignacio Antonucci (Juan Jiant), itself a fork of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. The 303 voice and the TB-3PO generator: X0X by Charles Vestal (GPL-3.0-only, TB-3PO after schwung-tb3po and the Phazerville Hemisphere Suite's TB_3PO, GPL-3.0). The break engine: Amenity's JUNGLE (ejtb76/amenity, GPL-3.0-only with the GPL-3.0 linking exception). SERUM, PHASE and LOFI: Garden-FM (nvhhh, GPL-3.0). FM+STAT, SWAV-SAW and SID-6581: MONO2K by Iñigo Hernández (GPL-3.0-only). The MD drum models: Hortator by DEADACTIVE (GPL-3.0-only, with Mutable Instruments' fixed-point helpers under MIT). FM6: msfa (Google Inc., Pascal Gauthier), Apache-2.0. The UI's text: Chakra Petch, Inter Tight and Michroma (SIL OFL 1.1), its icons: the Fukiai font by Hügelton Instruments (MIT). VOICE's English words: a subset of the CMU Pronouncing Dictionary (BSD-2-Clause). No break recording is included in this build. M-VAVE and FM-1 are trademarks of their owners, Machinedrum of Elektron: Rhythmer is not affiliated with them."
             CREDITS_FILES LEGAL-NOTICE.txt docs/THIRD_PARTY.md
             GEN ui_fonts.h ui_font_x.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_lyrics.h
                 felucca_fm6.h gen_drum_maps.h dice_bars.h gen_bass.h
             GEN_PYTHON "__import__('pathlib').Path('build/gen').mkdir(parents=True, exist_ok=True)"
             EXTRA_GEN "tools/gen_aa_font.py build/gen/ui_fonts.h --preset jiant-lab"
                       "tools/gen_px_font.py build/gen/ui_font_x.h"
                       "tools/gen_aa_icons.py build/gen/ui_icons.h --font ../site/vendor/jiant-editor/fukiai.ttf"
                       "tools/gen_aa_keycaps.py build/gen/ui_keycaps.h"
                       "tools/gen_ui_palettes.py build/gen/ui_palettes.h"
                       "tools/gen_tables.py build/gen/felucca_tables.h"
                       "tools/gen_lyrics.py build/gen/felucca_lyrics.h"
                       "tools/gen_fm6_patches.py build/gen/felucca_fm6.h"
                       "tools/gen_drum_maps.py firmware/src/gen_tables.c firmware/src/song_dice.c build/gen/gen_drum_maps.h build/gen/dice_bars.h build/gen/gen_bass.h"
             INCLUDES ${_rhy}/jiant/firmware/src ${_rhy}/jiant/firmware/six
             VERSION 0.1.0-beta.2 DEFINES JUNGLE_AUDIO_CODE=)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in: the GPL,
# JIANT's LICENSING.md, msfa's Apache-2.0 (fm6_core.c), the Fukiai font's MIT (ui_icons.h), the three fonts' OFL
# (ui_fonts.h, ui_keycaps.h), CMUdict's BSD-2-Clause (felucca_lyrics.h, sing.c), Mutable Instruments' MIT (drum_md.c);
# not the JieLi SDK's Apache-2.0 nor fm1-clk320's MIT (device packages and the HAL only). RUN_ARGS: none.
fm1_add_rfm1(rhythmer AUTHORS "Jazzy Rhythmer"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md (JIANT FM, the base of {name})=jiant/LICENSING.md"
                           "Apache-2.0 (msfa, the FM6 engine in {name})=jiant/LICENSES/Apache-2.0-msfa.txt"
                           "MIT (the Fukiai icon font in {name})=jiant/LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (Chakra Petch, the UI's text in {name})=jiant/LICENSES/OFL-ChakraPetch.txt"
                           "OFL-1.1 (Inter Tight, the UI's text in {name})=jiant/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (Michroma, the UI's titles in {name})=jiant/LICENSES/OFL-Michroma.txt"
                           "BSD-2-Clause (CMUdict, VOICE's words in {name})=jiant/LICENSES/BSD-2-Clause-CMUdict.txt"
                           "MIT (Mutable Instruments, the MD drums in {name})=jiant/firmware/hortator/MIT-Mutable-Instruments.txt")
