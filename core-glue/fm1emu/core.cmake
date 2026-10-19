# SPDX-License-Identifier: GPL-3.0-only
# The fm1emu core (core-glue/fm1emu/README.md): fm1-emu's emulated FM-1 (cores/fm1-emu, MIT) as a core module, a Rust
# cdylib (this folder's Cargo.toml) built by cargo, not by fm1_add_core (which compiles C units): the module is copied
# to <build>/cores/fm1emu.fm1core, next to the others, where CoreLoader finds it.
#
# THE PATCHES. cargo builds a copy of the commit the superproject pins for cores/fm1-emu (git archive), in
# <build>/fm1emu-src, with patches/*.patch applied in name order (git apply). The submodule itself is never modified.
# A new patch is a file dropped into patches/ (NNNN-*.patch, `git format-patch` against the pinned commit): the next
# configure (or build: the folder is a configure dependency) applies it, and the descriptor's version names it
# ("fm1-emu 5fbb066 + 0001"). When upstream takes a fix, bump the pin and delete its patch. The copy is redone only when
# the pin or a patch changes (a stamp). FM1EMU_PATCHES=OFF: the plain pinned commit.
# The crate is built from a manifest generated in <build>/fm1emu-crate (this folder's Cargo.toml with its fm1-emu
# paths pointed at the patched copy); this folder's own Cargo.toml still builds against the submodule, unpatched
# (cargo test of the glue alone).
#
# It is bundled (FM1_EXTRA_CORE_IDS: the bundles, the release manifest, FM1_BUILT_CORES) in every build, the public one
# too: the emulator is MIT and the module carries no firmware (a package is the user's pick, downloaded at run time:
# plugin/Hub.h). It is not in FM1_CORE_IDS: the per-core loops of the top-level CMakeLists (core_smoke, param_test,
# core_conformance, core_bench_run) expect a core that sounds without a package. Its tests are registered here, at
# the end of the top-level directory (cmake_language DEFER: core_conformance and fm1testlib exist then):
#   core_conformance_fm1emu_felucca / _mvave   tests/core_conformance.cpp with FM1EMU_FWSC = Felucca 1.6.1 / V15
#   fm1emu_test                                two instances with different packages, the image's round trip,
#                                              latency, the clock, Melodee (patch 0001) (tests/fm1emu_test.cpp)
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
# the pinned commit: the superproject's gitlink (the submodule's checkout may be elsewhere), else its HEAD
execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR} ls-files -s cores/fm1-emu OUTPUT_VARIABLE _ls
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(REGEX MATCH "^160000 ([0-9a-f]+) " _m "${_ls}")
set(FM1EMU_COMMIT "${CMAKE_MATCH_1}")
if(NOT FM1EMU_COMMIT)
    execute_process(COMMAND git -C ${FM1EMU_SRC} rev-parse HEAD OUTPUT_VARIABLE FM1EMU_COMMIT
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT FM1EMU_COMMIT)
    message(FATAL_ERROR "fm1emu core: cannot tell the commit cores/fm1-emu is pinned at")
endif()

