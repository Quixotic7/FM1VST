# SPDX-License-Identifier: GPL-3.0-only
# GR!MM 1.0 (https://github.com/GRiMMBIENT/grimm, tag v1.0): the Dinghy family glue (core-glue/dinghy/) over the
# unmodified submodule cores/grimm, on its browser shim web/fm1_web.c (GRIMM_WEB). grimm_hook/ (-iquote: searched
# before the -I paths) gives MIDI out and the keys' glow back to the glue (core_grimm.c). The headers in firmware/gen
# are committed (NO_GEN); the version is tools/build.py's APP_VER (--release 1.0). The .pool section attribute Mach-O
# rejects becomes an alignment (FLAGS). Public (the owner's call, 2026-10-22).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(grimm ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/grimm
             UNIT core_grimm.c DESC core_grimm_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "GR!MM by GRiMMBIENT, on Dinghy by zvenson (Felucca by Leo Kuroshita, Hügelton Instruments, with SLOOP's changes by isod89), GPL-3.0-only. msfa (Google Inc., Pascal Gauthier), Apache-2.0. DaisySP and Rings (Electrosmith, Emilie Gillet), MIT. The factory samples are its own recordings, CC0 1.0. The screen's fonts: Terminus Font (Dimitar Toshkov Zhekov) and Press Start 2P (the Press Start 2P Project Authors), SIL OFL 1.1. The full notices: the release's licences/cores/grimm/ (LICENSING.md, LICENSES/)."
             NO_GEN VERSION 1.0
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-iquote${CMAKE_CURRENT_LIST_DIR}/grimm_hook")
dinghy_core(grimm core_grimm.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3500 ms (one of its
# fixed SHOT times)
dinghy_golden_check(grimm SCRIPT golden_grimm.txt AT 3500 SOURCE ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/src/grimm.c
                    DEFINES DINGHY_HOST APP_VER="1.0"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/src ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
