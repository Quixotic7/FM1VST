# SPDX-License-Identifier: GPL-3.0-only
# The SLOOP family (core-glue/sloop): what its cores share. The cores themselves are core_<id>.cmake (sloop, sloopdx,
# orbit, nomansky), found by the top-level glob after this file.
# sloop_drive: a core driven by a script, its screen as PPM (the golden checks' FM1VST side; a development tool)
add_executable(sloop_drive ${CMAKE_CURRENT_LIST_DIR}/sloop_drive.cpp)
target_link_libraries(sloop_drive PRIVATE fm1testlib)
target_compile_options(sloop_drive PRIVATE -Wall -Wextra)
