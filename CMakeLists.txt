# SPDX-License-Identifier: GPL-3.0-only
# FM1VST: the FM-1 emulator from ChoralRoot FM-1 as a plugin (see FM1-VST-PLAN.md).
# Phase 1: the ChoralRoot core as a loadable module, the engine (core loader, device clock, resampler), replay tests.
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

# ---- the ChoralRoot core: the firmware unit (one translation unit, from the unmodified submodule) and its
# descriptor (core-glue/choralroot/core_choralroot_desc.c), compiled once (hidden visibility, PIC) and used twice:
#   core_choralroot   static library, for tests that link the core directly (core_smoke)
#   choralroot_core   the loadable core module build/cores/choralroot.fm1core, exporting only fm1core_get
# the version: the firmware's own default FELUCCA_VERSION (cr_ui.c "ChoralRoot X"; the emulator unit itself shows
# "EMU"), the commit: the submodule's HEAD
set(CR_VERSION "dev")
file(STRINGS ${CR}/firmware/src/cr_ui.c _crv REGEX "^#define FELUCCA_VERSION \"")
if(_crv)
    list(GET _crv 0 _crv)
    string(REGEX REPLACE "^#define FELUCCA_VERSION \"(ChoralRoot )?([^\"]*)\".*" "\\2" CR_VERSION "${_crv}")
endif()
execute_process(COMMAND git -C ${CR} rev-parse HEAD OUTPUT_VARIABLE CR_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR NOT CR_COMMIT)
    set(CR_COMMIT "unknown")
endif()
message(STATUS "ChoralRoot core: version ${CR_VERSION}, commit ${CR_COMMIT}")

add_library(core_choralroot_obj OBJECT core-glue/choralroot/core_choralroot.c core-glue/choralroot/core_choralroot_desc.c)
target_include_directories(core_choralroot_obj PRIVATE ${CR_GEN} ${CR}/firmware/src ${CR}/tools/emu
                           ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
target_compile_options(core_choralroot_obj PRIVATE -O2 -fvisibility=hidden)
target_compile_definitions(core_choralroot_obj PRIVATE FM1CORE_CR_VERSION="${CR_VERSION}" FM1CORE_CR_COMMIT="${CR_COMMIT}")
set_target_properties(core_choralroot_obj PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
add_dependencies(core_choralroot_obj gen_choralroot)
# the firmware's own code: warnings off (as build.sh); the generated headers are included by the unit: rebuild it
# when they change
set_source_files_properties(core-glue/choralroot/core_choralroot.c PROPERTIES COMPILE_OPTIONS "-w"
                            OBJECT_DEPENDS "${CR_GEN_HEADERS}")
set_source_files_properties(core-glue/choralroot/core_choralroot_desc.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
find_library(MATH_LIB m)

add_library(core_choralroot STATIC $<TARGET_OBJECTS:core_choralroot_obj>)
target_include_directories(core_choralroot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
if(MATH_LIB)
    target_link_libraries(core_choralroot PUBLIC ${MATH_LIB})
endif()

add_library(choralroot_core MODULE $<TARGET_OBJECTS:core_choralroot_obj>)
set_target_properties(choralroot_core PROPERTIES PREFIX "" SUFFIX ".fm1core" OUTPUT_NAME choralroot
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cores)
target_link_options(choralroot_core PRIVATE -Wl,-exported_symbol,_fm1core_get)
if(MATH_LIB)
    target_link_libraries(choralroot_core PRIVATE ${MATH_LIB})
endif()

# ---- the engine (C++17, no JUCE): core loading, the device clock, the resampler
add_library(fm1engine STATIC engine/cores.cpp engine/device.cpp)
target_include_directories(fm1engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
target_compile_features(fm1engine PUBLIC cxx_std_17)
target_compile_options(fm1engine PRIVATE -Wall -Wextra)
target_link_libraries(fm1engine PUBLIC ${CMAKE_DL_LIBS})

# phase 2: JUCE via FetchContent

# ---- the reference emulator (build/host/emu inside the submodule, gitignored there): the replay test's ground
# truth. Built by the submodule's own tools/emu/build.sh (needs SDL2 from Homebrew) when missing.
option(FM1_REFERENCE_TESTS "build the reference emulator and run the replay tests against it" ON)
set(CR_EMU ${CR}/build/host/emu)
if(FM1_REFERENCE_TESTS AND NOT EXISTS /opt/homebrew/bin/sdl2-config AND NOT EXISTS ${CR_EMU})
    message(WARNING "SDL2 (Homebrew) not found: the reference emulator cannot be built; replay_test is skipped "
                    "(brew install sdl2, or -DFM1_REFERENCE_TESTS=OFF)")
    set(FM1_REFERENCE_TESTS OFF)
endif()
if(FM1_REFERENCE_TESTS)
    file(GLOB CR_EMU_SOURCES CONFIGURE_DEPENDS ${CR}/tools/emu/*.c ${CR}/tools/emu/*.h)
    add_custom_command(
        OUTPUT ${CR_EMU}
        COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib sh ${CR}/tools/emu/build.sh
        DEPENDS ${CR_GEN_HEADERS} ${CR_EMU_SOURCES}
        COMMENT "ChoralRoot: building the reference emulator (tools/emu/build.sh -> build/host/emu)"
        VERBATIM)
    add_custom_target(reference_emu ALL DEPENDS ${CR_EMU})
endif()

# ---- tests
enable_testing()
add_executable(core_smoke tests/core_smoke.c)
target_link_libraries(core_smoke PRIVATE core_choralroot)
add_test(NAME core_smoke COMMAND core_smoke)

add_library(fm1testlib STATIC tests/script_runner.cpp)
target_link_libraries(fm1testlib PUBLIC fm1engine)
target_include_directories(fm1testlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_options(fm1testlib PRIVATE -Wall -Wextra)
target_compile_definitions(fm1testlib PUBLIC
    FM1_CR_DIR="${CR}" FM1_CORE_MODULE="$<TARGET_FILE:choralroot_core>" FM1_REF_EMU="${CR_EMU}"
    FM1_REF_OUT="${CMAKE_BINARY_DIR}/reference")

add_executable(double_load_test tests/double_load_test.cpp)
target_link_libraries(double_load_test PRIVATE fm1testlib)
add_dependencies(double_load_test choralroot_core)
add_test(NAME double_load_test COMMAND double_load_test)
set_tests_properties(double_load_test PROPERTIES RESOURCE_LOCK fm1_reference)

add_executable(halt_test tests/halt_test.cpp)
target_link_libraries(halt_test PRIVATE fm1testlib)
add_dependencies(halt_test choralroot_core)
add_test(NAME halt_test COMMAND halt_test)

if(FM1_REFERENCE_TESTS)
    add_executable(replay_test tests/replay_test.cpp)
    target_link_libraries(replay_test PRIVATE fm1testlib)
    add_dependencies(replay_test choralroot_core reference_emu)
    add_test(NAME replay_test COMMAND replay_test)
    set_tests_properties(replay_test PROPERTIES RESOURCE_LOCK fm1_reference)
endif()
