# SPDX-License-Identifier: GPL-3.0-only
# m-pyre (https://github.com/HistoryOfSky/m-pyre, tag v1.0.3; GPL-3.0-only): scaffolded by RFM1Manager from sloop's glue (core-glue/sloop); review it
# SLOOP 2.8.5 (https://github.com/HistoryOfSky/m-pyre, tag v2.8.5 = bac9c05): the SLOOP-family glue (core-glue/sloop: sloop_hal.h,
# sloop_firmware.h, mpyre_fw.c, mpyre_core.c) over the unmodified submodule cores/m-pyre (cmake/FM1Cores.cmake)
# GATED: the firmware compiles assets/icons.png (tools/gen_icons.py -> felucca_icons.h), the Hugelton icon atlas,
# all rights reserved; LICENSING.md's GPL section 7 permission to convey the combination asks to follow terms for
# the assets that are not published yet. Un-gate: those terms allow a plugin, or a build with replacement icons.
fm1_add_core(mpyre ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/m-pyre UNIT core_mpyre.c
             DESC core_mpyre_desc.c FAMILY sloop LICENSE GPL-3.0-only
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h
                 sloop_logo.h
             PYTHON_NEEDS numpy
             VERSION 1.0.3)
# the .rfm1 (cmake/FM1Rfm1.cmake; docs/RFM1.md): gated, built as a hosted file (the owner's call, 2026-10-31: CORES-PLAN.md
# 6). Its GPL, msfa's Apache-2.0, its LICENSING.md and the Terminus font's OFL (the screen font, gen_font.py).
fm1_add_rfm1(mpyre AUTHORS "HistoryOfSky"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "OFL-1.1 (the BarlowCondensed font in {name})=assets/fonts/BarlowCondensed-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the blackopsone font in {name})=assets/mpyre/fonts/OFL-blackopsone.txt"
                           "OFL-1.1 (the damion font in {name})=assets/mpyre/fonts/OFL-damion.txt"
                           "OFL-1.1 (the pacifico font in {name})=assets/mpyre/fonts/OFL-pacifico.txt"
                           "OFL-1.1 (the racingsansone font in {name})=assets/mpyre/fonts/OFL-racingsansone.txt"
                           "OFL-1.1 (the russoone font in {name})=assets/mpyre/fonts/OFL-russoone.txt"
                           "MIT (the FUKIAI in {name})=docs/webapp/editor/FUKIAI-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/FUKIAI-LICENSE.txt")

# the golden check: SLOOP's own tests/ui_pages_test.c (the real UI on its test doubles) writes its pages; the core runs
# golden_mpyre.txt to the same state: the splash, the TRACKS page and the EDIT page are pixel for pixel the same,
# except the status corner (x 189..232, y 3..13: our HAL reports a USB host and a battery reading, the test's
# doubles none)
