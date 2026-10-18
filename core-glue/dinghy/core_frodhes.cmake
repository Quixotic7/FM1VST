# SPDX-License-Identifier: GPL-3.0-only
# Frodhes 0.9.2 (https://github.com/leonardoMirabal/frodhes, tag v0.9.2): the Dinghy family glue (core-glue/dinghy/)
# over the unmodified submodule cores/frodhes, on its web route web/web_hal.c (DINGHY_HOST + DINGHY_WEB) with a stub
# <emscripten.h> (frodhes_inc/). frodhes_hook/ (-iquote: searched before the -I paths) gives MIDI out back to the
# glue (core_frodhes.c). The headers in firmware/gen are committed (NO_GEN); the version is tools/build.py's APP_VER.
# (gfx.c and fx.c keep their .pool attribute out of DINGHY_HOST builds themselves.)
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(frodhes ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/frodhes
             UNIT core_frodhes.c DESC core_frodhes_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             NO_GEN VERSION 0.9.2
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/frodhes/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
                      ${CMAKE_CURRENT_LIST_DIR}/frodhes_inc
             FLAGS "-iquote${CMAKE_CURRENT_LIST_DIR}/frodhes_hook")
dinghy_core(frodhes core_frodhes.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3000 ms
dinghy_golden_check(frodhes SCRIPT golden_frodhes.txt AT 3000 SOURCE ${CMAKE_SOURCE_DIR}/cores/frodhes/firmware/src/frodhes.c
                    DEFINES DINGHY_HOST APP_VER="0.9.2"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/frodhes/firmware/src ${CMAKE_SOURCE_DIR}/cores/frodhes/firmware/gen)
