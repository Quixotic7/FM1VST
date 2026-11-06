# SPDX-License-Identifier: GPL-3.0-only
# X0X (https://github.com/charlesvestal/fm1-x0x, tag v1.2.1): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the DSP, sequencer and engine units as the firmware builds them (cmake/FM1Cores.cmake)
# GATED (the integration's licence pass, CORES-BRIEF-PHASE1.md "when unsure, gate"): x0x_drum_samples.h compiles
# assets/909/{hh,ride,crash}.wav; LICENSING.md: "909 hi-hat, ride and crash samples (assets/909/) | ER-99 via 9W9 |
# GPL-3.0". Whether ER-99 recorded them or took them from the TR-909's cymbal ROM is not stated. Un-gated by: ER-99's
# author confirming they are original recordings (or X0X replacing them).
set(_x0x ${CMAKE_SOURCE_DIR}/cores/fm1-x0x)
fm1_add_core(x0x ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_x0x} UNIT core_x0x.c DESC core_x0x_desc.c
             EXTRA_SOURCES x0x_drum909.c x0x_drum808.c x0x_bass303.c x0x_breaks.c x0x_fxbus.c x0x_master.c
                           x0x_sequencer.c x0x_tb3po.c x0x_pattern.c x0x_motion.c x0x_engine.c
             FAMILY plat LICENSE GPL-3.0-only
             GATED "the 909 hi-hat, ride and crash samples (assets/909, from ER-99 via 9W9, said to be GPL-3.0) are of unverified provenance: they may be TR-909 ROM recordings"
             CREDITS "The break generator: ported from schwung-breakbeat (BB Gen) by mestela, used with the author's permission (that repository carries no licence file)."
             GEN felucca_font.h x0x_drum_samples.h ann_words.h x0x_break_bank.h PYTHON_NEEDS raqm
             VERSION 1.2.1
             FLAGS -ffp-contract=off
             INCLUDES ${_x0x}/firmware/src/dsp ${_x0x}/firmware/src/app)
# the golden check: X0X's own host simulator (host/build_host.sh's sources and flags) and the core, one script
include(${CMAKE_CURRENT_LIST_DIR}/plat_golden.cmake)
set(_u dsp/drum909 dsp/drum808 dsp/bass303 dsp/breaks dsp/fxbus dsp/master seq/sequencer seq/tb3po seq/pattern
       seq/motion app/engine)
list(TRANSFORM _u PREPEND ${_x0x}/firmware/src/)
list(TRANSFORM _u APPEND .c)
plat_golden(x0x GEN_TARGET gen_x0x SOURCES ${_x0x}/host/x0x_host.c ${_u} DEFINES X0X_HOST
            INCLUDES ${_x0x}/firmware/src ${_x0x}/firmware/src/dsp ${_x0x}/build/gen
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_x0x.txt)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The second core needs nothing more: the unit runs
# plat_cpu2_run's job at once on the one thread (fm1_cpu1_run), as natively. Not built while the core is gated.
# The licence texts: its GPL, its LICENSING.md and the two fonts' OFL (Barlow, Terminus)
fm1_add_rfm1(x0x AUTHORS "Charles Vestal"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             HOSTED_DESPITE_GATE "the owner's call, 2026-10-31 (Include X0X): hosted on RemoteFM1FW only, the cymbal-sample question stays open")
