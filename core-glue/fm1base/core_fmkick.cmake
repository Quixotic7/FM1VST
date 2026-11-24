# SPDX-License-Identifier: GPL-3.0-only
# FM-KICK 0.5.1 (https://github.com/davidamomusic/fm-kick, tag v0.5.1 = c4a8a5f; David Amo's kick drum synthesizer and
# sequencer: MPL-2.0. The repository is fm1-base itself, MPL-2.0, with the app in kick/, and its own submodule bsp =
# felucca-bsp @ 91583ec, MPL-2.0): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified submodule, from
# ECHOFIELD's glue (core_echofield.c: backup.c, the service's coroutine; its fm1-base 4039a8b is the nearest with
# backup) and Mighty Arp's (fm1-base b0976f2: FM-KICK's fm1-base is it plus plat_out_off). The DSP (fm1.toml "o2":
# kick/dsp.c, kick/fx.c) is two units of its own (fmkick_dsp.c, fmkick_fx.c, PLAT_HOST).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from kick/fm1.toml, GEN = kick/build/gen): the fonts (Barlow Semi Condensed: OFL) and the screen reader's
# words; one Python expression, run in kick/ (GEN_ROOT). Not gated.
set(_fk ${CMAKE_SOURCE_DIR}/cores/fm-kick)
set(_fk_ver 0.5.1)
# the release (kick/release.sh): the package identity is fixed, FM-1_222 (FM-KICK's own number from 0.5.0 on), and
# APP_VERSION is kick/app.h's own (the tag v0.5.1's app.h still says "0.5.0", which GLO's SYSTEM page shows)
set(_fk_id "FM-1_222")
set(_fk_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../tools') or __import__('build'))")
fm1_add_core(fmkick ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fk} UNIT core_fmkick.c DESC core_fmkick_desc.c
             EXTRA_SOURCES fmkick_dsp.c fmkick_fx.c
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "FM-KICK by David Amo, MPL-2.0. Built on fm1-base by Charles Vestal and felucca-bsp from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT kick GEN_PYTHON "${_fk_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_fk_ver}
             FLAGS -ffp-contract=off
             DEFINES FELUCCA_ID="${_fk_id}"
             INCLUDES ${_fk}/kick ${_fk}/include ${_fk}/device ${_fk}/lib)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: the repository's MPL-2.0 (LICENSE),
# LICENSING.md and CREDITS.md (fm1-base's, which FM-KICK is), felucca-bsp's MPL-2.0 with its notes, the Barlow OFL
# (gen_font.py's set; Terminus is not built in)
# rd1: the footer's knobs as drawn, live labels and readouts (core_fmkick.c, common/rd_cols.h)
fm1_add_rfm1(fmkick WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press LFO@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_TARGET_=_TYPE --expect-readout ctl_6_LENGTH_=_17
             AUTHORS "David Amo"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "{name} licensing (LICENSING.md)=LICENSING.md"
                           "fm1-base credits (CREDITS.md)=CREDITS.md"
                           "MPL-2.0 (felucca-bsp, bsp/LICENSE)=bsp/LICENSE"
                           "felucca-bsp licensing (bsp/LICENSING.md)=bsp/LICENSING.md"
                           "felucca-bsp notice (bsp/NOTICE)=bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=assets/fonts/Barlow-OFL.txt")
