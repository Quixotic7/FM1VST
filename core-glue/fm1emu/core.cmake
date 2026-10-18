# SPDX-License-Identifier: GPL-3.0-only
# The fm1emu core (core-glue/fm1emu/README.md): fm1-emu's emulated FM-1 (cores/fm1-emu, MIT, unmodified) as a core
# module, a Rust cdylib (this folder's Cargo.toml) built by cargo, not by fm1_add_core (which compiles C units): the
# module is copied to <build>/cores/fm1emu.fm1core, next to the others, where CoreLoader finds it.
#
# Not in FM1_CORE_IDS (yet): the per-core loops of the top-level CMakeLists (the bundles, core_smoke, param_test,
# core_bench_run) expect a core that sounds without a firmware file. Its tests are registered here, at the end of the
# top-level directory (cmake_language DEFER: core_conformance and fm1testlib exist then):
#   core_conformance_fm1emu_felucca / _mvave   tests/core_conformance.cpp with FM1EMU_FWSC = Felucca 1.6.1 / V15
#   fm1emu_test                                two instances with different packages, the image's round trip,
#                                              latency, LEDs, memory (tests/fm1emu_test.cpp)
# and tests/fm1emu_bench.sh runs core_bench per package (the RTF table in the README).
find_program(FM1_CARGO cargo HINTS /opt/homebrew/bin $ENV{HOME}/.cargo/bin)
if(NOT FM1_CARGO)
    message(WARNING "fm1emu core: no cargo found: the core is not built")
    return()
endif()
set(FM1EMU_GLUE ${CMAKE_CURRENT_LIST_DIR})
set(FM1EMU_SRC ${CMAKE_SOURCE_DIR}/cores/fm1-emu)
if(NOT EXISTS ${FM1EMU_SRC}/crates/fm1-soc/src/lib.rs)
    message(FATAL_ERROR "${FM1EMU_SRC} is empty: run git submodule update --init cores/fm1-emu")
endif()
execute_process(COMMAND git -C ${FM1EMU_SRC} rev-parse HEAD OUTPUT_VARIABLE FM1EMU_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR NOT FM1EMU_COMMIT)
    set(FM1EMU_COMMIT "unknown")
endif()
message(STATUS "fm1emu core: fm1-emu ${FM1EMU_COMMIT}, family fm1emu, MIT (Rust, ${FM1_CARGO})")

set(FM1EMU_CARGO_DIR ${CMAKE_BINARY_DIR}/fm1emu-cargo)
set(FM1EMU_MODULE ${CMAKE_BINARY_DIR}/cores/fm1emu.fm1core)
file(GLOB_RECURSE _fm1emu_deps CONFIGURE_DEPENDS ${FM1EMU_GLUE}/src/*.rs ${FM1EMU_SRC}/crates/fm1-core/src/*.rs
     ${FM1EMU_SRC}/crates/fm1-soc/src/*.rs ${FM1EMU_SRC}/crates/fm1-isa/src/*.rs)
add_custom_command(
    OUTPUT ${FM1EMU_MODULE}
    COMMAND ${CMAKE_COMMAND} -E env FM1EMU_COMMIT=${FM1EMU_COMMIT} CARGO_TARGET_DIR=${FM1EMU_CARGO_DIR}
            ${FM1_CARGO} build --release --manifest-path ${FM1EMU_GLUE}/Cargo.toml
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/cores
    COMMAND ${CMAKE_COMMAND} -E copy ${FM1EMU_CARGO_DIR}/release/libfm1emu_core.dylib ${FM1EMU_MODULE}
    DEPENDS ${_fm1emu_deps} ${FM1EMU_GLUE}/Cargo.toml
    COMMENT "fm1emu: cargo build --release (fm1-emu ${FM1EMU_COMMIT}) -> cores/fm1emu.fm1core"
    VERBATIM)
add_custom_target(fm1emu_core ALL DEPENDS ${FM1EMU_MODULE})

# the packages the tests boot (FM1EMU_FWSC): machine-local files, not in the repo
set(FM1EMU_FELUCCA_FWSC ${CMAKE_SOURCE_DIR}/build-fm1emu/fwsc/felucca-1.6.1.fwsc CACHE FILEPATH
    "Felucca 1.6.1's update package, for core_conformance_fm1emu_felucca")
set(FM1EMU_MVAVE_FWSC /Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/MvaveFM1Unbricker/FM-1.fwsc CACHE FILEPATH
    "M-VAVE's stock FM-1 V15 update package, for core_conformance_fm1emu_mvave")

function(fm1emu_tests)
    add_dependencies(core_conformance fm1emu_core)
    foreach(_n felucca mvave)
        string(TOUPPER ${_n} _u)
        set(_pkg ${FM1EMU_${_u}_FWSC})
        if(NOT EXISTS ${_pkg})
            message(STATUS "fm1emu core: ${_pkg} not found: core_conformance_fm1emu_${_n} not registered")
            continue()
        endif()
        set(_out ${CMAKE_BINARY_DIR}/conformance/fm1emu_${_n})
        file(MAKE_DIRECTORY ${_out})
        add_test(NAME core_conformance_fm1emu_${_n}
                 COMMAND core_conformance ${FM1EMU_MODULE} fm1emu fm1emu MIT ${FM1EMU_GLUE}/conformance_fm1emu_${_n}.txt
                         ${_out})
        set_tests_properties(core_conformance_fm1emu_${_n} PROPERTIES TIMEOUT 600
            ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home;FM1EMU_FWSC=${_pkg}")
    endforeach()
    add_executable(fm1emu_test ${CMAKE_SOURCE_DIR}/tests/fm1emu_test.cpp)
    target_link_libraries(fm1emu_test PRIVATE fm1testlib)
    target_compile_options(fm1emu_test PRIVATE -Wall -Wextra)
    add_dependencies(fm1emu_test fm1emu_core)
    if(EXISTS ${FM1EMU_FELUCCA_FWSC} AND EXISTS ${FM1EMU_MVAVE_FWSC})
        add_test(NAME fm1emu_test COMMAND fm1emu_test ${FM1EMU_MODULE} ${FM1EMU_FELUCCA_FWSC} ${FM1EMU_MVAVE_FWSC}
                 $<TARGET_FILE:felucca_core> ${CMAKE_BINARY_DIR}/conformance/fm1emu_test)
        set_tests_properties(fm1emu_test PROPERTIES TIMEOUT 600
            ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home")
        add_dependencies(fm1emu_test felucca_core)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/conformance/fm1emu_test)
    endif()
endfunction()
cmake_language(DEFER CALL fm1emu_tests)
