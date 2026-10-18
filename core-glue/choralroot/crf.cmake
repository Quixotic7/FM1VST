# SPDX-License-Identifier: GPL-3.0-only
# The ChoralRoot family's shared CMake (included by each core_<id>.cmake here): the golden checks.
#
#   crf_golden(<id> SCRIPT <file> REF <emulator> [BUILD <cmd..>] [BUILD_DEPENDS <targets..>] [REF_TARGET <target>]
#              [AUDIO])
#
# crf_golden_<id> (ctest): the firmware's own headless emulator, REF (built from the same pinned submodule by its own
# script: BUILD, run in the submodule with Homebrew's SDL2, or the existing target REF_TARGET), and the core <id> run
# SCRIPT; every `shot` in it must be the same picture, and with AUDIO the same samples (crf_golden_test.cpp). The test
# executable and the tests are made at the end of the top-level directory (cmake_language DEFER): fm1testlib and
# enable_testing() come after the cores there.
include_guard(GLOBAL)
set(CRF_DIR ${CMAKE_CURRENT_LIST_DIR})

function(crf_golden id)
    cmake_parse_arguments(A "AUDIO" "SCRIPT;REF;REF_TARGET" "BUILD;BUILD_DEPENDS" ${ARGN})
    if(NOT TARGET ${id}_core)                     # (left out: a public build without a gated core)
        return()
    endif()
    if(NOT EXISTS /opt/homebrew/bin/sdl2-config AND NOT EXISTS ${A_REF})
        message(STATUS "${id}: no SDL2 for its reference emulator: crf_golden_${id} skipped")
        return()
    endif()
    get_filename_component(_src ${A_REF} DIRECTORY)
    get_filename_component(_src ${_src}/../.. ABSOLUTE)   # (REF is <submodule>/build/host/<exe>)
    if(A_BUILD)
        get_filename_component(_py ${CR_PYTHON} DIRECTORY)
        file(GLOB _emu_src CONFIGURE_DEPENDS ${_src}/tools/emu/*.c ${_src}/tools/emu/*.h)
        add_custom_command(OUTPUT ${A_REF}
            COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib PYTHON=${CR_PYTHON}
                    "PATH=${_py}:$ENV{PATH}" ${A_BUILD}
            WORKING_DIRECTORY ${_src}
            DEPENDS ${_emu_src} ${A_BUILD_DEPENDS}
            COMMENT "${id}: building its reference emulator (${A_BUILD})"
            VERBATIM)
        add_custom_target(crf_ref_${id} ALL DEPENDS ${A_REF})
        if(A_BUILD_DEPENDS)
            add_dependencies(crf_ref_${id} ${A_BUILD_DEPENDS})
        endif()
        set(_ref_target crf_ref_${id})
    elseif(A_REF_TARGET)
        set(_ref_target ${A_REF_TARGET})          # (checked at the end: the top-level defines it after the cores)
    else()
        message(STATUS "${id}: no reference emulator target: crf_golden_${id} skipped")
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY CRF_GOLDEN_IDS ${id})
    set_property(GLOBAL PROPERTY CRF_GOLDEN_${id} "${A_SCRIPT}|${A_REF}|${_src}|${_ref_target}|${A_AUDIO}")
endfunction()

function(crf_golden_tests)
    get_property(_ids GLOBAL PROPERTY CRF_GOLDEN_IDS)
    if(NOT _ids)
        return()
    endif()
    add_executable(crf_golden_test ${CRF_DIR}/crf_golden_test.cpp)
    target_link_libraries(crf_golden_test PRIVATE fm1testlib)
    target_compile_options(crf_golden_test PRIVATE -Wall -Wextra)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/crf_golden)
    foreach(_id IN LISTS _ids)
        get_property(_g GLOBAL PROPERTY CRF_GOLDEN_${_id})
        string(REPLACE "|" ";" _g "${_g}")
        list(GET _g 0 _script)
        list(GET _g 1 _ref)
        list(GET _g 2 _src)
        list(GET _g 3 _tgt)
        list(GET _g 4 _audio)
        if(NOT TARGET ${_tgt})
            message(STATUS "${_id}: no reference emulator (${_tgt}): crf_golden_${_id} skipped")
            continue()
        endif()
        set(_extra "")
        if(_audio)
            set(_extra --audio)
        endif()
        add_dependencies(crf_golden_test ${_id}_core ${_tgt})
        add_test(NAME crf_golden_${_id}
                 COMMAND crf_golden_test --module $<TARGET_FILE:${_id}_core> --ref ${_ref} --ref-cwd ${_src}
                         --script ${_script} --out ${CMAKE_BINARY_DIR}/crf_golden ${_extra})
        set_tests_properties(crf_golden_${_id} PROPERTIES TIMEOUT 300
                             ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/crf_golden/home")
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL crf_golden_tests)
