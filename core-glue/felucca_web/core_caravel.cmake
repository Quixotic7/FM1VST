# SPDX-License-Identifier: GPL-3.0-only
# caravel (https://github.com/bonszaimusic/caravel, tag v0.8.0; GPL-3.0-only): scaffolded by RFM1Manager from felucca's glue (core-glue/felucca_web); review it
# Felucca 1.8.0 (https://github.com/bonszaimusic/caravel): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake. Since 1.8 the hardware layer is the felucca-bsp
# submodule (firmware/bsp, MPL-2.0): its chip/ and boards/fm1/ are on the include path as web/emu/build.sh puts them,
# and gen_adpcm.py's felucca_adpcm.h is generated.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(caravel ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/caravel UNIT core_caravel.c
             DESC core_caravel_desc.c FAMILY felucca_web LICENSE GPL-3.0-only SUBMODULES_RECURSIVE
             GEN felucca_fm6.h felucca_samples.h felucca_tables.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\""
             DEFINES FELUCCA_VERSION="v1.8.0"
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/caravel/firmware/hal ${CMAKE_SOURCE_DIR}/cores/caravel/firmware/bsp/chip
                      ${CMAKE_SOURCE_DIR}/cores/caravel/firmware/bsp/boards/fm1)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(caravel AUTHORS "bonszaimusic"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "NOTICE ({name})=NOTICE"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "Apache-2.0 ({name})=LICENSES/Apache-2.0.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Felucca-Engines in {name})=LICENSES/MIT-Felucca-Engines.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "OFL-1.1 (DotGothic16 in {name})=LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (InterTight in {name})=LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (assets/fonts/OFL.txt in {name})=assets/fonts/OFL.txt"
                           "GPL-3.0 (docs/performance/stasis-060/LICENSE.txt in {name})=docs/performance/stasis-060/LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/emu/fonts/OFL.txt in {name})=web/emu/fonts/OFL.txt")
