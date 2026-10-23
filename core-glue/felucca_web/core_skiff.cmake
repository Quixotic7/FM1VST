# SPDX-License-Identifier: GPL-3.0-only
# Skiff 1.4 (https://github.com/illuminus5/skiff, tag v1.4): the felucca_web family's core (fw_core.h over the
# firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core), the FM-1 board (build.py's
# default; the SMK-37 Pro build, BOARD_SMK37, is left out). The golden check: fw_common.cmake. Public (the owner's
# call, 2026-10-22): SLICE's two built-in loops, BREAK and FUNK (assets/breaks/), are cut from drum takes generated
# with the YuE2 music model (Multimodal Art Projection), published under YuE2's permission for creators to distribute
# its output; the credits say so.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(skiff ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/skiff UNIT core_skiff.c
             DESC core_skiff_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "Skiff by illuminus5, a fork of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. DRUM-X from JIANT FM (juanjiant-bit, GPL-3.0). DaisySP and Rings (Electrosmith, Emilie Gillet), MIT. msfa (Google Inc., Pascal Gauthier), Apache-2.0. Fonts: Barlow Condensed, Inter Tight, DotGothic16 (SIL OFL 1.1), the Fukiai icon font (MIT). samples VSCO-2 CE and VCSL (CC0 1.0). SLICE's BREAK and FUNK loops are AI-generated: cut from drum takes made with the YuE2 music model by Multimodal Art Projection (m-a-p), published under YuE2's permission for creators to distribute its output (assets/breaks/ATTRIBUTION.txt). The full notices: the release's licences/cores/skiff/."
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h ui_pixfont.h felucca_tables.h felucca_fm6.h
                 felucca_samples.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\""
             DEFINES FELUCCA_VERSION="v1.4")
fw_golden(skiff ${CMAKE_SOURCE_DIR}/cores/skiff web/emu/felucca_web.c FELUCCA_VERSION="v1.4")
