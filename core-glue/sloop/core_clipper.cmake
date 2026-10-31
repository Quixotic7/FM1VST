# SPDX-License-Identifier: GPL-3.0-only
# Clipper 1.3 (https://github.com/isod89/clipper-fm1, tag v1.3 = fbd8434), by isod89: a synthesizer grown out of SLOOP
# 2.8, on Felucca. Its felucca.c includes what SLOOP's does: the SLOOP-family glue (core-glue/sloop) as SLOOP.
# GATED as SLOOP: the firmware compiles assets/icons.png (tools/gen_icons.py -> felucca_icons.h), the Hugelton icon
# atlas, all rights reserved (LICENSING.md's GPL section 7 permission needs asset terms not yet published).
fm1_add_core(clipper ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/clipper-fm1 UNIT core_clipper.c
             DESC core_clipper_desc.c FAMILY sloop LICENSE GPL-3.0-only
             CREDITS "Clipper by isod89 (github.com/isod89/clipper-fm1), grown out of SLOOP, based on Felucca by Leo Kuroshita (Hugelton Instruments)."
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h
                 felucca_wavetables.h sloop_logo.h
             PYTHON_NEEDS numpy
             VERSION_FILE firmware/src/ui.c VERSION_REGEX "#define FELUCCA_VERSION \"CLIPPER ([0-9.]+)\"")
# the .rfm1 (cmake/FM1Rfm1.cmake; docs/RFM1.md; gated, built as a hosted file: HOSTED_DESPITE_GATE): its GPL, msfa's Apache-2.0, its LICENSING.md
# and the Terminus font's OFL (the screen font, compiled in by gen_font.py). The data needs 16 MiB.
fm1_add_rfm1(clipper AUTHORS "isod89" MEMORY_MIB 16
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             HOSTED_DESPITE_GATE "the owner's call, 2026-10-31: built as its author intended, hosted on RemoteFM1FW, not in the app")

# the golden check: Clipper's own tests/ui_pages_test.c (the real UI on its test doubles) writes its pages; the core
# runs golden_clipper.txt to the same state (SLOOP's script and comparison: the status corner differs, the HAL's USB
# host and battery). Two more explained differences: at v1.3 the test's own checks fail (104 FAILs, the visualiser and
# other SLOOP-era checks: REF_FAILS_OK, its pages are still written), and the TRACKS page's sound counter (x 77..82,
# y 112..121) reads /409 there, /407 on the device build (the test's doubles count two more sounds)
if(TARGET clipper_core AND NOT COMMAND fm1_sloop_golden)   # (FM1_ONLY_CORES=clipper: the family's core.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/core.cmake)
endif()
if(TARGET clipper_core)
    fm1_sloop_ref(clipper_ref_ui_pages ${CMAKE_SOURCE_DIR}/cores/clipper-fm1/tests/ui_pages_test.c
                  ${CMAKE_SOURCE_DIR}/cores/clipper-fm1 gen_clipper)
    fm1_sloop_golden(clipper REF clipper_ref_ui_pages REF_FAILS_OK REF_ARGS %REF% SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_clipper.txt
                     DRIVE_ARGS --compare 5 %REF%/page-splash.ppm --compare 2400 %REF%/page-tracks.ppm
                                --compare 3200 %REF%/page-edit.ppm --ignore 189 3 232 13 --ignore 77 112 82 121)
endif()
