# SPDX-License-Identifier: GPL-3.0-only
# Mighty Arp 1.5 (https://github.com/NickCulbertson/FM1-Mighty-Arp, tag v1.5 = 1283d87; Nick Culbertson's FM / subtractive
# synth with a chance arpeggiator: MPL-2.0, on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @
# b0976f2, base/bsp = felucca-bsp @ 5b0879b): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified
# submodule, from CTL-1's glue (fm1-base 3ae2a65: b0976f2 is it plus usb_audio_channels' plat_usb_ch). The engine
# (fm1.toml "o2": app/dsp/engine.c) is its own unit (mightyarp_dsp_engine.c, PLAT_HOST).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts and the screen reader's words; one Python expression, run in
# app/ (GEN_ROOT). Not gated.
set(_ma ${CMAKE_SOURCE_DIR}/cores/FM1-Mighty-Arp)
set(_ma_ver 1.5)
# build.sh --release 1.5 (tools/xxyy.py over build.py): APP_VERSION "1.5", the identity fm1.toml's id_prefix FM-1_6279 +
# XX YY (major, minor; Mighty Arp's releases are X.Y): FM-1_62790105
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _m "${_ma_ver}")
set(_ma_id "FM-1_6279")
foreach(_g 1 2)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d LESS 10)
        string(APPEND _ma_id "0")
    endif()
    string(APPEND _ma_id "${_d}")
endforeach()
set(_ma_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
fm1_add_core(mightyarp ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ma} UNIT core_mightyarp.c DESC core_mightyarp_desc.c
             EXTRA_SOURCES mightyarp_dsp_engine.c
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "Mighty Arp for the FM-1 by Nick Culbertson (Moby Pixel), MPL-2.0, from fm1-base's template by Charles Vestal. Built on fm1-base by Charles Vestal and felucca-bsp from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_ma_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_ma_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_ma_ver}" FELUCCA_ID="${_ma_id}"
             INCLUDES ${_ma}/app ${_ma}/app/dsp ${_ma}/base/include ${_ma}/base/device ${_ma}/base/lib)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: Mighty Arp's LICENSE (MPL-2.0) and
# LICENSING.md, fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL (gen_font.py's set; Terminus is not built in).
fm1_add_rfm1(mightyarp AUTHORS "Nick Culbertson"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "{name} licensing (LICENSING.md)=LICENSING.md"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