# ---- the patched copy
option(FM1EMU_PATCHES "apply core-glue/fm1emu/patches/*.patch over fm1-emu's pinned commit" ON)
set(_patches "")
if(FM1EMU_PATCHES)
    file(GLOB _patches CONFIGURE_DEPENDS ${FM1EMU_GLUE}/patches/*.patch)
    list(SORT _patches)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_patches})
set(FM1EMU_COPY ${CMAKE_BINARY_DIR}/fm1emu-src)
set(_stamp "${FM1EMU_COMMIT}")
set(FM1EMU_PATCH_IDS "")
foreach(_p IN LISTS _patches)
    file(SHA256 ${_p} _h)
    get_filename_component(_n ${_p} NAME)
    string(APPEND _stamp ";${_n}=${_h}")
    string(REGEX MATCH "^[0-9]+" _num "${_n}")
    list(APPEND FM1EMU_PATCH_IDS ${_num})
endforeach()
set(_stamp_file ${FM1EMU_COPY}/.fm1vst-stamp)
set(_old "")
if(EXISTS ${_stamp_file})
    file(READ ${_stamp_file} _old)
endif()
if(NOT _old STREQUAL _stamp OR NOT EXISTS ${FM1EMU_COPY}/crates/fm1-soc/src/lib.rs)
    file(REMOVE_RECURSE ${FM1EMU_COPY})
    file(MAKE_DIRECTORY ${FM1EMU_COPY})
    execute_process(COMMAND git -C ${FM1EMU_SRC} archive ${FM1EMU_COMMIT}
                    COMMAND tar -x -C ${FM1EMU_COPY}
                    RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "fm1emu core: git archive ${FM1EMU_COMMIT} of ${FM1EMU_SRC} failed: ${_err}")
    endif()
    # (a repository of its own: inside ours, git apply would apply relative to our root and skip every path)
    execute_process(COMMAND git -C ${FM1EMU_COPY} init -q RESULT_VARIABLE _rc)
    foreach(_p IN LISTS _patches)
        execute_process(COMMAND git -C ${FM1EMU_COPY} apply --whitespace=nowarn ${_p} RESULT_VARIABLE _rc ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            file(REMOVE_RECURSE ${FM1EMU_COPY})
            message(FATAL_ERROR "fm1emu core: ${_p} does not apply to fm1-emu ${FM1EMU_COMMIT}: ${_err}")
        endif()
        get_filename_component(_n ${_p} NAME)
        message(STATUS "fm1emu core: patch ${_n}")
    endforeach()
    file(WRITE ${_stamp_file} "${_stamp}")
endif()
string(REPLACE ";" " " _patch_ids_sp "${FM1EMU_PATCH_IDS}")
string(SUBSTRING "${FM1EMU_COMMIT}" 0 7 _short)
message(STATUS "fm1emu core: fm1-emu ${_short}, patches: ${_patch_ids_sp}, family fm1emu, MIT (Rust, ${FM1_CARGO})")

# ---- the crate's manifest, pointed at the copy
set(FM1EMU_CRATE ${CMAKE_BINARY_DIR}/fm1emu-crate)
file(READ ${FM1EMU_GLUE}/Cargo.toml _toml)
string(REPLACE "path = \"src/lib.rs\"" "path = \"${FM1EMU_GLUE}/src/lib.rs\"" _toml "${_toml}")
string(REPLACE "../../cores/fm1-emu/" "${FM1EMU_COPY}/" _toml "${_toml}")
set(_toml "# generated by core-glue/fm1emu/core.cmake from core-glue/fm1emu/Cargo.toml: do not edit\n${_toml}")
set(_old "")
if(EXISTS ${FM1EMU_CRATE}/Cargo.toml)
    file(READ ${FM1EMU_CRATE}/Cargo.toml _old)
endif()
if(NOT _old STREQUAL _toml)
    file(WRITE ${FM1EMU_CRATE}/Cargo.toml "${_toml}")
endif()
configure_file(${FM1EMU_GLUE}/Cargo.lock ${FM1EMU_CRATE}/Cargo.lock COPYONLY)

set(FM1EMU_CARGO_DIR ${CMAKE_BINARY_DIR}/fm1emu-cargo)
set(FM1EMU_MODULE ${CMAKE_BINARY_DIR}/cores/fm1emu.fm1core)
file(GLOB_RECURSE _fm1emu_deps CONFIGURE_DEPENDS ${FM1EMU_GLUE}/src/*.rs ${FM1EMU_COPY}/crates/fm1-core/src/*.rs
     ${FM1EMU_COPY}/crates/fm1-soc/src/*.rs ${FM1EMU_COPY}/crates/fm1-isa/src/*.rs)
set(_env FM1EMU_COMMIT=${FM1EMU_COMMIT} "FM1EMU_PATCHES=${_patch_ids_sp}" CARGO_TARGET_DIR=${FM1EMU_CARGO_DIR})
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND _env MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()
add_custom_command(
    OUTPUT ${FM1EMU_MODULE}
    COMMAND ${CMAKE_COMMAND} -E env ${_env} ${FM1_CARGO} build --release --manifest-path ${FM1EMU_CRATE}/Cargo.toml
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/cores
    COMMAND ${CMAKE_COMMAND} -E copy ${FM1EMU_CARGO_DIR}/release/libfm1emu_core.dylib ${FM1EMU_MODULE}
    DEPENDS ${_fm1emu_deps} ${FM1EMU_CRATE}/Cargo.toml ${FM1EMU_GLUE}/Cargo.toml ${_stamp_file}
    COMMENT "fm1emu: cargo build --release (fm1-emu ${_short} + patches ${_patch_ids_sp}) -> cores/fm1emu.fm1core"
    VERBATIM)
add_custom_target(fm1emu_core ALL DEPENDS ${FM1EMU_MODULE})

# bundled with the cores (CMakeLists.txt: the bundles, the release manifest, FM1_BUILT_CORES)
list(APPEND FM1_EXTRA_CORE_IDS fm1emu)
list(APPEND FM1_EXTRA_CORE_TARGETS fm1emu_core)
list(APPEND FM1_EXTRA_CORE_FILES ${FM1EMU_MODULE})
set(_v "fm1-emu ${_short}")
if(_patch_ids_sp)
    string(REPLACE " " "+" _pp "${_patch_ids_sp}")
    set(_v "${_v} + ${_pp}")
endif()
set_property(GLOBAL PROPERTY FM1_CORE_GLUE_fm1emu ${FM1EMU_GLUE})
set_property(GLOBAL PROPERTY FM1_CORE_FAMILY_fm1emu fm1emu)
set_property(GLOBAL PROPERTY FM1_CORE_LICENSE_fm1emu MIT)
set_property(GLOBAL PROPERTY FM1_CORE_VERSION_fm1emu "${_v}")
set_property(GLOBAL PROPERTY FM1_CORE_COMMIT_fm1emu "${FM1EMU_COMMIT}")
set_property(GLOBAL PROPERTY FM1_CORE_SOURCE_fm1emu "${FM1EMU_SRC}")

# the packages the tests boot (FM1EMU_FWSC): machine-local files, not in the repo
set(FM1EMU_FELUCCA_FWSC ${CMAKE_SOURCE_DIR}/build-fm1emu/fwsc/felucca-1.6.1.fwsc CACHE FILEPATH
    "Felucca 1.6.1's update package, for core_conformance_fm1emu_felucca")
set(FM1EMU_MVAVE_FWSC /Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/MvaveFM1Unbricker/FM-1.fwsc CACHE FILEPATH
    "M-VAVE's stock FM-1 V15 update package, for core_conformance_fm1emu_mvave")
set(FM1EMU_MELODEE_FWSC ${CMAKE_SOURCE_DIR}/build-fm1emu/fwsc/melodee-1.4.4.fwsc CACHE FILEPATH
    "Melodee 1.4.4's update package (the Hub's), for fm1emu_test: it boots only with patch 0001")

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
    if(EXISTS ${FM1EMU_FELUCCA_FWSC} AND EXISTS ${FM1EMU_MVAVE_FWSC} AND TARGET felucca_core)
        set(_mel "")
        if(EXISTS ${FM1EMU_MELODEE_FWSC})
            set(_mel --melodee ${FM1EMU_MELODEE_FWSC})
        else()
            message(STATUS "fm1emu core: ${FM1EMU_MELODEE_FWSC} not found: fm1emu_test skips Melodee")
        endif()
        add_test(NAME fm1emu_test COMMAND fm1emu_test ${FM1EMU_MODULE} ${FM1EMU_FELUCCA_FWSC} ${FM1EMU_MVAVE_FWSC}
                 $<TARGET_FILE:felucca_core> ${CMAKE_BINARY_DIR}/conformance/fm1emu_test ${_mel})
        set_tests_properties(fm1emu_test PROPERTIES TIMEOUT 600
            ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home")
        add_dependencies(fm1emu_test felucca_core)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/conformance/fm1emu_test)
    endif()
endfunction()
cmake_language(DEFER CALL fm1emu_tests)
