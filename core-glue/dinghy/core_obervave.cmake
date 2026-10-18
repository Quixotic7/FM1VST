# SPDX-License-Identifier: GPL-3.0-only
# OberVave 1.5 (https://github.com/zvenson/obervave, tag v1.5): the Dinghy family glue (core-glue/dinghy/) over the
# unmodified submodule cores/obervave, on its own host shim tests/host.c (OBV_HOST + OBV_WASM, as web/sim/sim.c).
# The headers in firmware/gen are committed (NO_GEN); the version is tools/build.py's APP_VER (not in a source file).
# The .pool section attribute (gfx.c, obv_fx.h, obervave.c) Mach-O rejects becomes an alignment (FLAGS).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(obervave ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/obervave
             UNIT core_obervave.c DESC core_obervave_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             NO_GEN VERSION 1.5
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)")
dinghy_core(obervave core_obervave.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3000 ms
dinghy_golden_check(obervave SCRIPT golden_obervave.txt AT 3000 SOURCE ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/src/obervave.c
                    DEFINES OBV_HOST APP_VER="1.5"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/src ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
