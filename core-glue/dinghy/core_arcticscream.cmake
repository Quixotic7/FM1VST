# SPDX-License-Identifier: GPL-3.0-only
# Arctic Scream p0larpatch 1.5.2 (https://github.com/PolarPatch/Arctic-scream-FM-1, tag v1.5.2): PolarPatch's fork of
# GR!MM, the Dinghy family glue (core-glue/dinghy/) over the unmodified submodule cores/Arctic-scream-FM-1, on its
# browser shim web/fm1_web.c (GRIMM_WEB). GR!MM's hook (grimm_hook/, -iquote: searched before the -I paths) gives MIDI
# out and the keys' glow back to the glue (core_arcticscream.c). The headers in firmware/gen are committed (NO_GEN);
# the version is tools/build.py's APP_VER (--release 1.5.2). The .pool section attribute Mach-O rejects becomes an
# alignment (FLAGS). The PolarPatch logo (assets/, firmware/gen/polarpatch_logo.h, all rights reserved) is in the
# tree but not compiled in: no source includes it (LICENSING.md says it marks the firmware).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(arcticscream ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Arctic-scream-FM-1
             UNIT core_arcticscream.c DESC core_arcticscream_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "Arctic Scream p0larpatch by PolarPatch, a modified version of GR!MM by GRiMMBIENT, on Dinghy by zvenson (Felucca by Leo Kuroshita, Hügelton Instruments, with SLOOP's changes by isod89), GPL-3.0-only. msfa (Google Inc., Pascal Gauthier), Apache-2.0. DaisySP and Rings (Electrosmith, Emilie Gillet), MIT. The screen's fonts: Terminus Font (Dimitar Toshkov Zhekov) and Press Start 2P (the Press Start 2P Project Authors), SIL OFL 1.1. The full notices: the release's licences/cores/arcticscream/ (LICENSING.md, LICENSES/)."
             NO_GEN VERSION 1.5.2 DEFINES APP_VER="1.5.2"
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/Arctic-scream-FM-1/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-iquote${CMAKE_CURRENT_LIST_DIR}/grimm_hook")
dinghy_core(arcticscream core_arcticscream.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 6000 ms (one of its
# fixed SHOT times: the boot screen is 5.6 s, ui.c UI_BOOT_MS)
dinghy_golden_check(arcticscream SCRIPT golden_arcticscream.txt AT 6000
                    SOURCE ${CMAKE_SOURCE_DIR}/cores/Arctic-scream-FM-1/firmware/src/grimm.c
                    DEFINES DINGHY_HOST APP_VER="1.5.2"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/Arctic-scream-FM-1/firmware/src
                             ${CMAKE_SOURCE_DIR}/cores/Arctic-scream-FM-1/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: the firmware's own (its LICENSE,
# LICENSES/, LICENSING.md, the fonts')
fm1_add_rfm1(arcticscream AUTHORS "PolarPatch"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "MIT (Emscripten in {name})=LICENSES/MIT-Emscripten.txt"
                           "MIT (Rings in {name})=LICENSES/MIT-Rings.txt"
                           "MIT (musl in {name})=LICENSES/MIT-musl.txt"
                           "OFL-1.1 (PressStart2P in {name})=LICENSES/OFL-PressStart2P.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
