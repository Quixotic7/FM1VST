# SPDX-License-Identifier: GPL-3.0-only
# CTL-1 1.0.1 (https://github.com/charlesvestal/fm1-ctl, tag v1.0.1 = 2b560cf; Charles Vestal's MIDI controller firmware
# with an onboard GM synth: MPL-2.0, on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @
# 698e2cb, base/bsp = felucca-bsp @ f878800): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified
# submodule, from AMB-1's glue. The GM synth (fm1.toml "o2": app/dsp/gm.c and Sonivox EAS, app/dsp/eas/, Apache-2.0)
# is its own units, straight from the submodule, with build.py's host flags (-O2 -ffp-contract=off -std=c99 -w
# -DPLAT_HOST, fm1.toml's include list dsp, dsp/eas; each file's own directory). The device-only include
# (device_include dsp/eas/fm1inc: a <string.h> for the freestanding toolchain) stays out, as on the host.
# The build is CTL-1's USB one (build.py --no-ble, ctl-usb.fwsc): FM1B_BLE 0 (plat.h's default), radio_none.c.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's
# words; one Python expression, run in app/ (GEN_ROOT). Not gated.
set(_ctl ${CMAKE_SOURCE_DIR}/cores/fm1-ctl)
set(_ctl_ver 1.0.1)
# build.py --release 1.0.1: APP_VERSION "1.0.1", the identity fm1.toml's id_prefix FM-1_6 + 01 00 01
string(TOUPPER "${_ctl_ver}" _ctl_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_ctl_ver}")
set(_ctl_id "FM-1_6")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _ctl_id "0")
    endif()
    string(APPEND _ctl_id "${_d}")
endforeach()
set(_ctl_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_ctl_o2 dsp/gm.c dsp/eas/eas_chorus.c dsp/eas/eas_chorusdata.c dsp/eas/eas_config.c dsp/eas/eas_data.c
    dsp/eas/eas_filter.c dsp/eas/eas_host_fm1.c dsp/eas/eas_math.c dsp/eas/eas_midi.c dsp/eas/eas_mididata.c
    dsp/eas/eas_mixbuf.c dsp/eas/eas_mixer.c dsp/eas/eas_pan.c dsp/eas/eas_pcm.c dsp/eas/eas_pcmdata.c
    dsp/eas/eas_public.c dsp/eas/eas_reverb.c dsp/eas/eas_reverbdata.c dsp/eas/eas_smf.c dsp/eas/eas_smfdata.c
    dsp/eas/eas_sndlibmgt.c dsp/eas/eas_voicemgt.c dsp/eas/eas_wtengine.c dsp/eas/eas_wtsynth.c dsp/eas/wt_200k_G.c)
set(_ctl_o2_rel "")
foreach(_f IN LISTS _ctl_o2)
    list(APPEND _ctl_o2_rel ../../cores/fm1-ctl/app/${_f})
endforeach()
fm1_add_core(ctl1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ctl} UNIT core_ctl1.c DESC core_ctl1_desc.c
             EXTRA_SOURCES ${_ctl_o2_rel}
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "CTL-1 for the FM-1 by Charles Vestal (MPL-2.0). The General MIDI sounds are Sonivox EAS (Apache-2.0, Copyright (c) 2004-2006 Sonic Network Inc., 2008 The Android Open Source Project). The Euclidean rhythms are Godfried Toussaint's reading of Bjorklund's algorithm. Built on fm1-base by Charles Vestal and felucca-bsp from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_ctl_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_ctl_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_ctl_app_ver}" FELUCCA_ID="${_ctl_id}"
             INCLUDES ${_ctl}/app ${_ctl}/app/dsp ${_ctl}/app/dsp/eas ${_ctl}/base/include ${_ctl}/base/device
                      ${_ctl}/base/lib)
if(TARGET core_ctl1_obj)
    # the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects' host flags,
    # fm1.toml's include list (dsp, dsp/eas), each file's own directory
    foreach(_f IN LISTS _ctl_o2)
        get_filename_component(_d ${_ctl}/app/${_f} DIRECTORY)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm1-ctl/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=c99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_d};-I${_ctl}/app/dsp;-I${_ctl}/app/dsp/eas")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: CTL-1's LICENSE (MPL-2.0) and
# LICENSING.md, Sonivox EAS's Apache-2.0 and NOTICE, fm1-base's and felucca-bsp's MPL-2.0 with their notes, the
# Barlow OFL. CTL-1's onboard GM synth plays its notes (Sound on, the default): the chord sounds.
fm1_add_rfm1(ctl1 AUTHORS "Charles Vestal"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "{name} licensing (LICENSING.md)=LICENSING.md"
                           "Apache-2.0 (Sonivox EAS, app/dsp/eas/LICENSE-2.0.txt)=app/dsp/eas/LICENSE-2.0.txt"
                           "Sonivox EAS notice (app/dsp/eas/NOTICE)=app/dsp/eas/NOTICE"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
