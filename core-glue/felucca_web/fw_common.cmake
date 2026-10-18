# SPDX-License-Identifier: GPL-3.0-only
# The felucca_web family's shared CMake (included by each core_<id>.cmake; not picked up by the top-level glob).
#
# fw_golden(<id> <source dir> <web unit, source relative> [defines..]): the golden check (CORES-PLAN.md 4, the brief's
# "golden check"): fw_golden_<id> runs the loaded core (<id>.fm1core, through the engine's Device as the plugin drives
# it) and the firmware's own host build, its web/emu/felucca_web.c compiled natively as its author's native_check.c
# does (fw_ref.c), side by side from power-on with the same input script (fw_golden.cpp), and compares the screen at
# several points and the whole audio stream, sample for sample. ctest -R fw_golden.
include_guard(GLOBAL)
set(FW_GLUE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(fw_golden id src webc)
    if(NOT TARGET ${id}_core)
        return()                                   # (left out: a gated core in a public build)
    endif()
    set(ref fw_ref_${id})
    add_library(${ref} STATIC ${FW_GLUE_DIR}/fw_ref.c)
    target_include_directories(${ref} PRIVATE ${src}/build/gen ${src}/firmware/src)
    target_compile_definitions(${ref} PRIVATE FW_REF_WEB_C="${src}/${webc}" ${ARGN})
    # (the firmware's own code: warnings off, as its native_check.c builds it)
    target_compile_options(${ref} PRIVATE -O2 -w -fno-common)
    set_target_properties(${ref} PROPERTIES C_STANDARD 11)
    add_dependencies(${ref} gen_${id})
    add_executable(fw_golden_${id} ${FW_GLUE_DIR}/fw_golden.cpp)
    target_link_libraries(fw_golden_${id} PRIVATE fm1testlib ${ref})
    target_compile_options(fw_golden_${id} PRIVATE -Wall -Wextra)
    add_dependencies(fw_golden_${id} ${id}_core)
    add_test(NAME fw_golden_${id} COMMAND fw_golden_${id} $<TARGET_FILE:${id}_core> ${id}
             ${CMAKE_BINARY_DIR}/conformance)
    set_tests_properties(fw_golden_${id} PROPERTIES TIMEOUT 300
                         ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home")
endfunction()
