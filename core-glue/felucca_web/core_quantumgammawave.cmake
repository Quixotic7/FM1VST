# SPDX-License-Identifier: GPL-3.0-only
# Quantum GammaWave 4.1.0 (https://github.com/Guillel51/Quantum-GammaWave, tag v4.1.0 = 671ac35): Guillel51's fork of
# Pocket GammaWave (on JIANT FM, on Felucca), the felucca_web family's core (fw_core.h over the firmware's own
# web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core), after core_jiant.cmake. The generated headers
# are tools/build.py generate()'s; the version: firmware/src/felucca.c's FELUCCA_VERSION; the identity FM-1_965
# (core_quantumgammawave.c). The golden check: fw_common.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
set(_qg ${CMAKE_SOURCE_DIR}/cores/Quantum-GammaWave)
set(_qg_ver 4.1.0)
fm1_add_core(quantumgammawave ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_qg} UNIT core_quantumgammawave.c
             DESC core_quantumgammawave_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "Quantum GammaWave by Guillel51, on Pocket GammaWave by Frederic Valente, on JIANT FM by Juan Jiant, on Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. FM6: msfa (Google Inc., Pascal Gauthier), Apache-2.0. The icons: the Fukiai font (MIT). The UI's text: IBM Plex Mono, Michroma, Chakra Petch, Inter Tight (SIL OFL 1.1)."
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h ui_shapes.h felucca_tables.h felucca_fm6.h
             VERSION ${_qg_ver} DEFINES FELUCCA_VERSION="v${_qg_ver}")
fw_golden(quantumgammawave ${_qg} web/emu/felucca_web.c FELUCCA_VERSION="v${_qg_ver}")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL and LICENSING.md, msfa's Apache-2.0, the Fukiai font's MIT, the rasterised fonts' OFL. Not
# the JieLi SDK's Apache-2.0 (device packages only) nor DotGothic16 (the page's). RUN_ARGS: the power-on splash (main.c
# SPL_WRITE + SPL_HOLD) holds the keys silent (kb_boot_hold) until ~4.5 s, past rfm1_run's default 4 s settle.
fm1_add_rfm1(quantumgammawave AUTHORS "Guillel51" "Frederic Valente (Pocket GammaWave)" "juanjiant-bit (JIANT)"
             "Leo Kuroshita (Hügelton Instruments) (Felucca)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (IBM Plex Mono in {name})=LICENSES/OFL-IBMPlexMono.txt"
                           "OFL-1.1 (Michroma in {name})=LICENSES/OFL-Michroma.txt"
                           "OFL-1.1 (Chakra Petch in {name})=LICENSES/OFL-ChakraPetch.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
             RUN_ARGS --settle 6000)
