# SPDX-License-Identifier: GPL-3.0-only
# Tonitor 0.53 (https://github.com/nzimas/tonitor): its own hand-written glue (core-glue/tonitor: tonitor_hal.h,
# hal/fm1_flash.h, tonitor_firmware.h, tonitor_fw.c, core_tonitor.c) over the unmodified submodule cores/tonitor.
# THE PIN: Tonitor has no tags or releases and its author publishes several times a day (0.50 .. 0.53 on 2026-10-21).
# The submodule is at c39d18f ("Website: Tonitor 0.53"), the commit that holds docs/firmware/tonitor.fwsc, the
# package the Firmware Hub serves as "2026.10.21" (its firmware commit is 9633aa3, TONITOR_VERSION "0.53"). A bump:
# the commit of the Hub's next package, and VERSION from its firmware/src/tonitor.c.
# Generate: tools/build.py generate() runs gen_tables.py and gen_font.py (Pillow; the font from the TTF in the tree).
# -Dsection(x)=aligned(4): dsp.h's POOL / FAST / DLYMEM section attributes, which Mach-O rejects.
fm1_add_core(tonitor ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/tonitor UNIT core_tonitor.c
             DESC core_tonitor_desc.c FAMILY tonitor LICENSE GPL-3.0-only
             GEN tonitor_tables.h tonitor_font.h
             INCLUDES ${CMAKE_CURRENT_LIST_DIR}/hal
             CREDITS "Tonitor by Nuno Zimas (github.com/nzimas/tonitor), built on Felucca (Leo Kuroshita, Hugelton Instruments) and SLOOP (isod89), with the dual-core mailbox from Melodee (Kerem Kilic, Ellic Studio). FM is msfa (Google Inc., Pascal Gauthier, Apache-2.0) in Felucca's integer port. The screen's type is Atkinson Hyperlegible (the Braille Institute), pre-rendered into glyph tables compiled into the firmware."
             CREDITS_FILES LICENSING.md LICENSES/BSD-LIRA-8.txt LICENSES/MIT-rings.txt
                           assets/fonts/AtkinsonHyperlegible-OFL.txt
             VERSION 0.53
             FLAGS "-Dsection(x)=aligned(4)")

# The golden check (where the firmware has its own host sim, drive both to the same state and compare the screen):
# Tonitor's tests/host/host.c, built from the unmodified tree as its tests/run_tests.sh builds it, writes `host
# screens`; tonitor_drive runs the core on golden.txt and compares (golden.cmake). What may differ, and why:
#   - the sound's name (y 140..190 on track 1's screen): host.c's fm1_ticks() steps 1 ms each call, so its sounds
#     are drawn from other seeds than a power-on at device ms 0;
#   - the battery corner (x 200..239, y 0..24): host.c attaches no USB host and reads no battery, the core does both
if(TARGET tonitor_core)
    add_executable(tonitor_drive ${CMAKE_CURRENT_LIST_DIR}/tonitor_drive.cpp)
    target_link_libraries(tonitor_drive PRIVATE fm1testlib)
    target_compile_options(tonitor_drive PRIVATE -Wall -Wextra)
    add_executable(tonitor_ref_host ${CMAKE_SOURCE_DIR}/cores/tonitor/tests/host/host.c)
    target_include_directories(tonitor_ref_host PRIVATE ${CMAKE_SOURCE_DIR}/cores/tonitor/firmware/src
                               ${CMAKE_SOURCE_DIR}/cores/tonitor/build/gen)
    target_compile_options(tonitor_ref_host PRIVATE -O2 -std=gnu99 -w)
    if(MATH_LIB)
        target_link_libraries(tonitor_ref_host PRIVATE ${MATH_LIB})
    endif()
    add_dependencies(tonitor_ref_host gen_tonitor)
    add_test(NAME golden_tonitor
             COMMAND ${CMAKE_COMMAND} -DREF=$<TARGET_FILE:tonitor_ref_host> -DDRIVE=$<TARGET_FILE:tonitor_drive>
                     -DCORE=$<TARGET_FILE:tonitor_core> -DSCRIPT=${CMAKE_CURRENT_LIST_DIR}/golden.txt
                     -DOUT=${CMAKE_BINARY_DIR}/golden/tonitor
                     "-DDRIVE_ARGS=--compare 1000 %REF%/screen-1-layer1.ppm --compare 2400 %REF%/screen-12-reverb.ppm --ignore 0 140 239 190 --ignore 200 0 239 24"
                     -P ${CMAKE_CURRENT_LIST_DIR}/golden.cmake)
    set_tests_properties(golden_tonitor PROPERTIES TIMEOUT 300)
endif()
