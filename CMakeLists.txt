# SPDX-License-Identifier: GPL-3.0-only
# FM1VST: the FM-1 emulator from ChoralRoot FM-1 as a plugin (see FM1-VST-PLAN.md).
# Phase 0: the ChoralRoot core as a static library and a headless smoke test.
cmake_minimum_required(VERSION 3.22)
project(FM1VST C CXX)

set(CR ${CMAKE_CURRENT_SOURCE_DIR}/cores/ChoralRootFM1)
if(NOT EXISTS ${CR}/tools/emu/emu_fw.c)
    message(FATAL_ERROR "cores/ChoralRootFM1 is empty: run git submodule update --init --recursive")
endif()

# the generators need Pillow: take the first python3 on PATH that has it (FindPython3 may pick another
# one); override with -DCR_PYTHON=/path/to/python3
set(CR_PYTHON "" CACHE FILEPATH "python3 with Pillow, for the ChoralRoot generate step")
if(NOT CR_PYTHON)
    execute_process(COMMAND /bin/sh -c "which -a python3"
                    OUTPUT_VARIABLE _py_list OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" _py_list "${_py_list}")
    foreach(_py IN LISTS _py_list)
        execute_process(COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
                        ${_py} -c "import PIL" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(CR_PYTHON ${_py} CACHE FILEPATH "python3 with Pillow, for the ChoralRoot generate step" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT CR_PYTHON)
    message(FATAL_ERROR "no python3 with Pillow found (pip install Pillow, or pass -DCR_PYTHON=...)")
endif()
message(STATUS "ChoralRoot generate step: ${CR_PYTHON}")

# ---- the generated headers: tools/build.py generate() only (its main() would build the firmware with
# the JieLi toolchain). It writes ${CR}/build/gen, which is gitignored in the submodule.
set(CR_GEN ${CR}/build/gen)
set(CR_GEN_HEADERS
    ${CR_GEN}/ui_fonts.h ${CR_GEN}/cr_fonts.h ${CR_GEN}/ui_icons.h ${CR_GEN}/ui_keycaps.h
    ${CR_GEN}/ui_palettes.h ${CR_GEN}/felucca_tables.h ${CR_GEN}/felucca_fm6.h ${CR_GEN}/melodee_cz1.h
    ${CR_GEN}/felucca_samples.h)
file(GLOB CR_GEN_SCRIPTS CONFIGURE_DEPENDS ${CR}/tools/*.py)
add_custom_command(
    OUTPUT ${CR_GEN_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib PYTHONPATH=${CR}/tools
            ${CR_PYTHON} -c "import build; build.generate()"
    WORKING_DIRECTORY ${CR}
    DEPENDS ${CR_GEN_SCRIPTS}
    COMMENT "ChoralRoot: generating build/gen headers (tools/build.py generate)"
    VERBATIM)
add_custom_target(gen_choralroot DEPENDS ${CR_GEN_HEADERS})

# ---- the ChoralRoot core: the firmware unit (one translation unit, from the unmodified submodule)
add_library(core_choralroot STATIC core-glue/choralroot/core_choralroot.c)
target_include_directories(core_choralroot PRIVATE ${CR_GEN} ${CR}/firmware/src ${CR}/tools/emu)
target_include_directories(core_choralroot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
target_compile_options(core_choralroot PRIVATE -O2 -w)
add_dependencies(core_choralroot gen_choralroot)
# the generated headers are included by the unit: rebuild it when they change
set_source_files_properties(core-glue/choralroot/core_choralroot.c PROPERTIES OBJECT_DEPENDS "${CR_GEN_HEADERS}")
find_library(MATH_LIB m)
if(MATH_LIB)
    target_link_libraries(core_choralroot PUBLIC ${MATH_LIB})
endif()

# phase 2: JUCE via FetchContent

# ---- tests
enable_testing()
add_executable(core_smoke tests/core_smoke.c)
target_link_libraries(core_smoke PRIVATE core_choralroot)
add_test(NAME core_smoke COMMAND core_smoke)
