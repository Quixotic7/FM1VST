# SPDX-License-Identifier: GPL-3.0-only
# QUARTZ 0.10 (https://github.com/F0nski/quartz, tag v0.10 = 52206ea; f0nski's four-track groovebox on Plaits:
# GPL-3.0-or-later, with Mutable Instruments' Plaits, Marbles, Clouds and stmlib (MIT) and Grids' drum map (GPL-3.0),
# on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @ b0976f2, base/bsp = felucca-bsp @
# 5b0879b): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified submodule, from Mighty Arp's glue (the
# same fm1-base and felucca-bsp commits). QUARTZ's patches/ (applied to base/ by its build.sh) change only build.py,
# the web installer and unity.c's APP_LATE line: none of base/'s files compiled here; core_quartz.c includes APP_LATE
# after app.c as the patched unity.c does.
# The engine is C++ (fm1.toml "o2": dsp/*.cc, third_party/plaits/**/*.cc, third_party/stmlib/**/*.cc, the globs
# expanded as build.py o2_files does: each sorted), compiled from the submodule's own files with fm1.toml's o2_cflags
# (gnu++11, no exceptions, no RTTI, no thread-safe statics, no math errno; its -idirafter app/libc_include is the
# JieLi toolchain's libc, a device-only link not made here), -iquote each file's own directory (its
# fm1-base-o2-iquote.patch: Plaits' physical_modelling/string.h would shadow <string.h>), and PLAT_HOST PLAT_WEB as
# build.py --web and --rfm1 give the o2 units (cxx_runtime.cc's std::sort instance, which the module does not link).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts and the screen reader's words; one Python expression, run in
# app/ (GEN_ROOT). Not gated.
set(_qz ${CMAKE_SOURCE_DIR}/cores/quartz)
set(_qz_ver 0.10)
# build.py --release 0.10: APP_VERSION "0.10", the identity fm1.toml's id_prefix FM-1_98 + XX YY ZZ: FM-1_98001000
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_qz_ver}")
set(_qz_id "FM-1_98")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _qz_id "0")
    endif()
    string(APPEND _qz_id "${_d}")
endforeach()
set(_qz_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, its globs expanded in build.py's order
set(_qz_o2 dsp/cxx_runtime.cc dsp/pq.cc dsp/qfx.cc dsp/qvoice.cc
    third_party/plaits/dsp/chords/chord_bank.cc
    third_party/plaits/dsp/engine/additive_engine.cc third_party/plaits/dsp/engine/bass_drum_engine.cc
    third_party/plaits/dsp/engine/chord_engine.cc third_party/plaits/dsp/engine/fm_engine.cc
    third_party/plaits/dsp/engine/grain_engine.cc third_party/plaits/dsp/engine/hi_hat_engine.cc
    third_party/plaits/dsp/engine/modal_engine.cc third_party/plaits/dsp/engine/noise_engine.cc
    third_party/plaits/dsp/engine/particle_engine.cc third_party/plaits/dsp/engine/snare_drum_engine.cc
    third_party/plaits/dsp/engine/speech_engine.cc third_party/plaits/dsp/engine/string_engine.cc
    third_party/plaits/dsp/engine/swarm_engine.cc third_party/plaits/dsp/engine/virtual_analog_engine.cc
    third_party/plaits/dsp/engine/waveshaping_engine.cc third_party/plaits/dsp/engine/wavetable_engine.cc
    third_party/plaits/dsp/engine2/chiptune_engine.cc third_party/plaits/dsp/engine2/phase_distortion_engine.cc
    third_party/plaits/dsp/engine2/six_op_engine.cc third_party/plaits/dsp/engine2/string_machine_engine.cc
    third_party/plaits/dsp/engine2/virtual_analog_vcf_engine.cc third_party/plaits/dsp/engine2/wave_terrain_engine.cc
    third_party/plaits/dsp/fm/algorithms.cc third_party/plaits/dsp/fm/dx_units.cc
    third_party/plaits/dsp/physical_modelling/modal_voice.cc third_party/plaits/dsp/physical_modelling/resonator.cc
    third_party/plaits/dsp/physical_modelling/string.cc third_party/plaits/dsp/physical_modelling/string_voice.cc
    third_party/plaits/dsp/speech/lpc_speech_synth.cc third_party/plaits/dsp/speech/lpc_speech_synth_controller.cc
    third_party/plaits/dsp/speech/lpc_speech_synth_phonemes.cc third_party/plaits/dsp/speech/lpc_speech_synth_words.cc
    third_party/plaits/dsp/speech/naive_speech_synth.cc third_party/plaits/dsp/speech/sam_speech_synth.cc
    third_party/plaits/resources.cc
    third_party/stmlib/dsp/atan.cc third_party/stmlib/dsp/units.cc third_party/stmlib/utils/random.cc)
set(_qz_o2_rel "")
foreach(_f IN LISTS _qz_o2)
    list(APPEND _qz_o2_rel ../../cores/quartz/app/${_f})
endforeach()
fm1_add_core(quartz ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_qz} UNIT core_quartz.c DESC core_quartz_desc.c
             EXTRA_SOURCES ${_qz_o2_rel}
             FAMILY fm1base LICENSE GPL-3.0-or-later
             CREDITS "QUARTZ by f0nski, GPL-3.0-or-later, built on fm1-base and on Mutable Instruments' Plaits, Grids, Marbles and Clouds by Emilie Gillet (MIT, Grids GPL-3.0). Built on fm1-base by Charles Vestal and felucca-bsp from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_qz_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_qz_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_qz_ver}" FELUCCA_ID="${_qz_id}"
             INCLUDES ${_qz}/app ${_qz}/app/third_party ${_qz}/app/dsp ${_qz}/base/include ${_qz}/base/device ${_qz}/base/lib)
# the C++ units' own flags (native and, read by fm1_add_rfm1, the .rfm1's: after its C flags, so -std=gnu++11 wins)
if(TARGET core_quartz_obj)
    foreach(_f IN LISTS _qz_o2)
        get_filename_component(_d ${_qz}/app/${_f} DIRECTORY)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/quartz/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu++11;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-fno-math-errno;-ffp-contract=off;-w;-DPLAT_HOST;-DPLAT_WEB;-ffunction-sections;-fdata-sections;-iquote${_d}")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: QUARTZ's LICENSE (GPL-3.0),
# Mutable Instruments' MIT notice (stmlib's LICENSE, the notice of Plaits, Marbles, Clouds), fm1-base's and
# felucca-bsp's MPL-2.0 with their notes, the Barlow OFL (gen_font.py's set; Terminus is not built in).
fm1_add_rfm1(quartz AUTHORS "f0nski"
             LICENSE_FILES "GPL-3.0 ({name}, LICENSE)=LICENSE"
                           "MIT (Mutable Instruments: Plaits, Marbles, Clouds, stmlib, app/third_party/stmlib/LICENSE)=app/third_party/stmlib/LICENSE"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
