# SPDX-License-Identifier: GPL-3.0-only
# The SLOOP family (core-glue/sloop): what its cores share. The cores themselves are core_<id>.cmake (sloop, sloopdx,
# orbit, nomansky), found by the top-level glob after this file.
# sloop_drive: a core driven by a script, its screen as PPM (the golden checks' FM1VST side; a development tool)
add_executable(sloop_drive ${CMAKE_CURRENT_LIST_DIR}/sloop_drive.cpp)
target_link_libraries(sloop_drive PRIVATE fm1testlib)
target_compile_options(sloop_drive PRIVATE -Wall -Wextra)

# The golden checks (CORES-BRIEF: where the firmware has its own host sim, drive both with the same input script and
# compare the screen): the firmware's host program built from its unmodified tree (fm1_sloop_ref), then golden.cmake.
#   fm1_sloop_ref(<target> <source file> <firmware root> <gen target> [FLAGS <flags..>] [INCLUDES <dirs..>])
function(fm1_sloop_ref name src root gen)
    cmake_parse_arguments(R "" "" "FLAGS;INCLUDES" ${ARGN})
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${root}/build/gen ${root}/firmware/src ${root}/firmware/hal
                               ${R_INCLUDES})
    target_compile_options(${name} PRIVATE -O2 -w ${R_FLAGS})
    if(MATH_LIB)
        target_link_libraries(${name} PRIVATE ${MATH_LIB})
    endif()
    add_dependencies(${name} ${gen})
endfunction()
#   fm1_sloop_golden(<id> REF <target> [REF_ARGS <args..>] [REF_STDIN <file>] SCRIPT <file> DRIVE_ARGS <args..>)
#   (in REF_ARGS / DRIVE_ARGS, %REF% is the reference's output folder)
function(fm1_sloop_golden id)
    cmake_parse_arguments(G "" "REF;REF_STDIN;SCRIPT" "REF_ARGS;DRIVE_ARGS" ${ARGN})
    string(JOIN " " _ra ${G_REF_ARGS})
    string(JOIN " " _da ${G_DRIVE_ARGS})
    add_test(NAME golden_${id}
             COMMAND ${CMAKE_COMMAND} -DREF=$<TARGET_FILE:${G_REF}> "-DREF_ARGS=${_ra}" "-DREF_STDIN=${G_REF_STDIN}"
                     -DDRIVE=$<TARGET_FILE:sloop_drive> -DCORE=$<TARGET_FILE:${id}_core> -DSCRIPT=${G_SCRIPT}
                     -DOUT=${CMAKE_BINARY_DIR}/golden/${id} "-DDRIVE_ARGS=${_da}"
                     -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/golden.cmake)
    set_tests_properties(golden_${id} PROPERTIES TIMEOUT 300)
endfunction()
