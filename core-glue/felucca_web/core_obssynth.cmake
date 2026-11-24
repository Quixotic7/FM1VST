# SPDX-License-Identifier: GPL-3.0-only
# ObsSynth FM-1 0.2.0 (https://github.com/orianbsilva/obssynth-fm1, tag v0.2.0 = f7aea06): Orian Beltrame's
# four-voice synthesizer on Felucca, by way of Hortator (DEADACTIVE). Not Felucca's web/emu/felucca_web.c (ObsSynth
# has none, so not fw_core.h either): its own browser simulator, tests/sim_core.c over tests/sim_host.h, unmodified,
# driven by core_obssynth.c (the model: Hortator's glue, core_hortator.c, whose simulator this one is built from).
# Built as tools/build_sim.sh builds fm1sim.wasm: the generated headers on the path (build.py generate()'s
# felucca_font.h / felucca_tables.h / synth_wavetables.h / synth_samples.h, and sim_keymap.h: hal/fm1_input.h's
# FM1_KEYMAP, which build_sim.sh cuts out with awk, here the same lines in the same GEN_PYTHON expression; chr(59) is
# the ';' of '};', a CMake list separator), firmware/src and tests on the include path, SYNTH_VERSION "<VERSION.txt>"
# and SYNTH_BUILD the commit (as build_sim.sh gives them); the simulator's own defines are in sim_host.h.
# The version: VERSION.txt (the release's tag v<version>).
set(_ob ${CMAKE_SOURCE_DIR}/cores/obssynth-fm1)
set(_ob_ver 0.2.0)
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(obssynth ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ob} UNIT core_obssynth.c
             DESC core_obssynth_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "ObsSynth FM-1 by Orian Beltrame, built on Felucca by Leo Kuroshita (Hügelton Instruments) and Hortator by DEADACTIVE (its knob decoder, pixel fonts and simulator), GPL-3.0-only. GRAIN's samples: VCSL, the Versilian Community Sample Library (CC0 1.0, the pipe organ sampled by Simon Dalzell of Ivy Audio), the wavetables and VOX made by the author. M-VAVE and FM-1 are trademarks of their owners: ObsSynth is not affiliated with them."
             GEN felucca_font.h felucca_tables.h synth_wavetables.h synth_samples.h sim_keymap.h
             GEN_PYTHON "(__import__('build').generate(), __import__('pathlib').Path('build/gen/sim_keymap.h').write_text(__import__('re').search(r'(?m)^.*static const int8_t FM1_KEYMAP(?s:.*?)^}' + chr(59), open('firmware/hal/fm1_input.h').read()).group(0) + chr(10)))"
             VERSION ${_ob_ver}
             INCLUDES ${_ob}/tests
             DEFINES SYNTH_VERSION="${_ob_ver}" SYNTH_BUILD="f7aea06")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md): the GPL (ObsSynth, Felucca, Hortator), LICENSING.md, and the CC0 samples' provenance
# (assets/samples/README.md). Not the JieLi SDK's Apache-2.0 (device packages only) nor jl-uboot-tool's MIT (the
# recovery tool): neither is in the firmware. RUN_ARGS: none (the keys play the power-on INIT sound).
# rd1: its knob row as drawn, live labels and readouts (core_obssynth.c, common/rd_cols.h)
fm1_add_rfm1(obssynth WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_ATK_=_1 --expect-readout ctl_6_REL_=_51
             AUTHORS "Orian Beltrame" "DEADACTIVE" "Leo Kuroshita (Hügelton Instruments)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "CC0-1.0 (VCSL, GRAIN's samples in {name})=assets/samples/README.md")
