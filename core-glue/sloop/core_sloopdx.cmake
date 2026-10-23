# SPDX-License-Identifier: GPL-3.0-only
# sloopDX 4.0 (https://github.com/zvenson/dxsloop, tag v4.0 = 9870b84: the firmware tree of eeec0a6, pinned before
# the tag existed; the three commits after it touch only CI and web/fm1ota.js). The SLOOP-family glue (core-glue/sloop).
# GATED: it compiles assets/icons.png (gen_icons.py), the Hugelton icon atlas, all rights reserved (LICENSING.md: the
# Felucca section 7 permission, whose asset terms are not published yet). Un-gate: those terms, or replacement icons.
fm1_add_core(sloopdx ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/dxsloop UNIT core_sloopdx.c
             DESC core_sloopdx_desc.c FAMILY sloop LICENSE GPL-3.0-only
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN felucca_font.h felucca_icons.h felucca_tables.h sloopdx_logo.h
             VERSION 4.0)

# the golden check: sloopDX's own tests/ui_pages_test.c writes its pages; the core runs golden_sloopdx.txt to the same
# state: the boot animation's algorithm 22 frame (ms 1520), the TRACKS page and the EDIT page are pixel for pixel the
# same. (Its page-splash is not compared: the test draws it after loading the voices, the device before: the
# operator level marks differ.)
if(TARGET sloopdx_core)
    fm1_sloop_ref(sloopdx_ref_ui_pages ${CMAKE_SOURCE_DIR}/cores/dxsloop/tests/ui_pages_test.c
                  ${CMAKE_SOURCE_DIR}/cores/dxsloop gen_sloopdx)
    fm1_sloop_golden(sloopdx REF sloopdx_ref_ui_pages REF_ARGS %REF%
                     SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_sloopdx.txt
                     DRIVE_ARGS --compare 1520 %REF%/page-splash-alg22.ppm --compare 4400 %REF%/page-tracks.ppm
                                --compare 5200 %REF%/page-edit.ppm)
endif()
