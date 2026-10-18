# SPDX-License-Identifier: GPL-3.0-only
# JIANT FM 0.8.5 (https://github.com/juanjiant-bit/JIANT): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(jiant ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/JIANT UNIT core_jiant.c
             DESC core_jiant_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "VOICE's English words: a subset of The CMU Pronouncing Dictionary (cmudict 0.7b, Carnegie Mellon University), BSD-2-Clause:"
             CREDITS_FILES LICENSES/BSD-2-Clause-CMUdict.txt
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h ui_shapes.h felucca_tables.h felucca_lyrics.h
                 felucca_fm6.h
             VERSION 0.8.5 DEFINES FELUCCA_VERSION="v0.8.5")
fw_golden(jiant ${CMAKE_SOURCE_DIR}/cores/JIANT web/emu/felucca_web.c FELUCCA_VERSION="v0.8.5")
