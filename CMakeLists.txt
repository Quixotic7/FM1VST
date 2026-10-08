# SPDX-License-Identifier: GPL-3.0-only
# FM1VST: the FM-1 emulator from ChoralRoot FM-1 as a plugin (see FM1-VST-PLAN.md).
# Phase 1: the ChoralRoot core as a loadable module, the engine (core loader, device clock, resampler), replay tests.
# Phase 2: the JUCE plugin (plugin/): AU, VST3, Standalone. Phase 3: the panel GUI (plugin/PanelComponent, Theme).
cmake_minimum_required(VERSION 3.22)
project(FM1VST VERSION 0.2.0 LANGUAGES C CXX)

# ---- the cores: fm1_add_core() per firmware (a fourth core is one more call and its core-glue/<id>/ folder)
#
# fm1_add_core(<id> <glue_dir>
#     SOURCE <dir>            the firmware's submodule (cores/<name>, unmodified)
#     UNIT <file.c>           the glue unit (glue_dir relative): the firmware unit + exit / fopen glue + the map
#     DESC <file.c>           the descriptor (its own translation unit, -Wall)
#     PREFIX <XX>             the glue's macro prefix: FM1CORE_<XX>_VERSION / _COMMIT reach the descriptor
#     GEN <headers..>         what <dir>/tools/build.py generate() writes into <dir>/build/gen (gitignored there)
#     VERSION_FILE <file>     (dir relative), VERSION_REGEX and [VERSION_GROUP n] (default 1): the firmware's
#                             version string, the regex's group n on the file's first matching line
#     [INCLUDES <dirs..>] [DEFINES <defs..>])
# makes: gen_<id> (the generate step), core_<id>_obj (OBJECT: the unit + descriptor, -O2, hidden, PIC),
# core_<id> (STATIC, for tests that link a core directly) and <id>_core (MODULE: build/cores/<id>.fm1core, exporting
# only fm1core_get), and appends <id> to FM1_CORE_IDS (the bundles and the per-core tests take them from there).
# The generators need Pillow (and fontTools): the first python3 on PATH that has it (FindPython3 may pick another);
# override with -DCR_PYTHON=/path/to/python3
set(CR_PYTHON "" CACHE FILEPATH "python3 with Pillow, for the cores' generate steps")
if(NOT CR_PYTHON)
    execute_process(COMMAND /bin/sh -c "which -a python3"
                    OUTPUT_VARIABLE _py_list OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" _py_list "${_py_list}")
    foreach(_py IN LISTS _py_list)
        execute_process(COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
                        ${_py} -c "import PIL, fontTools" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(CR_PYTHON ${_py} CACHE FILEPATH "python3 with Pillow, for the cores' generate steps" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT CR_PYTHON)
    message(FATAL_ERROR "no python3 with Pillow and fontTools found (pip install Pillow fonttools, or -DCR_PYTHON=...)")
endif()
message(STATUS "cores: generate steps with ${CR_PYTHON}")
find_library(MATH_LIB m)
set(FM1_CORE_IDS "")

function(fm1_add_core id glue)
    cmake_parse_arguments(A "" "SOURCE;UNIT;DESC;PREFIX;VERSION_FILE;VERSION_REGEX;VERSION_GROUP" "GEN;INCLUDES;DEFINES"
                          ${ARGN})
    if(NOT A_VERSION_GROUP)
        set(A_VERSION_GROUP 1)
    endif()
    set(src ${A_SOURCE})
    if(NOT EXISTS ${src}/tools/build.py)
        message(FATAL_ERROR "${src} is empty: run git submodule update --init --recursive")
    endif()
    # the generated headers: tools/build.py generate() only (its main() would build the firmware with the JieLi
    # toolchain); it writes ${src}/build/gen, which is gitignored in the submodule
    set(gen ${src}/build/gen)
    list(TRANSFORM A_GEN PREPEND ${gen}/ OUTPUT_VARIABLE gen_headers)
    file(GLOB gen_scripts CONFIGURE_DEPENDS ${src}/tools/*.py)
    add_custom_command(
        OUTPUT ${gen_headers}
        COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib PYTHONPATH=${src}/tools
                ${CR_PYTHON} -c "import build; build.generate()"
        WORKING_DIRECTORY ${src}
        DEPENDS ${gen_scripts}
        COMMENT "${id}: generating build/gen headers (tools/build.py generate)"
        VERBATIM)
    add_custom_target(gen_${id} DEPENDS ${gen_headers})
    # the version (the firmware's own string) and the commit (the submodule's HEAD)
    set(ver "dev")
    file(STRINGS ${src}/${A_VERSION_FILE} _v REGEX "${A_VERSION_REGEX}")
    if(_v)
        list(GET _v 0 _v)
        string(REGEX MATCH "${A_VERSION_REGEX}" _m "${_v}")
        if(_m)
            set(ver "${CMAKE_MATCH_${A_VERSION_GROUP}}")
        endif()
    endif()
    execute_process(COMMAND git -C ${src} rev-parse HEAD OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT commit)
        set(commit "unknown")
    endif()
    message(STATUS "${id} core: version ${ver}, commit ${commit}")

    set(obj core_${id}_obj)
    add_library(${obj} OBJECT ${glue}/${A_UNIT} ${glue}/${A_DESC})
    target_include_directories(${obj} PRIVATE ${gen} ${src}/firmware/src ${A_INCLUDES}
                               ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
    target_compile_options(${obj} PRIVATE -O2 -fvisibility=hidden)
    target_compile_definitions(${obj} PRIVATE FM1CORE_${A_PREFIX}_VERSION="${ver}" FM1CORE_${A_PREFIX}_COMMIT="${commit}"
                               ${A_DEFINES})
    set_target_properties(${obj} PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
    add_dependencies(${obj} gen_${id})
    # the firmware's own code: warnings off (as its host builds); the generated headers are included by the unit:
    # rebuild it when they change
    set_source_files_properties(${glue}/${A_UNIT} PROPERTIES COMPILE_OPTIONS "-w" OBJECT_DEPENDS "${gen_headers}")
    set_source_files_properties(${glue}/${A_DESC} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

    add_library(core_${id} STATIC $<TARGET_OBJECTS:${obj}>)
    target_include_directories(core_${id} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
    add_library(${id}_core MODULE $<TARGET_OBJECTS:${obj}>)
    set_target_properties(${id}_core PROPERTIES PREFIX "" SUFFIX ".fm1core" OUTPUT_NAME ${id}
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cores)
    target_link_options(${id}_core PRIVATE -Wl,-exported_symbol,_fm1core_get)
    if(MATH_LIB)
        target_link_libraries(core_${id} PUBLIC ${MATH_LIB})
        target_link_libraries(${id}_core PRIVATE ${MATH_LIB})
    endif()
    set(FM1_CORE_IDS ${FM1_CORE_IDS} ${id} PARENT_SCOPE)
endfunction()

# ChoralRoot (https://github.com/Quixotic7/ChoralRootFM1): its own emulator's unit, tools/emu/emu_fw.c
set(CR ${CMAKE_CURRENT_SOURCE_DIR}/cores/ChoralRootFM1)
set(CR_GEN_NAMES ui_fonts.h cr_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h
                 melodee_cz1.h felucca_samples.h)
set(CR_GEN_HEADERS ${CR_GEN_NAMES})
list(TRANSFORM CR_GEN_HEADERS PREPEND ${CR}/build/gen/)
# the Tier 2 parameter map (core-glue/choralroot/core_choralroot_params.c): the sound editor's pages get host slots
# of their own too (30 more; off, they are FM1P_HIDDEN: the knobs reach them in the editor). The plugin then needs
# -DFM1_TIER2_SLOTS=103 or more
option(FM1_EXPOSE_EDITOR "ChoralRoot's Tier 2 map: also slots for the sound editor's ENV / LFO / MOD / MIX pages" OFF)
set(_cr_defs "")
if(FM1_EXPOSE_EDITOR)
    set(_cr_defs FM1_EXPOSE_EDITOR=1)
endif()
# the version: the firmware's own default FELUCCA_VERSION (cr_ui.c "ChoralRoot X"; the emulator unit itself shows
# "EMU")
fm1_add_core(choralroot core-glue/choralroot SOURCE ${CR} UNIT core_choralroot.c DESC core_choralroot_desc.c PREFIX CR
             GEN ${CR_GEN_NAMES} VERSION_FILE firmware/src/cr_ui.c
             VERSION_REGEX "^#define FELUCCA_VERSION \"(ChoralRoot )?([^\"]*)\"" VERSION_GROUP 2
             INCLUDES ${CR}/tools/emu DEFINES ${_cr_defs})
# Felucca (https://github.com/hugelton/Felucca): core-glue/felucca/ is its emulator (felucca_fw.c, felucca_hal.h,
# felucca_firmware.h) over the unmodified sources
fm1_add_core(felucca core-glue/felucca SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/cores/Felucca UNIT core_felucca.c
             DESC core_felucca_desc.c PREFIX FE
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\"")
# Melodee (https://github.com/keremimo/melodee, Felucca's fork with FM6, CZ-1 and USB audio): core-glue/melodee/
fm1_add_core(melodee core-glue/melodee SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/cores/melodee UNIT core_melodee.c
             DESC core_melodee_desc.c PREFIX ME
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h melodee_tables.h melodee_fm6.h melodee_cz1.h
             VERSION_FILE firmware/src/melodee.c VERSION_REGEX "^#define MELODEE_VERSION \"v?([^\"]*)\"")

# ---- the engine (C++17, no JUCE): core loading, the device clock, the resampler
add_library(fm1engine STATIC engine/cores.cpp engine/device.cpp)
target_include_directories(fm1engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
target_compile_features(fm1engine PUBLIC cxx_std_17)
target_compile_options(fm1engine PRIVATE -Wall -Wextra)
target_link_libraries(fm1engine PUBLIC ${CMAKE_DL_LIBS})

# ---- the plugin (phase 2): JUCE via FetchContent, AU + VST3 + Standalone
include(FetchContent)
set(FM1_JUCE_TAG 8.0.15)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${FM1_JUCE_TAG}
                     GIT_SHALLOW TRUE GIT_PROGRESS FALSE)
FetchContent_MakeAvailable(JUCE)

set(FM1_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
set(FM1_AU_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
# COPY_PLUGIN_AFTER_BUILD is done by juce_enable_copy_plugin_step below, AFTER the cores are put into each bundle and
# the bundle is signed (juce_add_plugin's own TRUE would add the copy step before ours)
juce_add_plugin(FM1VST
    COMPANY_NAME "Quixotic7"
    BUNDLE_ID com.quixotic7.fm1vst
    PLUGIN_MANUFACTURER_CODE Qx7u
    PLUGIN_CODE Fm1v
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "FM1VST"
    DESCRIPTION "The FM-1 emulator (ChoralRoot and other Felucca-family firmwares)"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_COPY_DIR "${FM1_VST3_DIR}"
    AU_COPY_DIR "${FM1_AU_DIR}")
target_sources(FM1VST PRIVATE plugin/Processor.cpp plugin/Tier2Parameter.cpp plugin/Editor.cpp plugin/Theme.cpp
                              plugin/PanelComponent.cpp)
# the theme presets (themes/presets.json), embedded: BinaryData::presets_json
juce_add_binary_data(FM1VST_Themes HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES themes/presets.json)
set_target_properties(FM1VST_Themes PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(FM1VST PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_compile_features(FM1VST PUBLIC cxx_std_17)
# the Tier 2 host parameter slots (plugin/Tier2Parameter.h): 0 by default, the host sees the 8 knobs and the 41 panel
# booleans (49), so Live's Configure collects nothing but the knobs. The opt-in for direct mappings of the firmware's
# own parameters: -DFM1_TIER2_SLOTS=79 (the 8 knobs + 79 + 41 = Live's 128)
set(FM1_TIER2_SLOTS 0 CACHE STRING "Tier 2 host parameter slots (the firmware's parameters; 0: none, 79: the opt-in)")
target_compile_definitions(FM1VST PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    FM1_CORES_DIR="${CMAKE_BINARY_DIR}/cores" FM1_TIER2_SLOTS=${FM1_TIER2_SLOTS})
target_link_libraries(FM1VST
    PRIVATE juce::juce_audio_utils fm1engine FM1VST_Themes
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
set(FM1_CORE_TARGETS ${FM1_CORE_IDS})
list(TRANSFORM FM1_CORE_TARGETS APPEND _core)
add_dependencies(FM1VST ${FM1_CORE_TARGETS})

# each format's bundle gets every built core in Contents/Resources/cores, each module and the bundle ad-hoc signed,
# then is copied to ~/Library/Audio/Plug-Ins (VST3, Components)
foreach(_fmt VST3 AU Standalone)
    set(_t FM1VST_${_fmt})
    set(_dst "$<TARGET_BUNDLE_DIR:${_t}>/Contents/Resources/cores")
    set(_cmds COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}")
    foreach(_c IN LISTS FM1_CORE_TARGETS)
        list(APPEND _cmds COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${_c}>" "${_dst}/"
                          COMMAND codesign --force -s - "${_dst}/$<TARGET_FILE_NAME:${_c}>")
    endforeach()
    add_dependencies(${_t} ${FM1_CORE_TARGETS})
    add_custom_command(TARGET ${_t} POST_BUILD ${_cmds}
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:${_t}>"
        COMMENT "FM1VST ${_fmt}: the cores (${FM1_CORE_IDS}) into the bundle, ad-hoc signed"
        VERBATIM)
endforeach()
juce_enable_copy_plugin_step(FM1VST)

# ---- the reference emulator (build/host/emu inside the submodule, gitignored there): the replay test's ground
# truth. Built by the submodule's own tools/emu/build.sh (needs SDL2 from Homebrew) when missing.
option(FM1_REFERENCE_TESTS "build the reference emulator and run the replay tests against it" ON)
set(CR_EMU ${CR}/build/host/emu)
if(FM1_REFERENCE_TESTS AND NOT EXISTS /opt/homebrew/bin/sdl2-config AND NOT EXISTS ${CR_EMU})
    message(WARNING "SDL2 (Homebrew) not found: the reference emulator cannot be built; replay_test is skipped "
                    "(brew install sdl2, or -DFM1_REFERENCE_TESTS=OFF)")
    set(FM1_REFERENCE_TESTS OFF)
endif()
if(FM1_REFERENCE_TESTS)
    file(GLOB CR_EMU_SOURCES CONFIGURE_DEPENDS ${CR}/tools/emu/*.c ${CR}/tools/emu/*.h)
    add_custom_command(
        OUTPUT ${CR_EMU}
        COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib sh ${CR}/tools/emu/build.sh
        DEPENDS ${CR_GEN_HEADERS} ${CR_EMU_SOURCES}
        COMMENT "ChoralRoot: building the reference emulator (tools/emu/build.sh -> build/host/emu)"
        VERBATIM)
    add_custom_target(reference_emu ALL DEPENDS ${CR_EMU})
endif()

# ---- tests
enable_testing()
# every core: boots, draws, sounds (core_smoke_<id>), and its parameter map (param_test_<id>), on the loaded module
add_executable(core_smoke tests/core_smoke.c)
target_include_directories(core_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
target_link_libraries(core_smoke PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(core_smoke ${FM1_CORE_TARGETS})
foreach(_id IN LISTS FM1_CORE_IDS)
    add_test(NAME core_smoke_${_id} COMMAND core_smoke $<TARGET_FILE:${_id}_core> ${CMAKE_BINARY_DIR}/cores/${_id}.ppm)
endforeach()

add_library(fm1testlib STATIC tests/script_runner.cpp)
target_link_libraries(fm1testlib PUBLIC fm1engine)
target_include_directories(fm1testlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_options(fm1testlib PRIVATE -Wall -Wextra)
target_compile_definitions(fm1testlib PUBLIC
    FM1_CR_DIR="${CR}" FM1_CORE_MODULE="$<TARGET_FILE:choralroot_core>" FM1_REF_EMU="${CR_EMU}"
    FM1_CORES_BUILD_DIR="${CMAKE_BINARY_DIR}/cores"
    FM1_REF_OUT="${CMAKE_BINARY_DIR}/reference")

add_executable(double_load_test tests/double_load_test.cpp)
target_link_libraries(double_load_test PRIVATE fm1testlib)
add_dependencies(double_load_test ${FM1_CORE_TARGETS})
add_test(NAME double_load_test COMMAND double_load_test)
set_tests_properties(double_load_test PROPERTIES RESOURCE_LOCK fm1_reference)

add_executable(halt_test tests/halt_test.cpp)
target_link_libraries(halt_test PRIVATE fm1testlib)
add_dependencies(halt_test ${FM1_CORE_TARGETS})
add_test(NAME halt_test COMMAND halt_test)

add_executable(param_test tests/param_test.cpp)
target_link_libraries(param_test PRIVATE fm1testlib)
target_compile_definitions(param_test PRIVATE FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/param_test_home")
add_dependencies(param_test ${FM1_CORE_TARGETS})
foreach(_id IN LISTS FM1_CORE_IDS)
    add_test(NAME param_test_${_id} COMMAND param_test $<TARGET_FILE:${_id}_core>)
endforeach()

if(FM1_REFERENCE_TESTS)
    add_executable(replay_test tests/replay_test.cpp)
    target_link_libraries(replay_test PRIVATE fm1testlib)
    add_dependencies(replay_test choralroot_core reference_emu)
    add_test(NAME replay_test COMMAND replay_test)
    set_tests_properties(replay_test PROPERTIES RESOURCE_LOCK fm1_reference)
endif()

# the plugin's processor, headless, on the plugin's shared code (FM1VST: a static library). The JUCE modules are the
# library's PRIVATE sources: their include paths and definitions are taken from it, not linked in a second time.
add_executable(plugin_test tests/plugin_test.cpp)
target_link_libraries(plugin_test PRIVATE FM1VST fm1engine)
target_include_directories(plugin_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(plugin_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS>
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/plugin_test_home"
    FM1_INSTALLED_VST3="${FM1_VST3_DIR}/FM1VST.vst3" FM1_INSTALLED_AU="${FM1_AU_DIR}/FM1VST.component")
add_dependencies(plugin_test ${FM1_CORE_TARGETS} FM1VST_VST3 FM1VST_AU)
add_test(NAME plugin_test COMMAND plugin_test)

# the Tier 2 parameters on the plugin's processor (headless, as plugin_test)
add_executable(tier2_test tests/tier2_test.cpp)
target_link_libraries(tier2_test PRIVATE FM1VST fm1engine)
target_include_directories(tier2_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(tier2_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS>
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/tier2_test_home")
add_dependencies(tier2_test ${FM1_CORE_TARGETS})
add_test(NAME tier2_test COMMAND tier2_test)
set_tests_properties(tier2_test PROPERTIES SKIP_RETURN_CODE 77)   # (FM1_TIER2_SLOTS 0: nothing to test)

# the eight physical knobs as host parameters, following the firmware's screens (headless, as tier2_test)
add_executable(knob_test tests/knob_test.cpp)
target_link_libraries(knob_test PRIVATE FM1VST fm1engine)
target_include_directories(knob_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(knob_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS>
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/knob_test_home")
add_dependencies(knob_test ${FM1_CORE_TARGETS})
add_test(NAME knob_test COMMAND knob_test)

# the panel GUI, headless (phase 3): the panel rendered offscreen to build/panel/*.png for every theme preset, the LCD
# region against the device's framebuffer, a key's LED, the contrast floors, the custom theme round trip
add_executable(panel_render_test tests/panel_render_test.cpp)
target_link_libraries(panel_render_test PRIVATE FM1VST fm1engine)
target_include_directories(panel_render_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(panel_render_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS>
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/panel_test_home" FM1_PANEL_OUT="${CMAKE_BINARY_DIR}/panel")
add_dependencies(panel_render_test ${FM1_CORE_TARGETS})
add_test(NAME panel_render_test COMMAND panel_render_test)
