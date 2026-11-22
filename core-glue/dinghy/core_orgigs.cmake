# SPDX-License-Identifier: GPL-3.0-only
# OrGigs 1.1 (https://github.com/orgelbobo/orgigs, tag v1.1 = e95e890): the Dinghy family glue (core-glue/dinghy/)
# over the unmodified submodule cores/orgigs, in its host mode (DINGHY_HOST, the FM-1 board). platform.c includes
# "../../tests/host.c" by a relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that path to
# orgigs_host.c, as core_sid1.cmake does; orgigs_host.c takes the firmware's own fake FM-1, sim/sim_platform.h (its
# browser simulator's and tests' shared one). The headers in firmware/gen are committed (NO_GEN); the version is the
# tag's (tools/build.py --release 1.1: APP_VER "1.1", identity FM-1_674102). The .pool section attribute
# (gfx.c) Mach-O rejects becomes an alignment (FLAGS).
# Public: GPL-3.0-only; the Terminus font SIL OFL 1.1; the Yamaha SEQTRAK sound list (names, firmware/gen/
# yst_presets.h) from jaeggerr/seqtrak-docs, MIT (LICENSING.md). The designburg logo in assets/ is not compiled in.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_orgigs ${CMAKE_SOURCE_DIR}/cores/orgigs)
set(_orgigs_ver 1.1)
set(_orgigs_vfs ${CMAKE_BINARY_DIR}/dinghy/orgigs_vfs.yaml)
file(WRITE ${_orgigs_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_orgigs}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/orgigs_host.c\" } ] }\n")
fm1_add_core(orgigs ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_orgigs}
             UNIT core_orgigs.c DESC core_orgigs_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "OrGigs by Bobo (orgelbobo), with Claude: the organ, its effects, MIDI zones and panel. On Dinghy by zvenson (the wiring platform.c, the host mode, the storage from OberVave), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89, the SMK-37 layer by M. Geisler (genkigenki). GPL-3.0-only. The screen's font is Terminus (SIL Open Font License 1.1), the Yamaha SEQTRAK sound list is from jaeggerr/seqtrak-docs (MIT). M-VAVE, FM-1, SMK-37 and SEQTRAK are trademarks of their owners. The full notices: the release's licences/cores/orgigs/ (LICENSING.md)."
             NO_GEN VERSION ${_orgigs_ver} DEFINES DINGHY_HOST APP_VER="${_orgigs_ver}"
             INCLUDES ${_orgigs}/firmware/src ${_orgigs}/firmware/gen ${_orgigs}/sim ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_orgigs_vfs}")
if(TARGET core_orgigs_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_orgigs.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/orgigs_host.c ${_orgigs_vfs})
endif()
dinghy_core(orgigs core_orgigs.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md, the Terminus font's OFL and the
# SEQTRAK docs' MIT; Emscripten's and musl's are added by fm1_add_rfm1
fm1_add_rfm1(orgigs AUTHORS "Bobo (orgelbobo)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "MIT (the SEQTRAK sound list in {name}, jaeggerr/seqtrak-docs)=assets/seqtrak/LICENSE.seqtrak-docs")
