# SPDX-License-Identifier: GPL-3.0-only
# JIANT FM 0.9.9 (0912abb, https://github.com/juanjiant-bit/JIANT): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(jiant ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/JIANT UNIT core_jiant.c
             DESC core_jiant_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "VOICE's English words: a subset of The CMU Pronouncing Dictionary (cmudict 0.7b, Carnegie Mellon University), BSD-2-Clause:"
             CREDITS_FILES LICENSES/BSD-2-Clause-CMUdict.txt
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h ui_shapes.h felucca_tables.h felucca_lyrics.h
                 felucca_fm6.h ui_font_x.h gen_drum_maps.h dice_bars.h gen_bass.h
             VERSION 0.9.9 DEFINES FELUCCA_VERSION="v0.9.9")
fw_golden(jiant ${CMAKE_SOURCE_DIR}/cores/JIANT web/emu/felucca_web.c FELUCCA_VERSION="v0.9.9")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(jiant AUTHORS "juanjiant-bit")
