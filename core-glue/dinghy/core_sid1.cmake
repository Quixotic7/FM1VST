# SPDX-License-Identifier: GPL-3.0-only
# SID-1 1.0.0 (https://github.com/putchdev/sid-1, tag v1.0.0, 1d0555b): the Dinghy family glue (core-glue/dinghy/)
# over the unmodified submodule cores/sid-1, in its host mode (SID1_HOST). platform.c includes "../../tests/host.c"
# by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that path to sid1_host.c, as
# core_aurora.cmake does. sid.c (the reSID 0.16 port, its own unit on the device) is included by core_sid1.c after
# sid1.c. The headers in firmware/gen are committed (NO_GEN); the version is the tag's (tools/build.py --release
# 1.0.0: APP_VER "1.0.0", identity FM-1_960). The .pool section attribute (gfx.c) Mach-O rejects becomes an
# alignment (FLAGS).
# Public: the app GPL-3.0-only, the SID core GPL-2.0-or-later (reSID), the font SIL OFL 1.1 (LICENSING.md). The
# GPL-2.0 text is reSID's own COPYING, which the tree keeps with its reference copy (tests/ref/resid-0.16).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_sid1 ${CMAKE_SOURCE_DIR}/cores/sid-1)
set(_sid1_vfs ${CMAKE_BINARY_DIR}/dinghy/sid1_vfs.yaml)
file(WRITE ${_sid1_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_sid1}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/sid1_host.c\" } ] }\n")
fm1_add_core(sid1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sid1}
             UNIT core_sid1.c DESC core_sid1_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "SID-1 by putchdev, the SID-1 authors (the app, the patch player, the web page). Its SID core sid.c is a C port of reSID 0.16 by Dag Lem (GPL-2.0-or-later). On Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only as a whole. The screen's font is Press Start 2P by CodeMan38, SIL Open Font License 1.1. Commodore 64 is a trademark of its owners. The full notices: the release's licences/cores/sid1/ (LICENSING.md)."
             NO_GEN VERSION 1.0.0 DEFINES APP_VER="1.0.0"
             INCLUDES ${_sid1}/firmware/src ${_sid1}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_sid1_vfs}")
if(TARGET core_sid1_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_sid1.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/sid1_host.c ${_sid1_vfs})
endif()
dinghy_core(sid1 core_sid1.c)
# the golden check: the same script on its own host sim (tests/host.c, unchanged: sid1.c and sid.c, sid1_hostsim.c),
# the screens compared at 3000 ms (one of host.c's fixed SHOT times)
dinghy_golden_check(sid1 SCRIPT golden_sid1.txt AT 3000 SOURCE ${CMAKE_CURRENT_LIST_DIR}/sid1_hostsim.c
                    DEFINES SID1_HOST APP_VER="1.0.0"
                    INCLUDES ${_sid1}/firmware/src ${_sid1}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md, reSID's GPL-2.0 (sid.c) and the Press
# Start 2P font's OFL; Emscripten's and musl's are added by fm1_add_rfm1
fm1_add_rfm1(sid1 AUTHORS "putchdev (the SID-1 authors)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "GPL-2.0-or-later (reSID 0.16 by Dag Lem, ported as sid.c in {name})=tests/ref/resid-0.16/COPYING"
                           "OFL-1.1 (the Press Start 2P font in {name})=assets/fonts/PressStart2P-OFL.txt")
