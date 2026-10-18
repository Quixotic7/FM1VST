# SPDX-License-Identifier: GPL-3.0-only
# Rainbow Mode 1.1.5-rb (https://github.com/joshbartlettband-web/felucca-rainbow): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
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
