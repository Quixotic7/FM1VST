# SPDX-License-Identifier: GPL-3.0-only
# Rainbow Mode 1.1.5-rb (https://github.com/joshbartlettband-web/felucca-rainbow, main @ 2a5bed79e4f1291359779d4938ca300a26d12471): owner's exception:
# no release or tag, main @ 2a5bed7, 2026-11-01 (the firmware hub's 1.1.5-rb, 2026-10-31). the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
# GATED (CORES-PLAN.md 6): kid.c's friends include 17 look-alikes of TV and film characters (the grown-ups' LOOK-ALIKES
# shows original stand-ins instead, but the look-alike pictures are in every build: kid_art.h). Un-gate when the author
# confirms the pictures may be redistributed, or a build without them (the stand-ins only) exists upstream.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(rainbow ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/felucca-rainbow UNIT core_rainbow.c
             DESC core_rainbow_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             GATED "bundles pixel-art look-alikes of TV and film characters (tools/gen_kid_art.py: WEB HERO, SPIDER MOM, GROUCH, SPACE HERO ..), third-party character rights unverified"
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
                 kid_art.h
             VERSION 1.1.5-rb DEFINES FELUCCA_VERSION="v1.1.5")
fw_golden(rainbow ${CMAKE_SOURCE_DIR}/cores/felucca-rainbow web/emu/felucca_web.c FELUCCA_VERSION="v1.1.5")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md), enabled by RFM1Manager. The licence texts: the
# firmware's own (its LICENSE, LICENSES/, LICENSING.md, the fonts')
fm1_add_rfm1(rainbow AUTHORS "Hausway"
             HOSTED_DESPITE_GATE "the look-alike pictures of TV and film characters (original drawings, with a LOOK-ALIKES OFF setting) are in every build: built as the author ships it, hosted on RemoteFM1FW only, the owner call of 2026-11-01"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "Apache-2.0 ({name})=LICENSES/Apache-2.0.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Fukiai in {name})=LICENSES/MIT-Fukiai.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "OFL-1.1 (DotGothic16 in {name})=LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (InterTight in {name})=LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (assets/fonts/OFL.txt in {name})=assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/emu/fonts/OFL.txt in {name})=web/emu/fonts/OFL.txt")
