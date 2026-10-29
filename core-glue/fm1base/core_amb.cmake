# SPDX-License-Identifier: GPL-3.0-only
# AMB-1 (https://github.com/charlesvestal/fm1-amb, tag v1.3; MPL-2.0): the fm1-base glue (core-glue/fm1base/README.md)
# over the unmodified submodule and its own submodules (base = fm1-base, base/bsp = felucca-bsp); the engine
# (app/dsp/amb.c) its own unit, as fm1-base's build.py builds an fm1.toml "o2" unit.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): one Python expression, run in app/ (GEN_ROOT).
set(_amb ${CMAKE_SOURCE_DIR}/cores/fm1-amb)
set(_amb_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
fm1_add_core(amb ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_amb} UNIT core_amb.c DESC core_amb_desc.c
             EXTRA_SOURCES amb_amb.c
             FAMILY fm1base LICENSE MPL-2.0
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_amb_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION 1.3
             FLAGS -ffp-contract=off
             INCLUDES ${_amb}/app ${_amb}/app/dsp ${_amb}/base/include ${_amb}/base/device ${_amb}/base/lib)
# the golden check: fm1-base's host simulator (build.py --host: base/host/plat_host.c with the app, its o2 unit) and
# the core, one script
include(${CMAKE_CURRENT_LIST_DIR}/../plat/plat_golden.cmake)
plat_golden(amb GEN_TARGET gen_amb SOURCES ${_amb}/base/host/plat_host.c ${_amb}/app/dsp/amb.c DEFINES PLAT_HOST
            INCLUDES ${_amb}/base/device ${_amb}/base/include ${_amb}/base/lib ${_amb}/app ${_amb}/app/build/gen
                     ${_amb}/app/dsp
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_amb.txt)
# the key sensing path (velocity and pressure through lib/touch.c, for an fm1-base app with APP_TOUCH: CTL-1, MONK-1)
# is compiled out for AMB-1; this object builds the same unit with APP_TOUCH so the path keeps compiling
if(TARGET amb_core)
    add_library(fm1base_touch_check OBJECT ${CMAKE_CURRENT_LIST_DIR}/core_amb.c)
    target_include_directories(fm1base_touch_check PRIVATE ${_amb}/app/build/gen ${_amb}/app ${_amb}/app/dsp
                               ${_amb}/base/include ${_amb}/base/device ${_amb}/base/lib ${FM1_CORE_API_DIR}
                               ${FM1_COMMON_DIR})
    target_compile_definitions(fm1base_touch_check PRIVATE APP_TOUCH FM1CORE_ID="amb" FM1CORE_VERSION="1.3")
    target_compile_options(fm1base_touch_check PRIVATE -w -fno-common)
    add_dependencies(fm1base_touch_check gen_amb)
endif()
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(amb AUTHORS "Charles Vestal")
