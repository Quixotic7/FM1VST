# SPDX-License-Identifier: GPL-3.0-only
# ORBIT 0.7.1 (https://github.com/dspaudio/orbit, tag v0.7.1): the SLOOP-family glue (core-glue/sloop).
# GATED: the CLUSTER engine (eng_op1_cluster.c, op1_cluster_core.c, felucca_op1_cluster.h from
# assets/op1-cluster/*.json) translates OP-1 firmware DSP and data, which LICENSING.md says remain Teenage
# Engineering material, not relicensed; engines.c includes it unconditionally (no switch). Also the Hugelton icon
# atlas (assets/icons.png). Un-gate: the author's or Teenage Engineering's clearance, or an upstream switch to build
# without CLUSTER, and the atlas's terms or replacement icons.
fm1_add_core(orbit ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/orbit UNIT core_orbit.c
             DESC core_orbit_desc.c FAMILY sloop LICENSE GPL-3.0-only
             GATED "the CLUSTER engine translates OP-1 firmware DSP and data (Teenage Engineering material, not relicensed, no build switch to leave it out), and the Hugelton icon atlas"
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h
                 felucca_op1_cluster.h
             PYTHON_NEEDS numpy
             VERSION 0.7.1)

# the golden check: ORBIT's native preview (tools/orbit_host.c, the stdin bridge) taps EDIT ("button 4") and writes
# screen.ppm; the core runs golden_orbit.txt: the EDIT page is pixel for pixel the same except the battery icon
# (x 219..232, y 6..11: the preview's ADC reads none). (Its HOME is not compared: the preview loads a demo pattern.)
if(TARGET orbit_core)
    fm1_sloop_ref(orbit_ref_host ${CMAKE_SOURCE_DIR}/cores/orbit/tools/orbit_host.c ${CMAKE_SOURCE_DIR}/cores/orbit
                  gen_orbit)
    fm1_sloop_golden(orbit REF orbit_ref_host REF_ARGS %REF% REF_STDIN ${CMAKE_CURRENT_LIST_DIR}/golden_orbit_ref.txt
                     SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_orbit.txt
                     DRIVE_ARGS --compare 3200 %REF%/screen.ppm --ignore 219 6 232 11)
endif()
