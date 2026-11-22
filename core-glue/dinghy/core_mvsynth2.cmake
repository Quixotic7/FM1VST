# SPDX-License-Identifier: GPL-3.0-only
# M-Vsynth2 1.5.2 by Kris (bk40-home) (https://github.com/bk40-home/M-Vsynth2, tag v1.5.2 = b558b50): Synth2 (Francois
# Chabloz's open-source recreation of Synth1) on Dinghy. The Dinghy family glue (core-glue/dinghy/) over the unmodified
# submodule cores/M-Vsynth2, in its host mode (DINGHY_HOST), as CULTBOX's core (the model; the same Dinghy tests/host.c):
# platform.c includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay
# maps that path to mvsynth2_host.c. The engine is its own unit, firmware/src/vs_dsp.c (through mvsynth2_dsp.c,
# EXTRA_SOURCES), as tools/build.py builds it apart. The headers in firmware/gen are committed (NO_GEN); firmware/hal is on the path for
# store.h's fm1_flash_map.h. The version is the tag's (tools/build.py --release 1.5.2: APP_VER "1.5.2", the identity
# FM-1_9681005). The .pool section attribute is the device's linker's: not -Dsection (mvsynth2.c has a function
# section()), aligned(4) around gfx.c (mvsynth2_host.c) and vs_dsp.c (mvsynth2_dsp.c) only. The submodule's own submodule
# (third_party/oxo_synth2: Synth2's sources, for its compare tools on the computer) is not needed.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_mv ${CMAKE_SOURCE_DIR}/cores/M-Vsynth2)
set(_mv_ver 1.5.2)
set(_mv_vfs ${CMAKE_BINARY_DIR}/dinghy/mvsynth2_vfs.yaml)
file(WRITE ${_mv_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_mv}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/mvsynth2_host.c\" } ] }\n")
fm1_add_core(mvsynth2 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_mv}
             UNIT core_mvsynth2.c DESC core_mvsynth2_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "M-Vsynth2 by Kris (bk40-home): Synth2's engine, its oscillators, envelopes, filters, voice handling and patch codec, ported from Synth2 by Francois Chabloz (MIT), the screens, the banks and the transfer. The Synth2 Factory bank by Francois Chabloz, MIT, shipped with his permission. On Dinghy by zvenson (the wiring platform.c, the host mode, the storage, from OberVave), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89, the SMK-37 layer by M. Geisler (genkigenki) and the second core from Melodee by Kerem Kilic (Ellic Studio). GPL-3.0-only. The fonts: Terminus (SIL OFL 1.1) and DejaVu Sans Condensed Bold (Bitstream Vera licence, DejaVu changes public domain). Synth1 is Daichi Laboratory's: M-Vsynth2 is not affiliated with it."
             NO_GEN VERSION ${_mv_ver} DEFINES DINGHY_HOST APP_VER="${_mv_ver}"
             INCLUDES ${_mv}/firmware/hal ${_mv}/firmware/src ${_mv}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             EXTRA_SOURCES mvsynth2_dsp.c
             FLAGS "-ivfsoverlay" "${_mv_vfs}")
if(TARGET core_mvsynth2_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_mvsynth2.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/mvsynth2_host.c ${_mv_vfs})
endif()
dinghy_core(mvsynth2 core_mvsynth2.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md (with Synth2's MIT notice, which covers the
# ported engine and the factory bank), the Terminus font's OFL and DejaVu's licence (vs_ui_gen.h's font tables),
# (1.5) the MPL-2.0 of fm1-base's announce.c (the screen reader, vs_speak.c: built in; the text: licences/mvsynth2/MPL-2.0.txt, as fm1-kit is a nested submodule). Not
# the MPL-2.0 of uac_out.c (USB audio: the device branch only) nor the JieLi SDK's Apache-2.0 (device packages only).
fm1_add_rfm1(mvsynth2 AUTHORS "Kris (bk40-home)" "zvenson"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name}, with Synth2's MIT notice)=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "Bitstream Vera / DejaVu (DejaVu Sans Condensed Bold in {name})=assets/fonts/dejavu/LICENSE"
                           "MPL-2.0 (fm1-base's announce.c in {name})=${CMAKE_CURRENT_LIST_DIR}/licences/mvsynth2/MPL-2.0.txt")
