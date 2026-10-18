# SPDX-License-Identifier: GPL-3.0-only
# The golden checks of the plat and fm1-base cores (core-glue/plat/README.md): each firmware's own host simulator,
# built here from the unmodified submodule as its host/build_host.sh (or fm1-base's build.py --host) builds it, and
# plat_golden_test, which runs a script through both and compares the screens. Included by the core_<id>.cmake files
# (core-glue/plat, core-glue/fm1base); the test runner is defined once.
include_guard(GLOBAL)

add_executable(plat_golden_test ${CMAKE_CURRENT_LIST_DIR}/plat_golden_test.cpp)
target_link_libraries(plat_golden_test PRIVATE fm1testlib)
target_compile_options(plat_golden_test PRIVATE -Wall -Wextra)

# plat_golden(<id> GEN_TARGET <gen_x> SOURCES <host sources..> [DEFINES ..] [INCLUDES ..] SCRIPT <file>
#             [ALLOWED <pixels>])
#   <id>_hostsim: the firmware's host simulator (-O2 -ffp-contract=off -std=c99, as its own build), after the core's
#   generate step; ctest plat_golden_<id>: SCRIPT through both (build/golden/<id>/: the PNGs, the core's PPMs)
function(plat_golden id)
    cmake_parse_arguments(A "" "GEN_TARGET;SCRIPT;ALLOWED" "SOURCES;DEFINES;INCLUDES" ${ARGN})
    if(NOT TARGET ${id}_core)
        return()                                   # (a gated core left out of a public build)
    endif()
    if(NOT A_ALLOWED)
        set(A_ALLOWED 0)
    endif()
    add_executable(${id}_hostsim ${A_SOURCES})
    target_include_directories(${id}_hostsim PRIVATE ${A_INCLUDES})
    target_compile_definitions(${id}_hostsim PRIVATE ${A_DEFINES})
    target_compile_options(${id}_hostsim PRIVATE -O2 -ffp-contract=off -w)
    set_target_properties(${id}_hostsim PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF
                          RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/golden)
    target_link_libraries(${id}_hostsim PRIVATE m)
    if(A_GEN_TARGET)
        add_dependencies(${id}_hostsim ${A_GEN_TARGET})
    endif()
    add_dependencies(plat_golden_test ${id}_core ${id}_hostsim)
    add_test(NAME plat_golden_${id}
             COMMAND plat_golden_test $<TARGET_FILE:${id}_core> $<TARGET_FILE:${id}_hostsim> ${A_SCRIPT}
                     ${CMAKE_BINARY_DIR}/golden/${id} ${A_ALLOWED})
    set_tests_properties(plat_golden_${id} PROPERTIES TIMEOUT 300)
endfunction()
