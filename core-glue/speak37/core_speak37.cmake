# SPDX-License-Identifier: GPL-3.0-only
# Speak37 1.0 (https://github.com/genkigenki/Speak37, tag v1.0): the talking synth's FM-1 build (build.py
# BOARD=fm1speak: firmware/src/speak.c with BOARD_SMK37 0, identity FM-1_5XY), on its own hand-written glue
# (core-glue/speak37: speak37_hal.h, speak37_firmware.h, core_speak37.c) over the unmodified submodule cores/Speak37.
# The SMK-37 build (BOARD=smk37: another panel) is out of scope.
#   GEN: build.py generate(); sp_vocab.h from the committed LPC cache (assets/speak/vocab_lpc.json: no espeak-ng
#        needed), with the FM-1's editor word lists (SP_NDICT "572 528", as build.py sets it for the FM-1); its
#        gen_vocab.py imports numpy
#   DEFINES: the v1.0 release package's identity and version (build.py --release 1.0: FM-1_510, v1.0)
#   FLAGS: the .pool / .noinit section attributes, which Mach-O rejects
# PUBLIC (the owner's call, 2026-10-22): the TMS5220's coding tables and the Speak & Spell chirp are chip constants
# as tabulated by MAME, BSD-3-Clause (LICENSES/BSD-3-Clause-MAME.txt, carried in the credits); the words are rendered
# by espeak-ng at build time (no TI ROM vocabulary); the 808 is M-VAVE's FM1 Open drum808.c (MIT, carried too).
fm1_add_core(speak37 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Speak37 UNIT core_speak37.c
             DESC core_speak37_desc.c FAMILY speak37 LICENSE GPL-3.0-only
             CREDITS "Speak37 by M. Geisler (genkigenki / makkuro), on Felucca by Leo Kuroshita (Hügelton Instruments).\nThe words are rendered by espeak-ng at build time and LPC-encoded by Speak37's own encoder: no TI ROM vocabulary is used. The editor's word lists come from FrequencyWords by Hermit Dave (github.com/hermitdave/FrequencyWords, CC BY-SA 4.0). The screen's text is the Inter Tight typeface (The Inter Project Authors, SIL Open Font License 1.1), rasterised at build time.\nThe TMS5220's coding tables (energy, pitch, K1..K10) and the Speak & Spell chirp, as tabulated by MAME, and RHYTHM's 808 (FM1 Open's drum808.c by M-VAVE, ported to fixed point) carry these notices:"
             CREDITS_FILES LICENSES/BSD-3-Clause-MAME.txt LICENSES/MIT-fm1_open-drum808.txt
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
                 sp_vocab.h vo_font.h
             GEN_PYTHON "__import__('os').environ.__setitem__('SP_NDICT', '572 528') or __import__('build').generate()"
             PYTHON_NEEDS numpy
             VERSION 1.0
             DEFINES FELUCCA_ID="FM-1_510" FELUCCA_VERSION="v1.0"
             FLAGS "-Dsection(x)=aligned(4)")

# The golden check (speak37_golden.cpp): Speak37's own FM-1 host test, tests/sp_screen.c -DT_FM1=1 (as its
# tests/run_tests.sh builds it, with the section flag above), runs its whole check and writes its 28 FM-1 screens; the
# core is powered on and its screen compared, pixel for pixel, with the one that does not depend on the test's own
# constructed state: the boot screen at 500 ms (sp_boot_frame(500): SPEAK37 typed half-way), but for its clock line: the test draws
# AC791N 0 MHZ (it never measures a clock), the core 360 MHZ, the FM-1's (speak37_golden.cpp). The
# test's other screens are states it sets up directly (knobs swept far past both ends, pages and touches written into
# the UI's state, the clock moved by hand), not reachable from the panel.
if(TARGET speak37_core AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(_sp_src ${CMAKE_SOURCE_DIR}/cores/Speak37)
    add_executable(speak37_sp_screen ${_sp_src}/tests/sp_screen.c)
    target_include_directories(speak37_sp_screen PRIVATE ${_sp_src}/build/gen)
    target_compile_options(speak37_sp_screen PRIVATE -O2 -w "-Dsection(x)=aligned(4)")
    target_compile_definitions(speak37_sp_screen PRIVATE T_FM1=1)
    if(MATH_LIB)
        target_link_libraries(speak37_sp_screen PRIVATE ${MATH_LIB})
    endif()
    add_dependencies(speak37_sp_screen gen_speak37)
    add_executable(speak37_golden ${CMAKE_CURRENT_LIST_DIR}/speak37_golden.cpp)
    target_link_libraries(speak37_golden PRIVATE fm1testlib)
    target_compile_options(speak37_golden PRIVATE -Wall -Wextra)
    add_dependencies(speak37_golden speak37_core speak37_sp_screen)
    add_test(NAME golden_speak37 COMMAND speak37_golden $<TARGET_FILE:speak37_core> $<TARGET_FILE:speak37_sp_screen>
                                         ${CMAKE_BINARY_DIR}/golden/speak37)
    set_tests_properties(golden_speak37 PROPERTIES TIMEOUT 300)
endif()
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(speak37 AUTHORS "genkigenki")
