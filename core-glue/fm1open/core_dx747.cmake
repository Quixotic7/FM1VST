# SPDX-License-Identifier: GPL-3.0-only
# DX747 0.1 by Esben von Buchwald (https://github.com/esbenvb/fm1-dx747, tag v0.1 = bbae5d4; GPL-3.0-only with the
# FM1 SDK linking exception), an eight-part multitimbral DX7 module played over MIDI: a fork of M-VAVE's open APP
# (fm1_open: app/ over sdk/, whose engine is the binary sdk/lib/libfm1_engine.a). Its own glue family
# (core-glue/fm1open/README.md) over the unmodified submodule, built as DX747's own browser core is
# (tools/portable/browser-core.sh, tools/Build-Web.ps1's Build-Simulator): every .c under app/ but app/board/ (the
# hardware), web/ (web/engine: the engine's open C source in the place of libfm1_engine.a, and Simulator.c, the
# browser's platform layer) and third_party/synth/, with FM1_HOST_TEST and FM1_OPEN_WEB, the same include order and
# -fwrapv -ffp-contract=off. Simulator.c is included by the unit (core_dx747.c), the rest are their own units. The
# proprietary engine library is not linked. Nothing is generated: the factory DX7 bank (tools/Generate-Factory-DX7.mjs)
# is committed as app/fm_factory.inc.
set(_dx ${CMAKE_SOURCE_DIR}/cores/fm1-dx747)
set(_dx_glue ${CMAKE_CURRENT_LIST_DIR})
file(RELATIVE_PATH _dx_rel ${_dx_glue} ${_dx})
file(GLOB_RECURSE _dx_abs CONFIGURE_DEPENDS ${_dx}/app/*.c ${_dx}/web/*.c ${_dx}/third_party/synth/*.c)
list(FILTER _dx_abs EXCLUDE REGEX "/app/board/")
list(FILTER _dx_abs EXCLUDE REGEX "/web/engine/Simulator\\.c$")   # (in the unit)
list(SORT _dx_abs)
set(_dx_units "")
foreach(_f IN LISTS _dx_abs)
    file(RELATIVE_PATH _r ${_dx} ${_f})
    list(APPEND _dx_units ${_dx_rel}/${_r})
endforeach()
if(NOT EXISTS ${_dx}/web/engine/Simulator.c OR NOT EXISTS ${_dx}/app/screen.c)
    message(FATAL_ERROR "dx747: no web/engine/Simulator.c or app/screen.c in ${_dx} (a new pin: check the glue)")
endif()

fm1_add_core(dx747 ${_dx_glue} SOURCE ${_dx} UNIT core_dx747.c DESC core_dx747_desc.c
             EXTRA_SOURCES ${_dx_units}
             FAMILY fm1open LICENSE GPL-3.0-only
             NO_GEN
             VERSION 0.1
             CREDITS "DX747 by Esben von Buchwald, a fork of M-VAVE's open FM1 APP (fm1_open, Copyright (c) 2026 M-VAVE, GPL-3.0-only with the FM1 SDK linking exception), with its open web engine (incorporating contributions by Vikram Rojo).\nThe FM model is a C adaptation of MSFA / Dexed (Google, Pascal Gauthier: Apache-2.0). The 303 after Open303 (Robin Schmidt), 808 and Pluck after DaisySP (Electrosmith, Emilie Gillet), the SVF after stmlib (Emilie Gillet), the PSG after emu2149 (Mitsutaka Okazaki), the SEA codec after Daninet/sea-codec (Dani Biro): MIT.\nThe 128-voice factory DX7 bank is M-VAVE's V15 bank (Apache-2.0, THIRD_PARTY.md). The full notices: LICENSE.md and THIRD_PARTY.md."
             FLAGS -fwrapv -ffp-contract=off
             DEFINES FM1_HOST_TEST FM1_OPEN_WEB
             INCLUDES ${_dx}/web/engine ${_dx}/web/synth ${_dx}/third_party/synth ${_dx}/sdk/include ${_dx}/app/synth
                      ${_dx}/app)
if(TARGET core_dx747_obj)
    # the firmware's own units: warnings off, its arithmetic flags (browser-core.sh)
    list(TRANSFORM _dx_units PREPEND ${_dx_glue}/ OUTPUT_VARIABLE _dx_paths)
    set_source_files_properties(${_dx_paths} PROPERTIES COMPILE_OPTIONS "-w;-fwrapv;-ffp-contract=off")
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: DX747's LICENSE.md (GPL-3.0 with
# the SDK linking exception) and THIRD_PARTY.md (the MIT and Apache-2.0 components compiled in, the factory bank's
# source). The built-in keys play the selected part (part 1, MIDI channel 1, at power-on): rfm1_run's chord sounds
fm1_add_rfm1(dx747 AUTHORS "Esben von Buchwald"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE.md"
                           "THIRD_PARTY.md ({name})=THIRD_PARTY.md")
