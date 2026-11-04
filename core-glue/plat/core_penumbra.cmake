# SPDX-License-Identifier: GPL-3.0-only
# Penumbra (https://github.com/MadMooneyham/Penumbra, tag v1.1 = ffeffb6; GPL-3.0-only), Lunar Eclipse Forge's
# space-drone instrument, a fork of X0X on Felucca's platform: the plat glue (core-glue/plat/README.md) over the
# unmodified submodule. The device's unity build (penumbra.c: the UI, the settings and journeys, the store, the
# screen reader) is core_penumbra.c; the DSP, the clock, the journey, the looper and the engine their own units, as
# tools/build.py's O2_UNITS (penumbra_*.c, -O2, -ffp-contract=off). Generated, by tools/build.py generate(): the font
# (gen_font.py: Barlow Semi Condensed, Pillow with libraqm), the Lunar Eclipse Forge logo (gen_logo.py), the effects'
# tables (gen_dsp_tables.py) and the screen reader's words link (gen_words.py).
set(_pen ${CMAKE_SOURCE_DIR}/cores/Penumbra)
fm1_add_core(penumbra ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_pen} UNIT core_penumbra.c DESC core_penumbra_desc.c
             EXTRA_SOURCES penumbra_drone.c penumbra_vox.c penumbra_pulse.c penumbra_texture.c penumbra_fxbus.c
                           penumbra_spacevb.c penumbra_dsp_tables.c penumbra_master.c penumbra_clock.c
                           penumbra_journey.c penumbra_looper.c penumbra_engine.c
             FAMILY plat LICENSE GPL-3.0-only
             CREDITS "Penumbra by Lunar Eclipse Forge, a fork of X0X (Charles Vestal), itself of Felucca (Leo Kuroshita, Hügelton Instruments).\nThe tape delay, distortion and glue compressor: ported from 9W9 (athousanddetails), after ER-99 (Matthew Cieplak), GPL-3.0.\nThe second core's start-up: after Melodee (keremimo) and Jangada (zednaked).\nThe Barlow Semi Condensed font, SIL OFL 1.1."
             GEN felucca_font.h logo.h dsp_tables.h ann_words.h PYTHON_NEEDS raqm
             VERSION 1.1
             FLAGS -ffp-contract=off
             INCLUDES ${_pen}/firmware/src/dsp ${_pen}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The drone swells in at power-on (PLAY AT POWER-ON is
# on by default): no RUN_ARGS needed. The licence texts: its GPL, its LICENSING.md and the Barlow font's OFL (1.1 dropped Terminus)
fm1_add_rfm1(penumbra AUTHORS "Lunar Eclipse Forge"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt")
