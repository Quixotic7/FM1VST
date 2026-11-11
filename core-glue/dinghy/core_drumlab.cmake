# SPDX-License-Identifier: GPL-3.0-only
# Drumlab 0.1.3-beta (https://github.com/pbarilla/drumlab, tag v0.1.3-beta, 011bde0: the default branch's head and
# the Firmware Hub's latest; no stable release yet): the Dinghy family glue (core-glue/dinghy/) over the unmodified
# submodule cores/drumlab, built from its own entry point firmware/src/lab.c (tools/build.py, web/build.sh) in its host
# mode (CHIPBOX_HOST, the name Drumlab kept from Chipbox), as core_chipbox.cmake does for its parent. platform.c
# includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that
# path to drumlab_host.c. The same overlay maps two files to copies written here, in the build tree, with the device's
# code on where the host mode leaves it out (the rest of each file unchanged):
#   firmware/src/labio.c   the Projects page's SysEx (proj_io, app_install_mark, behind #ifndef CHIPBOX_HOST)
#   firmware/src/lab.c     the main loop's open of a project that came in over USB (px_open_req)
# The power-on screen is lab.c's own LAB_BOOT (core_drumlab.c). The headers in firmware/gen are committed and
# tools/build.py runs no generator (NO_GEN); the version is the tag's (build.py --release 0.1.3-beta: APP_VER "0.1.3").
# Public: all of it is GPL-3.0-only, the font SIL OFL 1.1. Not built: firmware/ble/ (the device's radio unit).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_dl ${CMAKE_SOURCE_DIR}/cores/drumlab)
function(_dl_overlay src dst)                             # src with each FROM -> TO pair of ARGN replaced (each must hit)
    file(READ ${src} _t)
    set(_a ${ARGN})
    list(LENGTH _a _n)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE 0 ${_last} 2)
        math(EXPR _j "${_i} + 1")
        list(GET _a ${_i} _from)
        list(GET _a ${_j} _to)
        string(FIND "${_t}" "${_from}" _at)
        if(_at LESS 0)
            message(FATAL_ERROR "core_drumlab.cmake: '${_from}' not in ${src} (a new Drumlab: look at the overlay)")
        endif()
        string(REPLACE "${_from}" "${_to}" _t "${_t}")
    endforeach()
    file(WRITE ${dst}.tmp "${_t}")
    configure_file(${dst}.tmp ${dst} COPYONLY)
endfunction()
set(_dl_labio ${CMAKE_BINARY_DIR}/dinghy/drumlab_labio.c)
_dl_overlay(${_dl}/firmware/src/labio.c ${_dl_labio}
            "#ifndef CHIPBOX_HOST" "#if 1 /* (core_drumlab.cmake: the device half on) */")
set(_dl_main ${CMAKE_BINARY_DIR}/dinghy/drumlab_lab.c)
_dl_overlay(${_dl}/firmware/src/lab.c ${_dl_main}
            "#ifndef CHIPBOX_HOST\n        if (px_open_req)" "#if 1 /* (core_drumlab.cmake: a project that came in is opened) */\n        if (px_open_req)")
set(_dl_vfs ${CMAKE_BINARY_DIR}/dinghy/drumlab_vfs.yaml)
file(WRITE ${_dl_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_dl}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/drumlab_host.c\" }, { \"type\": \"file\", \"name\": \"${_dl}/firmware/src/labio.c\", \"external-contents\": \"${_dl_labio}\" }, { \"type\": \"file\", \"name\": \"${_dl}/firmware/src/lab.c\", \"external-contents\": \"${_dl_main}\" } ] }\n")
fm1_add_core(drumlab ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_dl}
             UNIT core_drumlab.c DESC core_drumlab_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "Drumlab by pbarilla (the app: the drum voices, the LAB, the kits, the sequencer, the PO-32 transfer, its views, the website, begun from Chipbox), on Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. The screen's font is Terminus Font (8x16, Dimitar Toshkov Zhekov), SIL Open Font License 1.1. PO-32 and Pocket Operator belong to Teenage Engineering, Microtonic to Sonic Charge: Drumlab is not made or endorsed by them. The full notices: the release's licences/cores/drumlab/ (LICENSING.md)."
             CREDITS_FILES ${_dl}/assets/fonts/Terminus-LICENSE.txt
             NO_GEN VERSION 0.1.3-beta DEFINES APP_VER="0.1.3"
             INCLUDES ${_dl}/firmware/src ${_dl}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_dl_vfs}")
if(TARGET core_drumlab_obj)
    # (the overlaid files are not in the compiler's dependency list under their real names)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_drumlab.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/drumlab_host.c ${_dl_labio} ${_dl_main} ${_dl_vfs})
endif()
dinghy_core(drumlab core_drumlab.c)
# the golden check: the same script on its own host sim (tests/host.c and lab.c unchanged; LAB_BOOT is the power-on
# picture, as the core shows it), the screens compared at the script's shot
dinghy_golden_check(drumlab SCRIPT golden_drumlab.txt AT 5000 SOURCE ${_dl}/firmware/src/lab.c
                    DEFINES CHIPBOX_HOST LAB_BOOT APP_VER="0.1.3"
                    INCLUDES ${_dl}/firmware/src ${_dl}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md and the Terminus font's OFL (the only
# font in the firmware: the site's fonts are not in it); Emscripten's and musl's are added by fm1_add_rfm1. RUN_ARGS:
# a fresh flash opens on the quick start after the power-on picture (1.7 s): OCT- puts it away, then PLAY starts the
# demo in project 1 (Korobeiniki)
fm1_add_rfm1(drumlab AUTHORS "Pasquale Barilla"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             RUN_ARGS --press OCT-@2500 --press PLAY@3000)
