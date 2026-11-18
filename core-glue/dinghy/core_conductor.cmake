# SPDX-License-Identifier: GPL-3.0-only
# CONDUCTOR 1.0 (https://github.com/MammothCheese/conductor, tag v1.0 = a74fb5a): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/conductor, in its host mode (DINGHY_HOST). platform.c
# includes "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that
# path to conductor_host.c, as core_bytemania.cmake does. The headers in firmware/gen are committed (NO_GEN); the
# version is the tag's (tools/build.py --release 1.0: APP_VER "1.0", identity FM-1_1812 for every build). The .pool
# section attribute is the device's linker's (-Dsection). store.h reads hal/fm1_flash_map.h (INCLUDES).
# Public: GPL-3.0-only; the fonts Terminus and Noto Music SIL OFL 1.1, DejaVu Serif the Bitstream Vera licence. The
# winds' and brass' tables (firmware/gen/winds_env.h) are spectral envelopes measured from recordings, not samples.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_cnd ${CMAKE_SOURCE_DIR}/cores/conductor)
set(_cnd_ver 1.0)
set(_cnd_vfs ${CMAKE_BINARY_DIR}/dinghy/conductor_vfs.yaml)
file(WRITE ${_cnd_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_cnd}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/conductor_host.c\" } ] }\n")
fm1_add_core(conductor ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cnd}
             UNIT core_conductor.c DESC core_conductor_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "CONDUCTOR by MammothCheese (the orchestra, its sequencer, Composer and score), built on Dinghy by zvenson (the wiring platform.c, the host mode, the storage from OberVave), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. The fonts: Terminus (8x16) and Noto Music (the score's symbols), SIL Open Font License 1.1, DejaVu Serif Bold (Bitstream Vera licence). The full notices: the release's licences/cores/conductor/."
             NO_GEN VERSION ${_cnd_ver} DEFINES APP_VER="${_cnd_ver}"
             INCLUDES ${_cnd}/firmware/src ${_cnd}/firmware/gen ${_cnd}/firmware/hal ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_cnd_vfs}")
if(TARGET core_conductor_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_conductor.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/conductor_host.c ${_cnd_vfs})
endif()
dinghy_core(conductor core_conductor.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md and the three fonts'; Emscripten's and
# musl's are added by fm1_add_rfm1. rfm1_run plays its chord: no RUN_ARGS
fm1_add_rfm1(conductor AUTHORS "MammothCheese"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (Noto Music, the score's symbols in {name})=assets/fonts/NotoMusic-LICENSE.txt"
                           "Bitstream Vera (DejaVu Serif in {name})=firmware/gen/DEJAVU-LICENSE.txt")
