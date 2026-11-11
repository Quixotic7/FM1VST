# SPDX-License-Identifier: GPL-3.0-only
# M-PYRE 1.0.3 (https://github.com/HistoryOfSky/m-pyre, tag v1.0.3 = f50926b), by History Of Sky: a riffbox grown out
# of SLOOP 2.8.1, on Felucca. Its felucca.c includes what SLOOP's does (its amp, pedal and studio screens come in
# through those units): the SLOOP-family glue (core-glue/sloop: sloop_hal.h, sloop_firmware.h, sloop_fw.c,
# sloop_core.c) runs it as SLOOP, over the unmodified submodule cores/m-pyre (cmake/FM1Cores.cmake).
# GATED as SLOOP: the firmware compiles assets/icons.png (tools/gen_icons.py -> felucca_icons.h), the Hugelton icon
# atlas, all rights reserved; LICENSING.md's GPL section 7 permission to convey the combination asks to follow terms for
# the assets that are not published yet. Un-gate: those terms allow a plugin, or a build with replacement icons.
# The generate step: mpyre_kit.py first (build/mpyre/sets.json: the HEAVY KIT's recordings, assets/mpyre-src, are not
# in the public tree; without them a stand-in from the tree's CC0 kit, mpyre_kit.py), then build.py's generate().
# Without assets/mpyre-src/ir the amps use gen_mpyre_amp.py's generic cabinet (as the author's own build without them).
fm1_add_core(mpyre ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/m-pyre UNIT core_mpyre.c
             DESC core_mpyre_desc.c FAMILY sloop LICENSE GPL-3.0-only
             CREDITS "M-PYRE by History Of Sky (github.com/HistoryOfSky/m-pyre), based on SLOOP by isod89 and Felucca by Leo Kuroshita (Hugelton Instruments). Its boot picture: History Of Sky, CC BY 4.0. The HEAVY KIT's recordings are not in its public source: this build plays a stand-in made from the tree's CC0 kit (VCSL, Versilian Studios)."
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN_PYTHON "__import__('runpy').run_path('${CMAKE_CURRENT_LIST_DIR}/mpyre_kit.py') and __import__('build').generate()"
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h
                 sloop_logo.h mpyre_amp.h mpyre_amplogo.h
             PYTHON_NEEDS numpy scipy
             VERSION_FILE firmware/src/ui.c VERSION_REGEX "#define FELUCCA_VERSION \"M-PYRE ([0-9.]+)\"")
# the .rfm1 (cmake/FM1Rfm1.cmake; docs/RFM1.md; gated, built as a hosted file: HOSTED_DESPITE_GATE): its GPL, its
# LICENSING.md (History Of Sky's CC BY 4.0 boot picture, the CC0 samples), msfa's Apache-2.0 (the FM6 engine) and the
# OFL of the fonts drawn into it (Barlow Condensed: the screen font, gen_font.py; the five amp-name faces,
# gen_mpyre_amplogo.py). The data needs 16 MiB, as SLOOP's. RUN_ARGS: tracks 1..3 (HI-GAIN, LEAD GTR, DRIVE BASS) play
# the sound pack's recordings (soundpack.bin, a release file the web editor installs into the user slots: not in the
# .rfm1), silent without it; the web editor's own ED_TRACK SysEx (editor.c: F0 7D 46 4C 1B 03 F7) selects track 4,
# the HEAVY KIT, and the chord plays its drums.
fm1_add_rfm1(mpyre AUTHORS "History Of Sky" MEMORY_MIB 16
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "OFL-1.1 (the Barlow Condensed font in {name})=assets/fonts/BarlowCondensed-OFL.txt"
                           "OFL-1.1 (the Black Ops One font in {name})=assets/mpyre/fonts/OFL-blackopsone.txt"
                           "OFL-1.1 (the Damion font in {name})=assets/mpyre/fonts/OFL-damion.txt"
                           "OFL-1.1 (the Pacifico font in {name})=assets/mpyre/fonts/OFL-pacifico.txt"
                           "OFL-1.1 (the Racing Sans One font in {name})=assets/mpyre/fonts/OFL-racingsansone.txt"
                           "OFL-1.1 (the Russo One font in {name})=assets/mpyre/fonts/OFL-russoone.txt"
             HOSTED_DESPITE_GATE "the Hugelton icon atlas is compiled in: built as the author ships it, hosted on RemoteFM1FW only, the owner call of 2026-11-11"
             RUN_ARGS --sysex ${CMAKE_SOURCE_DIR}/tests/fixtures/mpyre/track4.syx)

# the golden check: M-PYRE's own tests/ui_pages_test.c (the real UI on its test doubles) writes its pages; the core runs
# golden_mpyre.txt (SLOOP's script) and its splash is compared, pixel for pixel. SLOOP's TRACKS and EDIT comparisons are
# left out: the test draws them in palette 2 on hostsim's tracks (host_tracks_init: empty patterns), the device powers
# on in palette 0 with felucca_init's tracks (the 1.0.3 pages differ in their colours: ~52000 pixels on TRACKS; with
# MENU > COLOR set to the test's palette, still the track rows' names and meters and the EDIT page's label colours)
if(TARGET mpyre_core AND NOT COMMAND fm1_sloop_golden)   # (FM1_ONLY_CORES=mpyre: the family's core.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/core.cmake)
endif()
if(TARGET mpyre_core)
    fm1_sloop_ref(mpyre_ref_ui_pages ${CMAKE_SOURCE_DIR}/cores/m-pyre/tests/ui_pages_test.c
                  ${CMAKE_SOURCE_DIR}/cores/m-pyre gen_mpyre)
    fm1_sloop_golden(mpyre REF mpyre_ref_ui_pages REF_ARGS %REF% SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_mpyre.txt
                     DRIVE_ARGS --compare 5 %REF%/page-splash.ppm --ignore 189 3 232 13)
endif()
