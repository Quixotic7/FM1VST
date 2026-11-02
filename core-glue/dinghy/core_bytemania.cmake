# SPDX-License-Identifier: GPL-3.0-only
# Bytemania 1.2.0 (https://github.com/llozd/bytemania, tag v1.2.0, e670b35): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/bytemania, in its host mode (DINGHY_HOST). platform.c
# includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that
# path to bytemania_host.c, as core_aurora.cmake does. The headers in firmware/gen are committed (NO_GEN); the
# version is the tag's (tools/build.py --release 1.2.0: identity FM-1_942, APP_VER 1.2.0). The .pool section
# attribute is the device's linker's (-Dsection). Public: all of it is GPL-3.0-only, the fonts SIL OFL 1.1, the
# built-in formulas the classic published bytebeats, credited on the screen.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_bm ${CMAKE_SOURCE_DIR}/cores/bytemania)
set(_bm_vfs ${CMAKE_BINARY_DIR}/dinghy/bytemania_vfs.yaml)
file(WRITE ${_bm_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_bm}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/bytemania_host.c\" } ] }\n")
fm1_add_core(bytemania ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_bm}
             UNIT core_bytemania.c DESC core_bytemania_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "Bytemania by llozd (the bytebeat synth, its compiler, editor and views), built on Dinghy by zvenson (the app it grew from, the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. The built-in formulas are classic bytebeats by viznut, Niklas Roy, miiro, stephth, Rrrola, droid, Mu6k, krcko, Blueberry, ryg, Lord Graga, marmakoide, skurk, raer, erlehmann and others, credited on the screen. The fonts: Terminus Font (8x16, Dimitar Toshkov Zhekov) and Press Start 2P (The Press Start 2P Project Authors, as Bytemania Pixel), SIL Open Font License 1.1. The full notices: the release's licences/cores/bytemania/ (LICENSING.md)."
             CREDITS_FILES ${_bm}/assets/fonts/Terminus-LICENSE.txt ${_bm}/assets/fonts/PressStart2P-OFL.txt
             NO_GEN VERSION 1.2.0 DEFINES APP_VER="1.2.0"
             INCLUDES ${_bm}/firmware/src ${_bm}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_bm_vfs}")
if(TARGET core_bytemania_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_bytemania.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/bytemania_host.c ${_bm_vfs})
endif()
dinghy_core(bytemania core_bytemania.c)
# the golden check: the same script on its own host sim (tests/host.c, unchanged), the screens compared at 3000 ms
# (one of its fixed SHOT times)
dinghy_golden_check(bytemania SCRIPT golden_bytemania.txt AT 3000 SOURCE ${_bm}/firmware/src/bytemania.c
                    DEFINES DINGHY_HOST APP_VER="1.2.0"
                    INCLUDES ${_bm}/firmware/src ${_bm}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md and both fonts' OFL; Emscripten's and
# musl's are added by fm1_add_rfm1. rfm1_run plays its chord: no RUN_ARGS
fm1_add_rfm1(bytemania AUTHORS "llozd"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (Press Start 2P, as Bytemania Pixel, in {name})=assets/fonts/PressStart2P-OFL.txt")
