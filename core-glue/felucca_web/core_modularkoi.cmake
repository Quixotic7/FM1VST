# SPDX-License-Identifier: GPL-3.0-only
# Modular Koi 0.1 (https://github.com/bunglesociety/modular-koi, tag v0.1-beta = d27a62e; GPL-3.0-only) by Bungle
# Society (Alexis Arakis): the FM-1 as a steel tongue drum, built on Felucca 1.6.1. The felucca_web family's core
# (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core), with its
# submodules (firmware/engines, firmware/bsp: SUBMODULES_RECURSIVE; the web unit uses neither felucca-bsp nor the
# off-by-default ADPCM engine). Built as web/emu/build.sh builds its browser emulator; the generated headers are
# tools/build.py generate's. VERSION 0.1: the release's package (modular-koi-0.1.fwsc, tools/build.py --release 0.1,
# FELUCCA_VERSION v0.1; felucca.c's default says v1.6.1). The golden check: fw_common.cmake.
set(_dt_defs FELUCCA_VERSION="v0.1")
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(modularkoi ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/modular-koi
             UNIT core_modularkoi.c DESC core_modularkoi_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             SUBMODULES_RECURSIVE
             CREDITS "Modular Koi by Bungle Society (Alexis Arakis), built on Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. Felucca-Engines and the Fukiai icon font (Hügelton Instruments), MIT. DaisySP (Electrosmith, Emilie Gillet) and Rings (Emilie Gillet), MIT. msfa (Google Inc., Pascal Gauthier), Apache-2.0. The second core after X0X (charlesvestal), Melodee (keremimo) and Jangada (zednaked), GPL-3.0. Inter Tight font (The Inter Project Authors), SIL OFL 1.1. Samples: Versilian Studios VSCO-2 CE and VCSL (CC0 1.0), the Hügelton Sample Pack (GPL-3.0-only). The full notices: CREDITS.md, LICENSING.md and LICENSES/ in the repository."
             GEN cat_sprites.h felucca_fm6.h felucca_samples.h felucca_tables.h m1_pixfont.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h VERSION 0.1
             DEFINES ${_dt_defs})

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL and LICENSING.md; the ported msfa, DaisySP and Rings; the Felucca-Engines submodule; the
# Fukiai icon font and Inter Tight (rasterised into ui_icons.h / ui_fonts.h / ui_keycaps.h); the samples'
# provenance (felucca_samples.h). Not the JieLi SDK's Apache-2.0 (device packages only), DotGothic16 (the browser
# page's labels), felucca-bsp's MPL-2.0 (not in the web unit) nor snes_spc's LGPL (the ADPCM engine, built off).
fm1_add_rfm1(modularkoi AUTHORS "Bungle Society"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "Apache-2.0 ({name})=LICENSES/Apache-2.0.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Felucca-Engines in {name})=LICENSES/MIT-Felucca-Engines.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "OFL-1.1 (DotGothic16 in {name})=LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (InterTight in {name})=LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (assets/fonts/OFL.txt in {name})=assets/fonts/OFL.txt"
                           "OFL-1.1 (the silkscreen font in {name})=assets/fonts/ui/silkscreen-LICENSE.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=docs/firmware/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=docs/firmware/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (the Silkscreen in {name})=docs/img/Silkscreen-OFL.txt"
                           "MIT (the FUKIAI in {name})=web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/emu/fonts/OFL.txt in {name})=web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the Silkscreen in {name})=web/koi-fm1/img/Silkscreen-OFL.txt")
