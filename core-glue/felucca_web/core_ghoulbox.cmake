# SPDX-License-Identifier: GPL-3.0-only
# GHOULBOX 2.4 (https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(ghoulbox ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/ghoulbox-fm1-dungeon-synth
             UNIT core_ghoulbox.c DESC core_ghoulbox_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
                 ghoulbox_art.h
             VERSION 2.4 DEFINES FELUCCA_VERSION="v1.4.1")
fw_golden(ghoulbox ${CMAKE_SOURCE_DIR}/cores/ghoulbox-fm1-dungeon-synth web/emu/felucca_web.c FELUCCA_VERSION="v1.4.1")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
# rd1: Felucca's map (fw_map_fork.h), checked against this fork's screens (HOME, ENV, LFO, FX, SCL, ARP, EDIT, GLO
# held); the test: ENV's ATTACK, ALGORITHM to part 2, FX's chorus send
fm1_add_rfm1(ghoulbox WANTS rd1
             READOUT_TEST --press ENV@4100 --turn K1@4400:2 --turn K4@4600:-1 --turn ALGO@4800:1 --press FX@5000
                          --turn K2@5300:3 --seconds 3 --expect-readout ctl_3_Attack_=_2.1_ms
                          --expect-readout ctl_1_Part_=_Part_2 --expect-readout ctl_4_Chorus_Send_=_50_%
             AUTHORS "jasonpersinger")
