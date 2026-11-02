# SPDX-License-Identifier: GPL-3.0-only
# Dungeon Tracker 1.0.0 (https://github.com/BourbonWX/Dungeon-Tracker, tag v1.0.0): BourbonWX's fork of Felucca 1.6.1,
# the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core), with its submodules (firmware/engines, firmware/bsp: SUBMODULES_RECURSIVE; the
# web unit uses neither felucca-bsp nor the off-by-default ADPCM engine). Built as web/emu/build.sh builds its browser
# emulator; the generated headers are tools/build.py generate's. VERSION: the tag's (felucca.c's default says v1.1;
# the release is built with --release 1.0, FELUCCA_VERSION v1.0). The golden check: fw_common.cmake.
set(_dt_defs FELUCCA_VERSION="v1.0")
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(dungeontracker ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Dungeon-Tracker
             UNIT core_dungeontracker.c DESC core_dungeontracker_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             SUBMODULES_RECURSIVE
             CREDITS "Dungeon Tracker by BourbonWX and the Dungeon Tracker (RONDO) contributors, a fork of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. Felucca-Engines and the Fukiai icon font (Hügelton Instruments), MIT. WAVES after Ambika by Emilie Gillet, GPL-3.0-or-later. DaisySP (Electrosmith, Emilie Gillet) and Rings (Emilie Gillet), MIT. msfa (Google Inc., Pascal Gauthier), Apache-2.0. Inter Tight font (The Inter Project Authors), SIL OFL 1.1. Samples: Versilian Studios VSCO-2 CE and VCSL (CC0 1.0), the Hügelton Sample Pack (CC0 1.0). The full notices: the release's licences/cores/dungeontracker/."
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
             VERSION 1.0.0
             DEFINES ${_dt_defs})
fw_golden(dungeontracker ${CMAKE_SOURCE_DIR}/cores/Dungeon-Tracker web/emu/felucca_web.c ${_dt_defs})
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL and LICENSING.md; the ported msfa, DaisySP and Rings; the Felucca-Engines submodule; the
# Fukiai icon font and Inter Tight (rasterised into ui_icons.h / ui_fonts.h / ui_keycaps.h); the CC0 samples'
# provenance (felucca_samples.h). Not the JieLi SDK's Apache-2.0 (device packages only), DotGothic16 (the browser
# page's labels), felucca-bsp's MPL-2.0 (not in the web unit) nor snes_spc's LGPL (the ADPCM engine, built off).
fm1_add_rfm1(dungeontracker AUTHORS "BourbonWX"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (Felucca-Engines in {name})=LICENSES/MIT-Felucca-Engines.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
                           "CC0-1.0 (the samples in {name})=assets/samples-cc0/ATTRIBUTION.txt")
