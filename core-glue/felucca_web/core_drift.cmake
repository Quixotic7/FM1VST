# SPDX-License-Identifier: GPL-3.0-only
# DRIFT 0.9.1 (https://github.com/FBNFRTG/drift-fm1, tag v0.9.1): Fabian Freitag's ambient instrument, a fork of
# Felucca, the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). Its Felucca-Engines are in the tree (firmware/engines, no submodule to fetch).
# The version: FELUCCA_VERSION "DRIFT 0.9.1", as web/emu/build.sh passes it from felucca.c (and build.py --release
# 0.9.1 stamps it): without it the web unit says "WEB", and the sample uploader wants INFO "FELUCCA DRIFT 0.9.1". The
# identity FM-1_983 (core_drift.c).
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(drift ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/drift-fm1 UNIT core_drift.c
             DESC core_drift_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "DRIFT by Fabian Freitag, a fork of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. Hügelton Instruments' Felucca-Engines (ANALOG, LOFI, TRIO, WHEEL, NOISE, the engine interface and DSP helpers) and the Fukiai icon font, MIT. Inter Tight font by The Inter Project Authors, SIL OFL 1.1. Samples: Versilian Studios VSCO-2 Community Edition and VCSL, CC0 1.0. DaisySP by Electrosmith and Emilie Gillet and Rings by Emilie Gillet, MIT. msfa by Google Inc. and Pascal Gauthier (from Dexed), Apache-2.0. VOICE after klattsch by Tony Gies (MIT, design reference). The second core after X0X by charlesvestal, Melodee by keremimo and Jangada by zednaked, the browser emulator after X0X (GPL-3.0). Felucca contributions by keremimo, ChanceTheMaker, andreahaku, spinkham, ugai, zednaked, jasonpersinger, renebohne and gospodindilly-ops."
             GEN felucca_fm6.h felucca_samples.h felucca_tables.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"[A-Z]* *v?([^\"]*)\""
             DEFINES "FELUCCA_VERSION=\"DRIFT 0.9.1\"")

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md, CREDITS.md): the GPL, LICENSING.md and CREDITS.md; the ported msfa, DaisySP and Rings; Felucca-Engines;
# the Fukiai icon font and Inter Tight (rasterised into ui_icons.h / ui_fonts.h); the CC0 samples' provenance
# (felucca_samples.h). Not the JieLi SDK's Apache-2.0 (device packages only) nor DotGothic16 (the browser page's
# labels); the tree's second copies of the same texts (firmware/engines/LICENSE, assets/fonts/OFL.txt, ..) left out.
fm1_add_rfm1(drift AUTHORS "Fabian Freitag (FBNFRTG)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "CREDITS.md ({name})=CREDITS.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (Felucca-Engines in {name})=LICENSES/MIT-Felucca-Engines.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
                           "CC0-1.0 (the samples in {name})=assets/samples-cc0/ATTRIBUTION.txt")
