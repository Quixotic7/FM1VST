# SPDX-License-Identifier: GPL-3.0-only
# Felucca 1.6.1 (https://github.com/hugelton/Felucca): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(felucca ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Felucca UNIT core_felucca.c
             DESC core_felucca_desc.c FAMILY felucca_web LICENSE GPL-3.0-only SUBMODULES_RECURSIVE
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\""
             DEFINES FELUCCA_VERSION="v1.6.1")
fw_golden(felucca ${CMAKE_SOURCE_DIR}/cores/Felucca web/emu/felucca_web.c FELUCCA_VERSION="v1.6.1")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(felucca AUTHORS "Leo Kuroshita (Hügelton Instruments)")
