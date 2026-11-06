# SPDX-License-Identifier: GPL-3.0-only
# CULTBOX 1.2 by beatcult (github.com/beatcult/CULTBOX, tag v1.2 = c35e403): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/CULTBOX, in its host mode (CULTBOX_HOST, the fork's name for
# DINGHY_HOST). platform.c includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's
# -ivfsoverlay maps that path to cultbox_host.c, as core_fmel1.cmake does. The headers are committed (firmware/gen
# the font, firmware/src/drums_data.h the samples, logo.h the logo: NO_GEN); firmware/hal is on the path for store.h's
# fm1_flash_map.h. The version is the tag's (tools/build.py --release 1.2: APP_VER "1.2"; the identity is FM-1_333 on
# every release). The .pool section attribute is the device's linker's (-Dsection).
#
# Licences (LICENSING.md): the code GPL-3.0-only; the Terminus font SIL OFL 1.1. Compiled in without a licence of
# their own (LICENSING.md names them, the owner decides on gating): the ten Elka Drummer One samples
# (assets/drums/elka -> firmware/src/drums_data.h, "recorded from an Elka Drummer One", no recordist or terms given)
# and the BEAT CULT logo (assets/sleepr.svg -> firmware/src/logo.h, "artwork, not GPL code", supplied by beatcult).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_cb ${CMAKE_SOURCE_DIR}/cores/CULTBOX)
set(_cb_vfs ${CMAKE_BINARY_DIR}/dinghy/cultbox_vfs.yaml)
file(WRITE ${_cb_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_cb}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/cultbox_host.c\" } ] }\n")
fm1_add_core(cultbox ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cb}
             UNIT core_cultbox.c DESC core_cultbox_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             GATED "embeds ten Elka Drummer One samples with no stated recordist or terms (LICENSING.md) and the BEAT CULT logo, called artwork not GPL code in LICENSING.md"
             CREDITS "CULTBOX by beatcult (the PLVKS synth after the Formanta Polivoks, the drums and their sequencer, the DUBMARINE dub siren, the effects, the mixer, the screens), a fork of Dinghy by zvenson (the wiring platform.c, the host mode, the storage), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89 and the SMK-37 layer by M. Geisler (genkigenki). GPL-3.0-only. The drum samples: recorded from an Elka Drummer One. The BEAT CULT logo: artwork by beatcult, not GPL code. The screen's font: Terminus (Dimitar Toshkov Zhekov), SIL Open Font License 1.1. The full notices: the release's licences/cores/cultbox/ (LICENSING.md)."
             CREDITS_FILES ${_cb}/assets/fonts/Terminus-LICENSE.txt
             NO_GEN VERSION 1.2 DEFINES APP_VER="1.2"
             INCLUDES ${_cb}/firmware/hal ${_cb}/firmware/src ${_cb}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_cb_vfs}")
if(TARGET core_cultbox_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_cultbox.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/cultbox_host.c ${_cb_vfs})
endif()
dinghy_core(cultbox core_cultbox.c)
# the golden check: the same script on its own host sim (tests/host.c, unchanged), the screens compared at 3000 ms
# (one of its fixed SHOT times)
dinghy_golden_check(cultbox SCRIPT golden_cultbox.txt AT 3000 SOURCE ${_cb}/firmware/src/cultbox.c
                    DEFINES CULTBOX_HOST APP_VER="1.2"
                    INCLUDES ${_cb}/firmware/hal ${_cb}/firmware/src ${_cb}/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md (the samples' and the logo's notes) and
# the Terminus font's OFL; Emscripten's and musl's are added by fm1_add_rfm1. rfm1_run plays its chord on PLVKS (the
# INIT preset): no RUN_ARGS
fm1_add_rfm1(cultbox AUTHORS "beatcult"
             HOSTED_DESPITE_GATE "the author ships the samples and the logo in every package: built as shipped, hosted at RemoteFM1FW only, never bundled (owner, 2026-11-06)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
