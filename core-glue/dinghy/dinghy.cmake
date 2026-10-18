# SPDX-License-Identifier: GPL-3.0-only
# The Dinghy family's shared CMake (included by each core_<id>.cmake here, first): the glue headers as dependencies
# of the units, and the golden check against each firmware's own host sim (dinghy_golden.c).
include_guard(GLOBAL)
set(DINGHY_GLUE ${CMAKE_CURRENT_LIST_DIR})
set(DINGHY_GLUE_HEADERS ${DINGHY_GLUE}/dinghy_pre.h ${DINGHY_GLUE}/dinghy_glue.h)

# the golden driver: loads a core module, runs the host sim, compares the screens
add_executable(dinghy_golden ${DINGHY_GLUE}/dinghy_golden.c)
target_include_directories(dinghy_golden PRIVATE ${CMAKE_SOURCE_DIR}/core-api ${FM1_COMMON_DIR})
target_compile_options(dinghy_golden PRIVATE -Wall -Wextra)
target_link_libraries(dinghy_golden PRIVATE ${CMAKE_DL_LIBS})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/dinghy/golden)

# dinghy_core(<id>): the glue headers rebuild the unit; dinghy_golden_check(<id> SCRIPT <file> AT <ms> SOURCE <the
# firmware's main .c> [DEFINES ..] [FLAGS ..] [INCLUDES ..]): <id>_hostsim (the firmware's own tests/host.c build,
# from the same tree) and the test golden_<id>
function(dinghy_core id unit)
    if(TARGET core_${id}_obj)
        set_property(SOURCE ${DINGHY_GLUE}/${unit} APPEND PROPERTY OBJECT_DEPENDS ${DINGHY_GLUE_HEADERS})
    endif()
endfunction()
function(dinghy_golden_check id)
    cmake_parse_arguments(G "" "SCRIPT;AT;SOURCE" "DEFINES;FLAGS;INCLUDES" ${ARGN})
    if(NOT TARGET ${id}_core)
        return()                                          # (left out: a public build without the gated cores)
    endif()
    add_executable(${id}_hostsim ${G_SOURCE})
    target_include_directories(${id}_hostsim PRIVATE ${G_INCLUDES})
    target_compile_definitions(${id}_hostsim PRIVATE ${G_DEFINES})
    target_compile_options(${id}_hostsim PRIVATE -O2 -w ${G_FLAGS})
    if(MATH_LIB)
        target_link_libraries(${id}_hostsim PRIVATE ${MATH_LIB})
    endif()
    add_dependencies(dinghy_golden ${id}_hostsim ${id}_core)
    add_test(NAME golden_${id}
             COMMAND dinghy_golden $<TARGET_FILE:${id}_core> $<TARGET_FILE:${id}_hostsim> ${DINGHY_GLUE}/${G_SCRIPT}
                     ${G_AT} ${CMAKE_BINARY_DIR}/dinghy/golden)
endfunction()
