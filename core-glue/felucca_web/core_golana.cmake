# SPDX-License-Identifier: GPL-3.0-only
# Golana 1.3 (https://github.com/Gadlele/Golana, tag golana-v1.3): Gadlele's analog-style synth, a fork of Felucca,
# the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The version: FELUCCA_VERSION "v1.3" (build.py --release 1.3's, also felucca.c's
# default); the identity FM-1_913 (core_golana.c).
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(golana ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Golana UNIT core_golana.c
             DESC core_golana_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "Golana by Gadlele, a fork of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. The USB rescue follows zvenson's dinghy (GPL-3.0). Hügelton Instruments' PHASE waveforms (a port of CrispyZebra, GPL-3.0), DRUM voices and Sample Pack (GPL-3.0-only) and the Fukiai icon font (MIT). Inter Tight font by The Inter Project Authors, SIL OFL 1.1. Samples: Versilian Studios VSCO-2 Community Edition and VCSL, CC0 1.0. DaisySP by Electrosmith and Emilie Gillet and Rings by Emilie Gillet, MIT. msfa by Google Inc. and Pascal Gauthier (from Dexed), Apache-2.0. VOICE after klattsch by Tony Gies (MIT, design reference). The browser emulator after X0X by charlesvestal (GPL-3.0). microKORG is a trademark of Korg, named as a reference only."
             GEN felucca_fm6.h felucca_samples.h felucca_tables.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\""
             DEFINES FELUCCA_VERSION="v1.3")

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md, the README's credits): the GPL and LICENSING.md; the ported msfa, DaisySP and Rings; the Fukiai icon
# font and Inter Tight (rasterised into ui_icons.h / ui_fonts.h); the CC0 samples' provenance (felucca_samples.h). Not
# the JieLi SDK's Apache-2.0 (device packages only) nor DotGothic16 (the browser page's labels); the tree's second
# copies of the same texts (assets/fonts/OFL.txt, web/FUKIAI-LICENSE.txt, ..) left out.
fm1_add_rfm1(golana AUTHORS "Gadlele"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
                           "CC0-1.0 (the samples in {name})=assets/samples-cc0/ATTRIBUTION.txt")
