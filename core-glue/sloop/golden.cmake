# SPDX-License-Identifier: GPL-3.0-only
# A SLOOP-family golden check (cmake -P): the firmware's own host sim (REF, run in OUT/ref with REF_ARGS, stdin from
# REF_STDIN if given) writes its screens; sloop_drive (DRIVE) runs the core (CORE) on the same input script (SCRIPT)
# and compares its screen at the given ms with those PPMs (DRIVE_ARGS; "%REF%" is OUT/ref). Fails on a difference
# outside the explained areas (core_<id>.cmake says which and why).
cmake_minimum_required(VERSION 3.22)
foreach(_v REF DRIVE CORE SCRIPT OUT)
    if(NOT ${_v})
        message(FATAL_ERROR "golden.cmake: ${_v} not set")
    endif()
endforeach()
file(REMOVE_RECURSE ${OUT})
file(MAKE_DIRECTORY ${OUT}/ref)
separate_arguments(_ref_args UNIX_COMMAND "${REF_ARGS}")
string(REPLACE "%REF%" "${OUT}/ref" _ref_args "${_ref_args}")
if(REF_STDIN)
    execute_process(COMMAND ${REF} ${_ref_args} WORKING_DIRECTORY ${OUT}/ref INPUT_FILE ${REF_STDIN}
                    RESULT_VARIABLE _rc OUTPUT_FILE ${OUT}/ref.log ERROR_FILE ${OUT}/ref.log)
else()
    execute_process(COMMAND ${REF} ${_ref_args} WORKING_DIRECTORY ${OUT}/ref
                    RESULT_VARIABLE _rc OUTPUT_FILE ${OUT}/ref.log ERROR_FILE ${OUT}/ref.log)
endif()
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "the reference host sim failed (${_rc}): ${OUT}/ref.log")
endif()
separate_arguments(_args UNIX_COMMAND "${DRIVE_ARGS}")
string(REPLACE "%REF%" "${OUT}/ref" _args "${_args}")
execute_process(COMMAND ${DRIVE} ${CORE} ${SCRIPT} ${OUT}/ours ${_args} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out
                ERROR_VARIABLE _out)
message("${_out}")
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "the core's screen differs from the firmware's own host sim (${OUT})")
endif()
