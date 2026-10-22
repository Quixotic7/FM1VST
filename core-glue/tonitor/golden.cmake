# SPDX-License-Identifier: GPL-3.0-only
# The Tonitor golden check (cmake -P; core.cmake): Tonitor's own host harness (REF: tests/host/host.c, built from the
# unmodified tree) writes its screens (`host screens OUT/ref`); tonitor_drive (DRIVE) runs the core (CORE) on
# golden.txt (SCRIPT) to the same states and compares its screen with those PPMs (DRIVE_ARGS; "%REF%" is OUT/ref).
# Fails on a difference outside the explained areas (core.cmake says which and why).
cmake_minimum_required(VERSION 3.22)
foreach(_v REF DRIVE CORE SCRIPT OUT)
    if(NOT ${_v})
        message(FATAL_ERROR "golden.cmake: ${_v} not set")
    endif()
endforeach()
file(REMOVE_RECURSE ${OUT})
file(MAKE_DIRECTORY ${OUT}/ref)
execute_process(COMMAND ${REF} screens ${OUT}/ref RESULT_VARIABLE _rc OUTPUT_FILE ${OUT}/ref.log ERROR_FILE ${OUT}/ref.log)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Tonitor's host harness failed (${_rc}): ${OUT}/ref.log")
endif()
separate_arguments(_args UNIX_COMMAND "${DRIVE_ARGS}")
string(REPLACE "%REF%" "${OUT}/ref" _args "${_args}")
execute_process(COMMAND ${DRIVE} ${CORE} ${SCRIPT} ${OUT}/ours ${_args} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out
                ERROR_VARIABLE _out)
message("${_out}")
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "the core's screen differs from Tonitor's own host harness (${OUT})")
endif()
