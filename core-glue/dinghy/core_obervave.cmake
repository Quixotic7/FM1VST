# SPDX-License-Identifier: GPL-3.0-only
# OberVave 3.3.2 (https://github.com/zvenson/obervave, tag v3.3.2, 792535d):
# the Dinghy family glue (core-glue/dinghy/) over the unmodified submodule cores/obervave, on its own host shim
# tests/host.c (OBV_HOST + OBV_WASM, as web/sim/sim.c).
# The headers in firmware/gen are committed (NO_GEN); the version is tools/build.py's APP_VER (not in a source file).
# The .pool section attribute (gfx.c, obv_fx.h, obervave.c) Mach-O rejects becomes an alignment (FLAGS).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(obervave ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/obervave
             UNIT core_obervave.c DESC core_obervave_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             NO_GEN VERSION 3.3.2
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)")
dinghy_core(obervave core_obervave.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3000 ms; 3.3.2's FM-1
# Mirror (obv_mirror.h) needs the device's usb.c / ota.c, which host.c lacks: obervave_mirror_host.h keeps it idle
dinghy_golden_check(obervave SCRIPT golden_obervave.txt AT 3000 SOURCE ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/src/obervave.c
                    DEFINES OBV_HOST APP_VER="3.3.2" OBV_MIRROR_HOST_STUBS
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/src ${CMAKE_SOURCE_DIR}/cores/obervave/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)" -include ${CMAKE_CURRENT_LIST_DIR}/obervave_mirror_host.h)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md, the Terminus font's OFL and (3.3.2)
# the screen mirror's MIT (Patrick F. Kellogg, compiled in though idle here)
fm1_add_rfm1(obervave AUTHORS "zvenson"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "MIT (the screen mirror in {name})=firmware/src/screen_mirror/LICENSE")
