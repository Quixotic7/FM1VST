# SPDX-License-Identifier: GPL-3.0-only
# K-WASUB 1.0.46 (https://github.com/Guizmoh/K-WASUB_Firmware_for_FM1, d7fa87d): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/K-WASUB_Firmware_for_FM1, in its host mode (DINGHY_HOST).
# no tag: pinned at the commit of 1.0.46, the Firmware Hub's latest (the owner's call, 2026-11-02). (The repository
# has no tags or releases; the Hub's source link names tree/v1.0.46. 03ec546 is the 1.0.46 commit, d7fa87d its tip:
# the commits after it touch the site and fmhub.json only, source/ is the same.)
# platform.c includes "../../tests/host.c" by a relative path under DINGHY_HOST, a file K-WASUB's tree no longer has,
# so clang's -ivfsoverlay maps that path to kwasub_host.c, as core_aurora.cmake does. The headers in firmware/gen are
# committed (NO_GEN). The version and the package identity are tools/build.py --release 1.0.46's: APP_VER "1.0.46",
# FELUCCA_ID "FM-1_1990" (every 1.Y). The .pool section attribute is the device build's only (ksub.c).
# Public: all of it is GPL-3.0-only, the fonts SIL OFL 1.1 (Terminus, IBM Plex Mono, Atkinson Hyperlegible Mono, Press
# Start 2P, Outfit, Instrument Sans, Tektur, JetBrains Mono, Big Shoulders) and the DejaVu fonts' licence (DejaVu Sans
# Bold, rendered into ksub_fonts.h by tools/gen_ksub.py: the default screen font). The SAMPLE layer is synthesised
# (gen_ksub.py: "made here, nothing recorded").
#
# THE TERMINUS AND DEJAVU TEXTS: K-WASUB's LICENSING.md names source/assets/fonts/Terminus-LICENSE.txt, which its tree
# does not carry, and does not name DejaVu; licences/aurora/Terminus-LICENSE.txt (ter-u16n, as Felucca-lineage
# firmwares ship it) and licences/sid/DejaVu-LICENSE.txt are the same fonts' notices.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_kw ${CMAKE_SOURCE_DIR}/cores/K-WASUB_Firmware_for_FM1)
set(_kw_src ${_kw}/source)
set(_kw_vfs ${CMAKE_BINARY_DIR}/dinghy/kwasub_vfs.yaml)
file(WRITE ${_kw_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_kw_src}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/kwasub_host.c\" } ] }\n")
fm1_add_core(kwasub ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_kw}
             UNIT core_kwasub.c DESC core_kwasub_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "K-WASUB by Guizmoh (the bass synthesizer: its engines, the SAMPLE layer, the pages, the DATA page), born from KAWA by Omnipotard, on Dinghy by zvenson (the wiring platform.c, the host mode), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. The CS engine after twochannels' CS-1 (GPL-3.0-only, written again). GPL-3.0-only. The screen fonts: Terminus Font (Dimitar Toshkov Zhekov), IBM Plex Mono (IBM Corp.), Atkinson Hyperlegible Mono, Press Start 2P, Outfit, Instrument Sans, Tektur, JetBrains Mono and Big Shoulders (their project authors), SIL Open Font License 1.1, and DejaVu Sans (Bitstream Vera licence). The full notices: the release's licences/cores/kwasub/ (LICENSING.md)."
             CREDITS_FILES ${CMAKE_CURRENT_LIST_DIR}/licences/aurora/Terminus-LICENSE.txt
             NO_GEN VERSION 1.0.46 DEFINES APP_VER="1.0.46" FELUCCA_ID="FM-1_1990"
             INCLUDES ${_kw_src}/firmware/src ${_kw_src}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-ivfsoverlay" "${_kw_vfs}")
if(TARGET core_kwasub_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_kwasub.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/kwasub_host.c ${_kw_vfs})
endif()
dinghy_core(kwasub core_kwasub.c)
# (no golden check: K-WASUB has no host sim of its own to compare with, its tests/host.c is gone)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md, the fonts' (OFL, DejaVu); Emscripten's
# and musl's are added by fm1_add_rfm1. RUN_ARGS: an empty flash is the very first start, SEL three times (the start
# screen, CLASSIC or SHIFT, the look) before the chord
set(_kw_f source/assets/fonts)
fm1_add_rfm1(kwasub AUTHORS "Guizmoh"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=${CMAKE_CURRENT_LIST_DIR}/licences/aurora/Terminus-LICENSE.txt"
                           "OFL-1.1 (IBM Plex Mono in {name})=${_kw_f}/IBMPlexMono-LICENSE.txt"
                           "OFL-1.1 (Atkinson Hyperlegible Mono in {name})=${_kw_f}/AtkinsonHyperlegibleMono-LICENSE.txt"
                           "OFL-1.1 (Press Start 2P in {name})=${_kw_f}/PressStart2P-LICENSE.txt"
                           "OFL-1.1 (Outfit in {name})=${_kw_f}/Outfit-LICENSE.txt"
                           "OFL-1.1 (Instrument Sans in {name})=${_kw_f}/InstrumentSans-LICENSE.txt"
                           "OFL-1.1 (Tektur in {name})=${_kw_f}/Tektur-LICENSE.txt"
                           "OFL-1.1 (JetBrains Mono in {name})=${_kw_f}/JetBrainsMono-LICENSE.txt"
                           "OFL-1.1 (Big Shoulders in {name})=${_kw_f}/BigShoulders-LICENSE.txt"
                           "Bitstream-Vera (the DejaVu Sans font in {name})=${CMAKE_CURRENT_LIST_DIR}/licences/sid/DejaVu-LICENSE.txt"
             RUN_ARGS --press SEL@1000 --press SEL@1600 --press SEL@2200)
