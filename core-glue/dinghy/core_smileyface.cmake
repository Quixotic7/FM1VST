# SPDX-License-Identifier: GPL-3.0-only
# Smileyface 1.1.0 (https://github.com/pbarilla/smileyface, tag v1.1.0, 622f74d): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/smileyface, in its host mode (SMILEY_HOST). platform.c
# includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that
# path to smileyface_host.c, as core_aurora.cmake does. The same overlay maps firmware/src/projio.c to a copy written
# here, in the build tree, with its device half (the Share page's SysEx, proj_io, behind #ifndef SMILEY_HOST) on: the
# rest of the file is unchanged. The headers in firmware/gen are committed (NO_GEN); the version is the tag's
# (tools/build.py --release). Public: all of it is GPL-3.0-only, the font SIL OFL 1.1.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_sf ${CMAKE_SOURCE_DIR}/cores/smileyface)
set(_sf_projio ${CMAKE_BINARY_DIR}/dinghy/smileyface_projio.c)
file(READ ${_sf}/firmware/src/projio.c _p)
string(REPLACE "#ifndef SMILEY_HOST" "#if 1 /* (core_smileyface.cmake: the device half on) */" _p "${_p}")
file(WRITE ${_sf_projio}.tmp "${_p}")
configure_file(${_sf_projio}.tmp ${_sf_projio} COPYONLY)
set(_sf_vfs ${CMAKE_BINARY_DIR}/dinghy/smileyface_vfs.yaml)
file(WRITE ${_sf_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_sf}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/smileyface_host.c\" }, { \"type\": \"file\", \"name\": \"${_sf}/firmware/src/projio.c\", \"external-contents\": \"${_sf_projio}\" } ] }\n")
fm1_add_core(smileyface ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sf}
             UNIT core_smileyface.c DESC core_smileyface_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "Smileyface by pbarilla (the app, the acid voice, the sequencer and its views, the website), built on Chipbox (pbarilla), on Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. The screen's font is Terminus Font (8x16, Dimitar Toshkov Zhekov), SIL Open Font License 1.1. The full notices: the release's licences/cores/smileyface/ (LICENSING.md)."
             CREDITS_FILES ${_sf}/assets/fonts/Terminus-LICENSE.txt
             NO_GEN VERSION 1.1.0 DEFINES APP_VER="1.1.0"
             INCLUDES ${_sf}/firmware/src ${_sf}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_sf_vfs}")
if(TARGET core_smileyface_obj)
    # (the overlaid files are not in the compiler's dependency list under their real names)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_smileyface.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/smileyface_host.c ${_sf_projio} ${_sf_vfs})
endif()
dinghy_core(smileyface core_smileyface.c)
# the golden check: the same script on its own host sim (tests/host.c, unchanged, with the power-on smiley as the
# core shows it), the screens compared at the script's shot
dinghy_golden_check(smileyface SCRIPT golden_smileyface.txt AT 4000 SOURCE ${_sf}/firmware/src/smileyface.c
                    DEFINES SMILEY_HOST SPLASH_DEMO APP_VER="1.1.0"
                    INCLUDES ${_sf}/firmware/src ${_sf}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md and the Terminus font's OFL; Emscripten's
# and musl's are added by fm1_add_rfm1. RUN_ARGS: PLAY starts the factory pattern (project 1) after the smiley
fm1_add_rfm1(smileyface AUTHORS "pbarilla"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             RUN_ARGS --press PLAY@2000)
