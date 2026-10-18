# SPDX-License-Identifier: GPL-3.0-only
# Melodee (https://github.com/keremimo/melodee, Felucca's fork with FM6, CZ-1 and USB audio): core-glue/melodee/ is
# its emulator over the unmodified sources (cmake/FM1Cores.cmake: fm1_add_core)
fm1_add_core(melodee ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/melodee UNIT core_melodee.c
             DESC core_melodee_desc.c PREFIX ME FAMILY melodee LICENSE GPL-3.0-only
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h melodee_tables.h melodee_fm6.h melodee_cz1.h
             VERSION_FILE firmware/src/melodee.c VERSION_REGEX "^#define MELODEE_VERSION \"v?([^\"]*)\"")
