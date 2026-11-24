# SPDX-License-Identifier: GPL-3.0-only
# M-vance 2.7.1 by Kris (bk40-home) (https://github.com/bk40-home/M-vance, tag v2.7.1 = 073c117): a JP-8000/8080-style
# virtual analog, his JT-8000 (Jteensy8000) engine ported to the FM-1, on Dinghy. The Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/M-vance, in its host mode (DINGHY_HOST), as M-Vsynth2's core
# (the model: the same author's Dinghy fork, the same tests/host.c): platform.c includes "../../tests/host.c" by a
# relative path (a program: script, WAV, main), so clang's -ivfsoverlay maps that path to mvance_host.c. The engine is
# its own unit, firmware/src/mv_dsp.c (through mvance_dsp.c, EXTRA_SOURCES), as tools/build.py builds it apart at -O2.
# The headers in firmware/gen are committed (NO_GEN); firmware/hal is on the path for store.h's fm1_flash_map.h. The
# version is the tag's (tools/build.py --release 2.7.1: APP_VER "2.7.1", the identity FM-1_968087). The .pool section
# attribute is the device's linker's: an alignment here (core_mvance.c defines section(x); mvance.c has no function section()).
# Bluetooth MIDI (fm1-kit's fk_ble, the submodule third_party/fm1-kit) is the device's only: FK_BLE 0, as its host mode.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_mva ${CMAKE_SOURCE_DIR}/cores/M-vance)
set(_mva_ver 2.7.1)
set(_mva_vfs ${CMAKE_BINARY_DIR}/dinghy/mvance_vfs.yaml)
file(WRITE ${_mva_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_mva}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/mvance_host.c\" } ] }\n")
fm1_add_core(mvance ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_mva}
             UNIT core_mvance.c DESC core_mvance_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "M-vance by Kris (bk40-home): the JT-8000 (Jteensy8000) synth engine, his own, ported to the FM-1 in fixed point (Super Saw, the JP filter, multi-effects, delay, the plate reverb after the JT-8000's PlateReverb), the panel, the screen kit, the sequencer and the 64 factory programs (his own, GPL-3.0-only). The arpeggiator from Skiff by illuminus5, over Felucca's by Leo Kuroshita (Hügelton Instruments). On Dinghy by zvenson (the wiring platform.c, the host mode, the storage, from OberVave), over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89 and the SMK-37 layer by M. Geisler (genkigenki). GPL-3.0-only. The fonts: Terminus (SIL OFL 1.1) and DejaVu Sans Condensed Bold (Bitstream Vera licence, DejaVu changes public domain). JP-8000 and JP-8080 are Roland's trademarks: M-vance is not affiliated with Roland."
             NO_GEN VERSION ${_mva_ver} DEFINES DINGHY_HOST APP_VER="${_mva_ver}" FK_BLE=0
             INCLUDES ${_mva}/firmware/hal ${_mva}/firmware/src ${_mva}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             EXTRA_SOURCES mvance_dsp.c
             FLAGS "-ivfsoverlay" "${_mva_vfs}")
if(TARGET core_mvance_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_mvance.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/mvance_host.c ${_mva_vfs})
endif()
dinghy_core(mvance core_mvance.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md (who made what: Dinghy, Felucca, SLOOP,
# the SMK-37 layer, Skiff's arpeggiator), the Terminus font's OFL and DejaVu's licence (mv_kit_gen.h's font tables,
# MV_UI_KIT 1). Not the MPL-2.0 of uac_out.c (USB audio) nor fm1-kit's fk_ble (MPL-2.0 / Apache-2.0, Bluetooth): the
# device branch only; nor the JieLi SDK's Apache-2.0 (device packages only).
# rd1: its knob row as drawn, live labels and readouts (core_mvance.c, common/rd_cols.h)
fm1_add_rfm1(mvance WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_DEPTH_=_+1
             AUTHORS "Kris (bk40-home)" "zvenson"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name}: who made what)=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "Bitstream Vera / DejaVu (DejaVu Sans Condensed Bold in {name})=assets/fonts/dejavu/LICENSE")
