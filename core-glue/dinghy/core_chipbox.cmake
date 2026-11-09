# SPDX-License-Identifier: GPL-3.0-only
# Chipbox 1.5.1 (https://github.com/pbarilla/chipbox, tag v1.5.1, 4851031): the Dinghy family glue (core-glue/dinghy/)
# over the unmodified submodule cores/chipbox, in its host mode (CHIPBOX_HOST), as core_smileyface.cmake does for its
# descendant. platform.c includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's
# -ivfsoverlay maps that path to chipbox_host.c. The same overlay maps two files to copies written here, in the build
# tree, with the device's code on where the host mode leaves it out (the rest of each file unchanged):
#   firmware/src/projio.c    the Projects page's SysEx (proj_io, behind #ifndef CHIPBOX_HOST)
#   firmware/src/chipbox.c   the power-on screen (the next of the 14 each boot) and the main loop's open of a project
#                            that came in over USB (px_open_req)
# The headers in firmware/gen are committed (NO_GEN); the version is the tag's (tools/build.py --release). Public: all
# of it is GPL-3.0-only (the SID's combined waveform tables are reSID's, GPL-2.0-or-later), the font SIL OFL 1.1.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_cb ${CMAKE_SOURCE_DIR}/cores/chipbox)
function(_cb_overlay src dst)                             # src with each FROM -> TO pair of ARGN replaced (each must hit)
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
            message(FATAL_ERROR "core_chipbox.cmake: '${_from}' not in ${src} (a new Chipbox: look at the overlay)")
        endif()
        string(REPLACE "${_from}" "${_to}" _t "${_t}")
    endforeach()
    file(WRITE ${dst}.tmp "${_t}")
    configure_file(${dst}.tmp ${dst} COPYONLY)
endfunction()
set(_cb_projio ${CMAKE_BINARY_DIR}/dinghy/chipbox_projio.c)
_cb_overlay(${_cb}/firmware/src/projio.c ${_cb_projio}
            "#ifndef CHIPBOX_HOST" "#if 1 /* (core_chipbox.cmake: the device half on) */")
set(_cb_main ${CMAKE_BINARY_DIR}/dinghy/chipbox_main.c)
_cb_overlay(${_cb}/firmware/src/chipbox.c ${_cb_main}
            "#if !defined(CHIPBOX_HOST) || defined(BOOT_DEMO)" "#if 1 /* (core_chipbox.cmake: the power-on screens, as on the device) */"
            "#ifndef CHIPBOX_HOST\n        if (px_open_req)" "#if 1 /* (core_chipbox.cmake: a project that came in is opened) */\n        if (px_open_req)")
set(_cb_vfs ${CMAKE_BINARY_DIR}/dinghy/chipbox_vfs.yaml)
file(WRITE ${_cb_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_cb}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/chipbox_host.c\" }, { \"type\": \"file\", \"name\": \"${_cb}/firmware/src/projio.c\", \"external-contents\": \"${_cb_projio}\" }, { \"type\": \"file\", \"name\": \"${_cb}/firmware/src/chipbox.c\", \"external-contents\": \"${_cb_main}\" } ] }\n")
fm1_add_core(chipbox ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cb}
             UNIT core_chipbox.c DESC core_chipbox_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "Chipbox by pbarilla (the app: the chips, the sequencer, the projects, its views, the website), on Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. The SID's combined waveform tables are reSID's (Dag Lem, GPL-2.0-or-later). The screen's font is Terminus Font (8x16, Dimitar Toshkov Zhekov), SIL Open Font License 1.1. The full notices: the release's licences/cores/chipbox/ (LICENSING.md)."
             CREDITS_FILES ${_cb}/assets/fonts/Terminus-LICENSE.txt
             NO_GEN VERSION 1.5.1 DEFINES APP_VER="1.5.1"
             INCLUDES ${_cb}/firmware/src ${_cb}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_cb_vfs}")
if(TARGET core_chipbox_obj)
    # (the overlaid files are not in the compiler's dependency list under their real names)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_chipbox.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/chipbox_host.c ${_cb_projio} ${_cb_main} ${_cb_vfs})
endif()
dinghy_core(chipbox core_chipbox.c)
# the golden check: the same script on its own host sim (tests/host.c and chipbox.c unchanged; BOOT_DEMO=1 is the
# power-on screen a fresh flash shows first, as the core does), the screens compared at the script's shot
dinghy_golden_check(chipbox SCRIPT golden_chipbox.txt AT 6000 SOURCE ${_cb}/firmware/src/chipbox.c
                    DEFINES CHIPBOX_HOST BOOT_DEMO=1 APP_VER="1.5.1"
                    INCLUDES ${_cb}/firmware/src ${_cb}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md and the Terminus font's OFL; Emscripten's
# and musl's are added by fm1_add_rfm1. RUN_ARGS: PLAY starts the demo (project 1, Korobeiniki) after the power-on
# screen (at most 3 s)
fm1_add_rfm1(chipbox AUTHORS "pbarilla"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             RUN_ARGS --press PLAY@4000)
