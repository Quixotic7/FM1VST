# SPDX-License-Identifier: GPL-3.0-only
# Caravel 0.11.0 (https://github.com/bonszaimusic/caravel, tag v0.11.0): bonszaimusic's fork of Felucca 1.6.1, the
# felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake
# fm1_add_core), with its Felucca-Engines submodule (firmware/engines: SUBMODULES_RECURSIVE). The golden check:
# fw_common.cmake. Built as web/emu/build.sh builds its browser emulator: build/gen and firmware/src on the include
# path (the unit includes firmware/hal and experiments/ by relative paths), the profile FELUCCA_HOME_PERF,
# FELUCCA_PAIR_MORPH, FELUCCA_PERF_HW = 1 and FELUCCA_CPU2 = 0; the generated headers are tools/build.py generate's.
set(_cv_defs FELUCCA_VERSION="v0.11.0" FELUCCA_HOME_PERF=1 FELUCCA_PAIR_MORPH=1 FELUCCA_PERF_HW=1 FELUCCA_CPU2=0)
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(caravel ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/caravel UNIT core_caravel.c
             DESC core_caravel_desc.c FAMILY felucca_web LICENSE GPL-3.0-only SUBMODULES_RECURSIVE
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\""
             DEFINES ${_cv_defs})
fw_golden(caravel ${CMAKE_SOURCE_DIR}/cores/caravel web/emu/felucca_web.c ${_cv_defs})
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL, LICENSING.md and NOTICE; the ported msfa, DaisySP and Rings; the Felucca-Engines submodule;
# the Fukiai icon font and Inter Tight (rasterised into ui_icons.h / ui_fonts.h / ui_keycaps.h); the CC0 samples'
# provenance (felucca_samples.h). Not the JieLi SDK's Apache-2.0 (device packages only) nor DotGothic16 (the browser
# page's labels); the tree's copies of the same texts (assets/fonts/OFL.txt, web/FUKIAI-LICENSE.txt, ..) once.
fm1_add_rfm1(caravel AUTHORS "bonszaimusic"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "NOTICE ({name})=NOTICE"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (Felucca-Engines in {name})=LICENSES/MIT-Felucca-Engines.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
                           "CC0-1.0 (the samples in {name})=assets/samples-cc0/ATTRIBUTION.txt")
