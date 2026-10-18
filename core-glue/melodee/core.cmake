# SPDX-License-Identifier: GPL-3.0-only
# Melodee (https://github.com/keremimo/melodee, Felucca's fork with Prophet, FM6, CZ-1, SID and the x0x 909):
# core-glue/melodee/ is its emulator over the unmodified sources (cmake/FM1Cores.cmake: fm1_add_core).
# x0x_drum_tables.h is not written by build.py generate() (build_app runs tools/gen_909_tables.py): EXTRA_GEN.
# GATED: the firmware bundles two sets of third-party sound data whose owners grant no licence (LICENSING.md, "Third-
# party material"): Casio's 64 CZ-1 preset tones ("no licence is granted for them by this project") and Sequential's
# 200 Prophet-5/10 factory programs ("the rights stay with Sequential"). The code is GPL-3.0-only (with MIT, BSD-3,
# Apache-2.0 and OFL parts, all compatible). Un-gating needs Casio's and Sequential's permission, or a Melodee build
# without the two banks (the firmware has no switch for that today).
fm1_add_core(melodee ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/melodee UNIT core_melodee.c
             DESC core_melodee_desc.c PREFIX ME FAMILY melodee LICENSE GPL-3.0-only
             GATED "bundles Casio CZ-1 preset tones and Sequential Prophet-5 factory programs with no licence granted (LICENSING.md)"
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h melodee_tables.h melodee_fm6.h melodee_cz1.h
                 melodee_prophet_factory.h x0x_drum_tables.h
             EXTRA_GEN "tools/gen_909_tables.py build/gen/x0x_drum_tables.h"
             VERSION_FILE firmware/src/melodee.c VERSION_REGEX "^#define MELODEE_VERSION \"v?([^\"]*)\"")
# The firmware unit with warnings ON (fm1_add_core gives it -w): the 0.11 glue's two LED planes against 1.4's
# three (ui_input.c writes fm1_led_dim[2][c]) built silently behind -w and overran. -Wall -Wextra, an out-of-bounds
# index an error; silenced by name only what the unmodified firmware itself shows on the host (its unused statics,
# parameters and fields, sign compares of non-negative values, hal/fm1_usb.h's register casts, hostsim.c's memset /
# memcpy over the SDK's fortify macros, one set-but-unused variable and parameter).
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/core_melodee.c PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Werror=array-bounds;-Wno-unused-function;-Wno-unused-parameter;-Wno-missing-field-initializers;-Wno-sign-compare;-Wno-int-to-pointer-cast;-Wno-macro-redefined;-Wno-unused-but-set-variable;-Wno-unused-but-set-parameter")

# The golden check (golden/me_golden.c): the core's screen against Melodee's own host renderer, tests/ui_render.c,
# built from the submodule as its tests/run_tests.sh builds it (-O1 -w -DMELODEE_USB_AUDIO=1)
if(TARGET melodee_core)
    set(_me_src ${CMAKE_SOURCE_DIR}/cores/melodee)
    add_executable(melodee_ui_render ${_me_src}/tests/ui_render.c)
    target_include_directories(melodee_ui_render PRIVATE ${_me_src}/build/gen ${_me_src}/firmware/src ${_me_src}/tests)
    target_compile_options(melodee_ui_render PRIVATE -O1 -w)
    target_compile_definitions(melodee_ui_render PRIVATE MELODEE_USB_AUDIO=1)
    if(MATH_LIB)
        target_link_libraries(melodee_ui_render PRIVATE ${MATH_LIB})
    endif()
    add_dependencies(melodee_ui_render gen_melodee)
    add_executable(melodee_golden ${CMAKE_CURRENT_LIST_DIR}/golden/me_golden.c)
    target_include_directories(melodee_golden PRIVATE ${FM1_CORE_API_DIR})
    target_compile_options(melodee_golden PRIVATE -Wall -Wextra)
    target_link_libraries(melodee_golden PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(melodee_golden melodee_core melodee_ui_render)
    add_test(NAME melodee_golden COMMAND melodee_golden $<TARGET_FILE:melodee_core> $<TARGET_FILE:melodee_ui_render>
                                         ${CMAKE_BINARY_DIR}/melodee_golden_out)
    set_tests_properties(melodee_golden PROPERTIES TIMEOUT 300)
endif()
