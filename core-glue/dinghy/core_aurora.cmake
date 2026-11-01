# SPDX-License-Identifier: GPL-3.0-only
# AURORA 0.7.7 (https://github.com/twochannels/AURORA, tag v0.7.7, befdbfa): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/AURORA, on its own fake FM-1 tests/fake_fm1.h in host mode
# (DINGHY_HOST). platform.c includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so
# clang's -ivfsoverlay maps that path to aurora_host.c (the overlay file is written here, in the build tree), as
# core_fm106.cmake does. The headers in firmware/gen are committed (NO_GEN); the version is the VERSION file
# (tools/build.py's APP_VER). The .pool section attribute is the device branch's only (fake_fm1.h: FM1_POOL empty).
# Public: all of it is GPL-3.0-only, the font SIL OFL 1.1.
#
# THE TERMINUS TEXT: AURORA's LICENSING.md names assets/fonts/Terminus-LICENSE.txt, which its tree does not carry.
# licences/aurora/Terminus-LICENSE.txt here is the same font's notice (ter-u16n, Terminus 4.48, SIL OFL 1.1) as
# Felucca-lineage firmwares ship it (Velo / fm1-x0x's assets/fonts/Terminus-LICENSE.txt, byte for byte).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_aurora ${CMAKE_SOURCE_DIR}/cores/AURORA)
set(_aurora_vfs ${CMAKE_BINARY_DIR}/dinghy/aurora_vfs.yaml)
file(WRITE ${_aurora_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_aurora}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/aurora_host.c\" } ] }\n")
fm1_add_core(aurora ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_aurora}
             UNIT core_aurora.c DESC core_aurora_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "AURORA by twochannels (the app, the CS-80 voice, the browser emulator), on Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. The second core as an audio worker by PatttF (FM-106), after the AC79 SDK (Apache-2.0). MIDI IN on the TRS jack from Felucca as adapted in FM-106. The keys' velocity and pressure sensing (Velo) by Charles Vestal. GPL-3.0-only. The screen's font is Terminus Font (8x16, Dimitar Toshkov Zhekov), SIL Open Font License 1.1. The full notices: the release's licences/cores/aurora/ (LICENSING.md)."
             CREDITS_FILES ${CMAKE_CURRENT_LIST_DIR}/licences/aurora/Terminus-LICENSE.txt
             NO_GEN VERSION 0.7.7 DEFINES APP_VER="0.7.7"
             INCLUDES ${_aurora}/firmware/src ${_aurora}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-ivfsoverlay" "${_aurora_vfs}")
if(TARGET core_aurora_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_aurora.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/aurora_host.c ${_aurora_vfs})
endif()
dinghy_core(aurora core_aurora.c)
# the golden check: the same script on its own host sim (tests/host.c, unchanged), the screens compared at 3000 ms
# (one of its fixed SHOT times)
dinghy_golden_check(aurora SCRIPT golden_aurora.txt AT 3000 SOURCE ${_aurora}/firmware/src/aurora.c
                    DEFINES DINGHY_HOST APP_VER="0.7.7"
                    INCLUDES ${_aurora}/firmware/src ${_aurora}/firmware/gen)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md and the Terminus font's OFL (FM1VST's
# copy, above); Emscripten's and musl's are added by fm1_add_rfm1
fm1_add_rfm1(aurora AUTHORS "twochannels"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=${CMAKE_CURRENT_LIST_DIR}/licences/aurora/Terminus-LICENSE.txt")
