# SPDX-License-Identifier: GPL-3.0-only
# SLOOP 3 3.4.0, public beta (https://github.com/isod89/sloop3-fm1, tag v3.4.0-beta = 2228313; no stable release yet:
# the owner's pin, 2026-11-18). A from-scratch rewrite of SLOOP for the FM-1 (float engines, 32 voices on two cores),
# not SLOOP 2.8's code: its own glue folder, over the unmodified submodule.
# The model is the author's own PC twin (host/hal.c + host/twin.c, `make twin`): fw/main.c unchanged on a simulated
# FM-1. core_sloop3.c is that twin's HAL rewritten for a core (no mmap / signals / threads: an .rfm1 is Wasm), with
# fw/main.c's never-returning app_main on a coroutine (common/coro.h). The firmware's other units are compiled as the
# twin compiles them (its Makefile's TWINSRC, -DHOST, -ffp-contract=off). Every generated table, the fonts and the
# presets are committed (NO_GEN); the sample bank (assets/samples.bin) is embedded and written at its flash place.
set(_s3 ${CMAKE_SOURCE_DIR}/cores/sloop3-fm1)
set(_s3_ver 3.4.0)
set(_s3_glue ${CMAKE_CURRENT_LIST_DIR})
file(RELATIVE_PATH _s3_rel ${_s3_glue} ${_s3})

# the twin's units (Makefile TWINSRC) less its host/ files and fw/main.c (core_sloop3.c includes it)
set(_s3_units app/project.c app/psound.c app/presets.c app/preset_data.c app/sloopimport.c app/sloop_kinds.c
              app/usersnd.c app/xbank.c app/edproto.c app/store.c app/ui.c app/gfx.c app/vis.c app/fonts.c
              app/harmony.c app/midi.c app/arp.c app/engdesc.c app/demo_song.c seq/seq.c
              engine/sound.c engine/fx.c engine/master.c engine/common.c engine/va.c engine/drum.c engine/drumkits.c
              engine/fm.c engine/lofi.c engine/pd.c engine/wheel.c engine/noise.c engine/voice.c engine/trio.c
              engine/phys.c engine/fm6.c engine/fm6_patches.c engine/sample.c engine/grain.c engine/slice.c
              engine/slice_scan.c engine/trackfx.c engine/texture.c engine/engines.c)
list(TRANSFORM _s3_units PREPEND ${_s3_rel}/)
# (the device's section attributes, .lowbss, .xtext.*, the twin's "twlow": Mach-O rejects them, Wasm needs none)
set(_s3_flags -ffp-contract=off "-Dsection(x)=aligned(4)")

fm1_add_core(sloop3 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_s3} UNIT core_sloop3.c DESC core_sloop3_desc.c
             EXTRA_SOURCES ${_s3_units}
             FAMILY sloop3 LICENSE GPL-3.0-only
             CREDITS "SLOOP 3 by isod89 (github.com/isod89/sloop3-fm1), GPL-3.0-only, 3.4.0 a public beta.\nThe samples are CC0 (assets/CREDITS-SAMPLES.txt). The screen fonts: Inter Bold (SIL OFL 1.1, rendered by tools/mkfont.py) and Terminus (SIL OFL 1.1)."
             NO_GEN
             VERSION ${_s3_ver}
             FLAGS ${_s3_flags}
             DEFINES HOST)
if(TARGET core_sloop3_obj)
    foreach(_u IN LISTS _s3_units)
        set_source_files_properties(${_s3_glue}/${_u} PROPERTIES COMPILE_OPTIONS "-w;${_s3_flags}")
    endforeach()
    # (the unit embeds the sample bank: rebuilt when it changes)
    set_source_files_properties(${_s3_glue}/core_sloop3.c PROPERTIES OBJECT_DEPENDS ${_s3}/assets/samples.bin)
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake; docs/RFM1.md). Its GPL, the samples' CC0 credits, the Terminus font's OFL and Inter's
# OFL (assets/fonts/OFL-Inter.txt: the big screen type, app/fonts.c from Inter Bold by tools/mkfont.py)
fm1_add_rfm1(sloop3 AUTHORS "isod89" MEMORY_MIB 16
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "CC0-1.0 (the samples in {name})=assets/CREDITS-SAMPLES.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Inter font in {name})=assets/fonts/OFL-Inter.txt")
