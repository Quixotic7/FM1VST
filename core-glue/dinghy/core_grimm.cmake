# SPDX-License-Identifier: GPL-3.0-only
# GR!MM 2.0.2 (https://github.com/GRiMMBIENT/grimm, tag v2.0.2): the Dinghy family glue (core-glue/dinghy/) over the
# unmodified submodule cores/grimm, on its browser shim web/fm1_web.c (GRIMM_WEB). grimm_hook/ (-iquote: searched
# before the -I paths) gives MIDI out and the keys' glow back to the glue (core_grimm.c). The headers in firmware/gen
# are committed (NO_GEN); the version is tools/build.py's APP_VER (--release 2.0.2). The .pool section attribute Mach-O
# rejects becomes an alignment (FLAGS). Public (the owner's call, 2026-10-22).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(grimm ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/grimm
             UNIT core_grimm.c DESC core_grimm_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "GR!MM by GRiMMBIENT, on Dinghy by zvenson (Felucca by Leo Kuroshita, Hügelton Instruments, with SLOOP's changes by isod89), GPL-3.0-only. msfa (Google Inc., Pascal Gauthier), Apache-2.0. DaisySP and Rings (Electrosmith, Emilie Gillet), MIT. The factory samples are its own recordings, CC0 1.0. VOICE's choir wavetables (GR!MM 2) are made from VocalSet: A Singing Voice Dataset (Julia Wilkins, Prem Seetharaman, Alison Wahl, Bryan Pardo, Northwestern University, 2018, https://doi.org/10.5281/zenodo.1442513), CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/), changed by GR!MM (measured into harmonics, drawn as single cycles). The screen's fonts: Terminus Font (Dimitar Toshkov Zhekov) and Press Start 2P (the Press Start 2P Project Authors), SIL OFL 1.1. The full notices: the release's licences/cores/grimm/ (LICENSING.md, LICENSES/)."
             NO_GEN VERSION 2.0.2
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-iquote${CMAKE_CURRENT_LIST_DIR}/grimm_hook")
dinghy_core(grimm core_grimm.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3500 ms (one of its
# fixed SHOT times)
dinghy_golden_check(grimm SCRIPT golden_grimm.txt AT 3500 SOURCE ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/src/grimm.c
                    DEFINES DINGHY_HOST APP_VER="2.0.2"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/src ${CMAKE_SOURCE_DIR}/cores/grimm/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md), enabled by RFM1Manager. The licence texts: the
# firmware's own (its LICENSE, LICENSES/, LICENSING.md, the fonts')
fm1_add_rfm1(grimm AUTHORS "GRiMMBIENT"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "CC-BY-4.0 ({name})=LICENSES/CC-BY-4.0.txt"
                           "CC0-1.0 ({name})=LICENSES/CC0-1.0.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Emscripten in {name})=LICENSES/MIT-Emscripten.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (musl in {name})=LICENSES/MIT-musl.txt"
                           "OFL-1.1 (PressStart2P in {name})=LICENSES/OFL-PressStart2P.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
