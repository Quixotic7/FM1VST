# SPDX-License-Identifier: GPL-3.0-only
# Felucca (https://github.com/hugelton/Felucca): core-glue/felucca/ is its emulator (felucca_fw.c, felucca_hal.h,
# felucca_firmware.h) over the unmodified sources (cmake/FM1Cores.cmake: fm1_add_core)
fm1_add_core(felucca ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Felucca UNIT core_felucca.c
             DESC core_felucca_desc.c PREFIX FE FAMILY felucca LICENSE GPL-3.0-only
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\"")
