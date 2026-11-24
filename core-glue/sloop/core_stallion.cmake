# SPDX-License-Identifier: GPL-3.0-only
# Stallion 1.1 (https://github.com/EberonLtd/stallion-fm1, tag v1.1 = 20292d1), by Eberon Limited (John Gorman): a
# synthesizer grown out of Clipper 1.3, SLOOP and Felucca. Its felucca.c includes what Clipper's does plus its own units
# (ui_atpop.c, key_touch.c via seq.c) and HAL headers (fm1_clk.h, fm1_hot.h, fm1_adc.h's short conversions): the
# SLOOP-family glue (core-glue/sloop) runs it as SLOOP with stallion_hal.h's stand-ins (FM1CORE_SL_STALLION).
# Not gated: Stallion replaced Felucca's all-rights-reserved assets (its icon atlas is drawn by tools/draw_icons.py,
# GPL-3.0; LICENSING.md "No additional permission"); the samples it compiles in are CC0 (assets/samples-cc0).
# The identity: tools/fm1pkg_make.py STALLION_NUM 1042 + X Y for release X.Y (FM-1_104211).
set(_stallion ${CMAKE_SOURCE_DIR}/cores/stallion-fm1)
set(_stallion_ver 1.1)
fm1_add_core(stallion ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_stallion} UNIT core_stallion.c
             DESC core_stallion_desc.c FAMILY sloop LICENSE GPL-3.0-only
             CREDITS "Stallion by Eberon Limited (John Gorman), github.com/EberonLtd/stallion-fm1. Copyright (C) 2026 Eberon Limited (John Gorman). Grown out of Clipper and SLOOP by 3dSam / isod89, based on Felucca by Leo Kuroshita (Hugelton Instruments). FM6: msfa (Google Inc., Pascal Gauthier), Apache-2.0. PHYS: DaisySP and Rings (Electrosmith, Emilie Gillet), MIT. Samples: Versilian Studios VSCO-2 CE and VCSL, Sonic Pi, CC0. Screen font: Terminus, SIL OFL 1.1. The full notices: LICENSING.md and CREDITS.md."
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h
                 felucca_wavetables.h sloop_logo.h
             PYTHON_NEEDS numpy
             DEFINES FELUCCA_ID="FM-1_104211"
             VERSION ${_stallion_ver})
# the .rfm1 (cmake/FM1Rfm1.cmake; docs/RFM1.md): its GPL, msfa's Apache-2.0, the MIT notices of the compressor's
# sources, its LICENSING.md and CREDITS.md, the CC0 samples' attribution and the Terminus font's OFL. The data needs
# 16 MiB, as Clipper's.
# rd1: sloop_rd.h's live knob labels and readouts (the columns as drawn)
fm1_add_rfm1(stallion WANTS rd1
             READOUT_TEST --press ENV@4100 --turn K1@4450:1 --turn K2@4550:1 --turn K3@4650:1 --turn K4@4750:1
                          --settle 4900 --seconds 0.15 --min-rms 0
                          --expect-readout ctl_3_ATK_=_1.2_ms --expect-readout ctl_6_REL_=_172_ms
             AUTHORS "Eberon Limited (John Gorman)" MEMORY_MIB 16
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "CREDITS.md ({name})=CREDITS.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (Airwindows Pop3 in {name})=LICENSES/MIT-Airwindows.txt"
                           "MIT (Lunar Modulator in {name})=LICENSES/MIT-Lunar-Modulator.txt"
                           "CC0-1.0 (the samples in {name})=assets/samples-cc0/ATTRIBUTION.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
