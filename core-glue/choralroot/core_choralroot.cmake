# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot 0.45 (https://github.com/Quixotic7/ChoralRootFM1, branch claude/engine-readout fa9ab7a9: 0.45 with the engine
# picker's readout and the takeover's knob steps; the Move takeover, cr_move.c; the FM-1 packages unchanged): its own emulator's unit,
# tools/emu/emu_fw.c, through core_choralroot.c and the family's glue (crf_glue.h; cmake/FM1Cores.cmake: fm1_add_core)
include(${CMAKE_CURRENT_LIST_DIR}/crf.cmake)
set(_cr ${CMAKE_SOURCE_DIR}/cores/ChoralRootFM1)
# the Tier 2 parameter map (core_choralroot_params.c): the sound editor's pages get host slots of their own too (30
# more; off, they are FM1P_HIDDEN: the knobs reach them in the editor). The plugin then needs -DFM1_TIER2_SLOTS=103 or
# more
option(FM1_EXPOSE_EDITOR "ChoralRoot's Tier 2 map: also slots for the sound editor's ENV / LFO / MOD / MIX pages" OFF)
set(_cr_defs "")
if(FM1_EXPOSE_EDITOR)
    set(_cr_defs FM1_EXPOSE_EDITOR=1)
endif()
# the version: firmware/src/choralroot.c's FELUCCA_VERSION ("ChoralRoot 0.26.1", the device build's default and
# tools/emu/build.sh's); the unit shows the same string (core_choralroot.c)
# UN-GATED (the owner's call, 2026-10-19): LICENSING.md lists the Casio CZ-1 preset tones ("the rights stay with
# Casio", "no licence granted") and Sequential's Prophet-5 factory programs ("the rights stay with Sequential"); the
# emulator unit compiles both in (FELUCCA_CZ 1; FELUCCA_PROPHET 1, FELUCCA_P5_FACTORY 1). They ship compiled into the
# firmware, as the device's own packages carry them; no raw .syx (assets/cz1-factory, assets/prophet5-factory) is
# distributed: tools/package_release.sh checks the release for them. The credits name both sets' owners
# GEN: tools/build.py generate()'s whole list (0.25: + cr_layouts.h, melodee_prophet_factory.h)
fm1_add_core(choralroot ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cr} UNIT core_choralroot.c DESC core_choralroot_desc.c
             PREFIX CR FAMILY choralroot LICENSE GPL-3.0-only
             CREDITS "The CZ-1 engine's factory tones are Casio's CZ-1 preset tones A-1 .. H-8 (Casio Computer Co., Ltd., 1986), and the PROPHET engine's factory presets are Sequential's Prophet-5 / 10 factory programs v1.03 (Sequential, 2025). Both are included as sound data for compatibility, compiled into the firmware. The rights stay with Casio and Sequential (LICENSING.md)."
             GEN ui_fonts.h cr_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h cr_layouts.h
                 felucca_fm6.h melodee_cz1.h melodee_prophet_factory.h felucca_samples.h
             VERSION_FILE firmware/src/choralroot.c
             VERSION_REGEX "^#define FELUCCA_VERSION \"(ChoralRoot )?([^\"]*)\"" VERSION_GROUP 2
             INCLUDES ${_cr}/tools/emu DEFINES ${_cr_defs})
# the golden check: the submodule's own D major script (screens and samples) against its reference emulator (the
# top-level reference_emu: tools/emu/build.sh at the pinned tree, the replay tests' ground truth)
crf_golden(choralroot SCRIPT ${_cr}/tools/emu/scripts/cr_dmaj.txt REF ${_cr}/build/host/emu REF_TARGET reference_emu
           AUDIO)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
# cpu2 (docs/RFM1.md): core 1's jobs (fx.c c1_part1_job, c1_fx_job) on a host thread where the host has one (cr_cpu1.h)
# rd1 (docs/RFM1.md): its popups' words (cr_ui.c), the map's bridge and the knobs' labels from knob_target
# (published as 0.42 again with these bytes, the owner's choice of 2026-11-19: the apps offer the same version with
# another sha256 as an update, and rank "0.42-r2" below "0.42" by semver)
# tk1 (docs/RFM1-TAKEOVER.md): ChoralRoot's own Ableton Move interface, "ChoralRoot" (the submodule's firmware/src/cr_move.c,
# which tools/emu/emu_fw.c includes only with FM1_HOST_TK: the FM-1's image is unchanged), claiming all but the Track
# buttons and the volume knob
# ep1 (docs/RFM1.md "Engine parameters (ep1)", engine-params/SPEC.md): every engine's parameters by id for the host's
# ENGINE page (cr_ep.c: the editor's walk, its knob's writes; the ids frozen in cr_ep_ids.h), only in the .rfm1
fm1_add_rfm1(choralroot AUTHORS "Quixotic7" METADATA skins/choralroot/RemoteFM1Metadata.json WANTS cpu2 rd1 tk1 ep1
             TAKEOVER_NAME ChoralRoot TAKEOVER_CLAIMS screen pads steps knobs jog buttons)
# the ctest rfm1_ep_choralroot (tests/cr_ep_test.c: the whole unit natively with cr_ep.c): an ep1 SET leaves the part
# byte for byte as the editor's knob does, for every parameter of every engine of both layers; the ids frozen; the
# audio bit-identical with the values read after every frame. Its --gen prints cr_ep_ids.h with new cells appended
add_executable(cr_ep_test ${CMAKE_CURRENT_LIST_DIR}/tests/cr_ep_test.c ${CMAKE_CURRENT_LIST_DIR}/core_choralroot_desc.c
               ${FM1_COMMON_SOURCES})
get_target_property(_cr_ep_inc core_choralroot_obj INCLUDE_DIRECTORIES)
get_target_property(_cr_ep_def core_choralroot_obj COMPILE_DEFINITIONS)
target_include_directories(cr_ep_test PRIVATE ${_cr_ep_inc} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(cr_ep_test PRIVATE ${_cr_ep_def} FM1_HOST_EP1=1)
target_compile_options(cr_ep_test PRIVATE -O1 -w -fno-common)
set_target_properties(cr_ep_test PROPERTIES C_STANDARD 11)
add_dependencies(cr_ep_test gen_choralroot)
if(MATH_LIB)
    target_link_libraries(cr_ep_test PRIVATE ${MATH_LIB})
endif()
add_test(NAME rfm1_ep_choralroot COMMAND cr_ep_test)
