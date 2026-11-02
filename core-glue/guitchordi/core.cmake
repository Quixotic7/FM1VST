# SPDX-License-Identifier: GPL-3.0-only
# GuitChordi (https://github.com/stoogs/guitchordi, tag v0.1.0-beta.1 = 09a4f17; GPL-3.0-only), Stoogs' six-string
# chord instrument: its own glue family (core-glue/guitchordi/README.md) over the unmodified submodule. Not a Felucca
# fork (no felucca.c, no plat.h, no web/sim): a portable application (firmware/app.c: the input, the clock, the UI,
# the screen) and its string DSP (dsp/engine.c over Clipper's phys_dsp.c) behind the fw_* calls, and a device adapter
# (firmware/fm1/main.c) on X0X's Felucca HAL. The unit is app.c with the device adapter's work redone on the ABI
# (core_guitchordi.c); the DSP is its own unit, as scripts/build-fm1.py builds them (engine.c -O2, the rest -Os, all
# -ffp-contract=off -fwrapv -fno-strict-aliasing, -DGM_FM1=1). Generated: build/generated/fw_bank.h (the chords, the
# 50 styles, the 14 tunings' fret tables) by scripts/generate-firmware-bank.py from web/chords.json, web/styles.json
# and docs/tuning-presets.json, copied to build/gen for fm1_add_core's dependency (the script takes ~30 s).
#
# The pin is a beta: the owner's call, 2026-11-02: the Hub's latest. (RFM1Manager pins stable releases only; nothing in
# fm1_add_core checks it: VERSION is the tag's own version string.)
set(_gc ${CMAKE_SOURCE_DIR}/cores/guitchordi)
fm1_add_core(guitchordi ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_gc} UNIT core_guitchordi.c DESC core_guitchordi_desc.c
             EXTRA_SOURCES guitchordi_engine.c
             FAMILY guitchordi LICENSE GPL-3.0-only
             CREDITS "GuitChordi by Stoogs, GPL-3.0-only. Its device platform: X0X by Charles Vestal, derived from Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0-only.\nThe physical string DSP (phys_dsp.c): Clipper by isod89, Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet, fixed-point C port for Felucca by Leo Kuroshita, derived from DaisySP, MIT.\nThe screen's glyphs: Noto Sans, Copyright 2022 The Noto Project Authors, SIL OFL 1.1.\nM-VAVE and FM-1 belong to their respective owners. GuitChordi is independent and not endorsed by them."
             CREDITS_FILES third_party/clipper-phys/MIT-LICENSE.txt
             GEN fw_bank.h
             GEN_PYTHON "(__import__('runpy').run_path('scripts/generate-firmware-bank.py'), __import__('os').makedirs('build/gen', exist_ok=True), __import__('shutil').copyfile('build/generated/fw_bank.h', 'build/gen/fw_bank.h'))"
             VERSION 0.1.0-beta.1
             DEFINES GM_FM1=1
             FLAGS "-Dsection(x)=aligned(4)" -ffp-contract=off -fwrapv -fno-strict-aliasing)
# (the generator's inputs: fm1_add_core's step depends on tools/*.py, which this tree has none of)
if(TARGET gen_guitchordi)
    add_custom_command(OUTPUT ${_gc}/build/gen/fw_bank.h APPEND
                       DEPENDS ${_gc}/scripts/generate-firmware-bank.py ${_gc}/scripts/tuning-voicings.py
                               ${_gc}/web/chords.json ${_gc}/web/styles.json ${_gc}/docs/tuning-presets.json)
endif()
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/guitchordi_engine.c PROPERTIES
                            COMPILE_OPTIONS "-w;-ffp-contract=off;-fwrapv;-fno-strict-aliasing")

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in (NOTICES.md's
# table): the GPL (GuitChordi, the X0X / Felucca adapter code it follows), the MIT notice of Clipper's string DSP, the
# OFL of the Noto Sans glyphs in display-font.h, and NOTICES.md itself. Not the JieLi SDK's Apache-2.0 (device
# packages only), Emscripten / musl (the firmware's own browser build) or the research tools.
fm1_add_rfm1(guitchordi AUTHORS "Stoogs"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "NOTICES.md ({name})=NOTICES.md"
                           "MIT (Clipper's physical string DSP in {name})=third_party/clipper-phys/MIT-LICENSE.txt"
                           "OFL-1.1 (the Noto Sans glyphs in {name})=firmware/fonts/OFL.txt")
