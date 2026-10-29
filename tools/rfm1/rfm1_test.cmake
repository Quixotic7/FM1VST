# SPDX-License-Identifier: GPL-3.0-only
# ctest rfm1_<id> (cmake/FM1Rfm1.cmake): the file against the format (tools/rfm1_check.py), then booted and played
# (rfm1_run). cmake -DPYTHON=.. -DCHECK=.. -DRUN=.. -DFILE=.. [-DRUN_ARGS=..] -P rfm1_test.cmake
if(NOT EXISTS ${FILE})
    message(FATAL_ERROR "${FILE} not built (cmake --build <build> --target rfm1_all)")
endif()
execute_process(COMMAND ${PYTHON} ${CHECK} ${FILE} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rfm1_check failed (${rc})")
endif()
separate_arguments(_args UNIX_COMMAND "${RUN_ARGS}")
execute_process(COMMAND ${RUN} ${_args} ${FILE} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rfm1_run failed (${rc})")
endif()
