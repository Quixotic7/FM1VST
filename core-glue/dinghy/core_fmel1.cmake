# SPDX-License-Identifier: GPL-3.0-only
# FMEL-1 1.2 (https://github.com/AntonioAltea/fmel-1, tag v1.2, 2d69b38): the Dinghy family glue (core-glue/dinghy/)
# over the unmodified submodule cores/fmel-1, in its host mode (DINGHY_HOST). platform.c includes "../../tests/host.c"
# by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that path to fmel1_host.c, as
# core_chipbox.cmake does. Its own browser emulator (web/emu/fm1_web.c, DINGHY_WEB) is not used: the host mode is the
# same app on the same kind of shim. The headers in firmware/gen are committed (NO_GEN); the version is the tag's
# (tools/build.py --release 1.2: APP_VER "1.2", identity FM-1_950). The .pool section attribute is the device's only.
#
# GATED, HOSTED (the owner's call, 2026-11-02): two things compiled into every build are not GPL and not freely
# redistributable (LICENSING.md, README.md "The tapes"):
#   firmware/gen/mel_tapes.h    the Mellotron M400S tapes by Bernie Kornowicz (Taijiguy, leisureland.us), "free to
#                               use, but cannot be used in any commercial, for profit software" (KVR, 2021)
#   firmware/gen/mel_sprites.h  the drawings by Javi (assets/sprites/), used with his permission, not under the GPL
# So it is left out of a public build (-DFM1_PUBLIC_BUILD=ON) and built as an .rfm1 for RemoteFM1FW only
# (HOSTED_DESPITE_GATE), with LICENSING.md (which quotes the tapes' terms) among its licence texts. The PIANO is VCSL's
# Steinway B (CC0) as SLOOP cut it; the fonts are Terminus (SIL OFL 1.1).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_mel ${CMAKE_SOURCE_DIR}/cores/fmel-1)
set(_mel_vfs ${CMAKE_BINARY_DIR}/dinghy/fmel1_vfs.yaml)
file(WRITE ${_mel_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_mel}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/fmel1_host.c\" } ] }\n")
fm1_add_core(fmel1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_mel}
             UNIT core_fmel1.c DESC core_fmel1_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             GATED "the Mellotron M400S tapes by Taijiguy (firmware/gen/mel_tapes.h: free to use, not in any commercial software) and the sprites drawn by Javi (firmware/gen/mel_sprites.h: by his permission, not GPL)"
             CREDITS "FMEL-1 (MEL) by AntonioAltea (the app, the tape voices, the effects, the sequencer, the screen, the browser emulator), on Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. The tapes: Mellotron M400S samples by Bernie Kornowicz (Taijiguy, leisureland.us), his, not GPL: free to use but not in any commercial, for-profit software. The drawings on the screen by Javi, used with his permission, not GPL. The PIANO: a Steinway B from Versilian Studios' VCSL (Sam Gossner, CC0 1.0), cut and played as SLOOP (isod89) does. The reverbs: HALL after Felucca's, PLATE Jon Dattorro's after FoMni's (Charles Vestal). The screen's fonts are Terminus (Dimitar Toshkov Zhekov), SIL Open Font License 1.1. The full notices: the release's licences/cores/fmel1/ (LICENSING.md)."
             CREDITS_FILES ${_mel}/assets/fonts/Terminus-LICENSE.txt
             NO_GEN VERSION 1.2 DEFINES APP_VER="1.2"
             INCLUDES ${_mel}/firmware/src ${_mel}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_mel_vfs}")
if(TARGET core_fmel1_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_fmel1.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/fmel1_host.c ${_mel_vfs})
endif()
dinghy_core(fmel1 core_fmel1.c)
# the golden check: the same script on its own host sim (tests/host.c, unchanged), the screens compared at 5000 ms
# (one of its fixed SHOT times)
dinghy_golden_check(fmel1 SCRIPT golden_fmel1.txt AT 5000 SOURCE ${_mel}/firmware/src/mel.c
                    DEFINES DINGHY_HOST APP_VER="1.2"
                    INCLUDES ${_mel}/firmware/src ${_mel}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md; gated, built as a hosted file: HOSTED_DESPITE_GATE):
# the main loop on the coroutine, so Asyncify (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its
# LICENSING.md (the tapes' terms, the drawings' permission) and the Terminus fonts' OFL; Emscripten's and musl's are
# added by fm1_add_rfm1
fm1_add_rfm1(fmel1 AUTHORS "AntonioAltea"
             HOSTED_DESPITE_GATE "the M400S tapes (non-commercial terms) and Javi's sprites (by permission): hosted at RemoteFM1FW only, the owner's call 2026-11-02"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus fonts in {name})=assets/fonts/Terminus-LICENSE.txt")
