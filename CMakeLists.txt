# SPDX-License-Identifier: GPL-3.0-only
# FM1VST: the FM-1 emulator from ChoralRoot FM-1 as a plugin (see FM1-VST-PLAN.md).
# Phase 1: the ChoralRoot core as a loadable module, the engine (core loader, device clock, resampler), replay tests.
# Phase 2: the JUCE plugin (plugin/): AU, VST3, Standalone. Phase 3: the panel GUI (plugin/PanelComponent, Theme).
cmake_minimum_required(VERSION 3.22)
project(FM1VST VERSION 0.4.0 LANGUAGES C CXX)

# ---- the cores: one fm1_add_core() per firmware (cmake/FM1Cores.cmake documents it), each in its glue folder:
# core-glue/<family>/core.cmake (or core_<id>.cmake for a family of several), picked up by the glob below, so a new
# core is only new files (core-glue/common/README.md "Adding a core").
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
# public builds leave out the cores whose licensing is not settled (fm1_add_core GATED; CORES-PLAN.md 6)
option(FM1_PUBLIC_BUILD "leave out the cores marked GATED (a build to publish)" OFF)
include(cmake/FM1Cores.cmake)
# (a core built otherwise than by fm1_add_core, e.g. core-glue/fm1emu's Rust module, appends its id, its build target
# and its module file to these: it is bundled and listed with the others, but not run by the per-core tests)
set(FM1_EXTRA_CORE_IDS "")
set(FM1_EXTRA_CORE_TARGETS "")
set(FM1_EXTRA_CORE_FILES "")
file(GLOB FM1_CORE_CMAKES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core-glue/*/core.cmake
                                            ${CMAKE_CURRENT_SOURCE_DIR}/core-glue/*/core_*.cmake)
list(SORT FM1_CORE_CMAKES)
foreach(_f IN LISTS FM1_CORE_CMAKES)
    include(${_f})
endforeach()
message(STATUS "cores: ${FM1_CORE_IDS}; built otherwise: ${FM1_EXTRA_CORE_IDS}")
# ChoralRoot's tree: the reference emulator and the replay tests below are its own. They need the choralroot core,
# which a public build leaves out (gated): then they are not built, and the tests that need it skip (77, saying so).
set(CR ${CMAKE_CURRENT_SOURCE_DIR}/cores/ChoralRootFM1)
set(FM1_HAVE_CR OFF)
if(choralroot IN_LIST FM1_CORE_IDS)
    set(FM1_HAVE_CR ON)
endif()
# (its generate step's outputs: the GEN list of core-glue/choralroot/core_choralroot.cmake)
set(CR_GEN_NAMES ui_fonts.h cr_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h cr_layouts.h
                 felucca_fm6.h melodee_cz1.h melodee_prophet_factory.h felucca_samples.h)
set(CR_GEN_HEADERS ${CR_GEN_NAMES})
list(TRANSFORM CR_GEN_HEADERS PREPEND ${CR}/build/gen/)
# the cores this build makes, for the tests (tests/built_cores.h: the bundle holds exactly these), and the plugin's
# default firmware: ChoralRoot, else (a public build) Felucca, else the first built
string(REPLACE ";" "," FM1_BUILT_CORES "${FM1_CORE_IDS};${FM1_EXTRA_CORE_IDS}")
string(REGEX REPLACE ",+$" "" FM1_BUILT_CORES "${FM1_BUILT_CORES}")
if(FM1_HAVE_CR)
    set(FM1_DEFAULT_CORE choralroot)
elseif(felucca IN_LIST FM1_CORE_IDS)
    set(FM1_DEFAULT_CORE felucca)
else()
    list(GET FM1_CORE_IDS 0 FM1_DEFAULT_CORE)
endif()
message(STATUS "cores: the plugin's default firmware: ${FM1_DEFAULT_CORE}")

# ---- the engine (C++17, no JUCE): core loading, the device clock, the resampler
add_library(fm1engine STATIC engine/cores.cpp engine/device.cpp)
target_include_directories(fm1engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine ${CMAKE_CURRENT_SOURCE_DIR}/core-api)
target_compile_features(fm1engine PUBLIC cxx_std_17)
target_compile_options(fm1engine PRIVATE -Wall -Wextra)
target_link_libraries(fm1engine PUBLIC ${CMAKE_DL_LIBS})

# ---- Remote FM-1 (RemoteFM1 M3, plugin/RemoteLink): libremotefm1 from the RemoteFM1 submodule (remote/RemoteFM1,
# unmodified), its portable sources (host/src/*.c) and its macOS back ends (host/src/macos/*.c: CoreMIDI, libusb)
# compiled here into one static library. (Not add_subdirectory(host): that CMake builds fm1remote and its tests.)
# libusb is linked STATICALLY (Homebrew's libusb-1.0.a), so the bundles depend on no Homebrew dylib. Without it the
# vendor bulk transport is left out (RFM_NO_LIBUSB) and frames go over SysEx; the editor says so.
set(RFM ${CMAKE_CURRENT_SOURCE_DIR}/remote/RemoteFM1)
if(NOT EXISTS ${RFM}/host/src/session.c)
    message(FATAL_ERROR "${RFM} is empty: run git submodule update --init --recursive")
endif()
file(GLOB RFM_SOURCES ${RFM}/host/src/*.c)
# The CoreMIDI back end is FM1VST's own, plugin/RemoteMidi.c (RemoteFM1's coremidi.c with discovery limited to what can
# be an FM-1: USB IDs 1209:0001, else an "FM-1" name; plugin/RemoteCandidates.h), so HELLO never reaches other MIDI
# hardware. host.c (the glue) and usb.c (libusb: only 1209:0001 with a vendor-class interface) are RemoteFM1's.
file(GLOB RFM_MAC_SOURCES ${RFM}/host/src/macos/*.c)
list(FILTER RFM_MAC_SOURCES EXCLUDE REGEX "/coremidi\\.c$")
add_library(remotefm1 STATIC ${RFM_SOURCES} ${RFM_MAC_SOURCES} plugin/RemoteMidi.c)
target_include_directories(remotefm1 PUBLIC ${RFM}/host/include ${RFM}/proto ${CMAKE_CURRENT_SOURCE_DIR}/plugin
                           PRIVATE ${RFM}/firmware/src ${RFM}/host/src/macos)
set_target_properties(remotefm1 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_compile_options(remotefm1 PRIVATE -O2 -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-parameter)
target_link_libraries(remotefm1 PUBLIC "-framework CoreMIDI" "-framework CoreFoundation" pthread)
# (-DFM1_LIBUSB_PREFIX=<dir>: a libusb built elsewhere, searched first; the release build's own, made for its
# deployment target: tools/libusb_static.sh)
set(FM1_LIBUSB_PREFIX "" CACHE PATH "a libusb install prefix searched before Homebrew's (include/libusb-1.0, lib)")
find_path(FM1_LIBUSB_INCLUDE libusb.h PATHS ${FM1_LIBUSB_PREFIX}/include/libusb-1.0 /opt/homebrew/include/libusb-1.0
          /usr/local/include/libusb-1.0 NO_DEFAULT_PATH)
find_library(FM1_LIBUSB_STATIC NAMES libusb-1.0.a PATHS ${FM1_LIBUSB_PREFIX}/lib /opt/homebrew/lib /usr/local/lib
             NO_DEFAULT_PATH)
if(FM1_LIBUSB_INCLUDE AND FM1_LIBUSB_STATIC)
    message(STATUS "Remote FM-1: vendor bulk with libusb, static (${FM1_LIBUSB_STATIC})")
    target_include_directories(remotefm1 PRIVATE ${FM1_LIBUSB_INCLUDE})
    target_link_libraries(remotefm1 PUBLIC ${FM1_LIBUSB_STATIC} "-framework IOKit" "-framework Security")
else()
    message(WARNING "Remote FM-1: libusb-1.0.a not found (brew install libusb): no vendor bulk, frames over SysEx only")
    target_compile_definitions(remotefm1 PRIVATE RFM_NO_LIBUSB=1 PUBLIC FM1_REMOTE_NO_BULK=1)
endif()

# ---- the plugin (phase 2): JUCE via FetchContent, AU + VST3 + Standalone
include(FetchContent)
set(FM1_JUCE_TAG 8.0.15)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${FM1_JUCE_TAG}
                     GIT_SHALLOW TRUE GIT_PROGRESS FALSE)
FetchContent_MakeAvailable(JUCE)

# the release manifest, build/release-manifest.tsv (tools/package_release.sh reads it), tab separated: this build's
# settings ("info" lines), each core it built (id, version, commit, licence, family, source tree) and each gated core
# it left out
set(_pub OFF)
if(FM1_PUBLIC_BUILD)
    set(_pub ON)
endif()
set(_m "info\tversion\t${PROJECT_VERSION}\ninfo\tpublic_build\t${_pub}\ninfo\tdefault_core\t${FM1_DEFAULT_CORE}\n")
string(APPEND _m "info\tdeployment_target\t${CMAKE_OSX_DEPLOYMENT_TARGET}\ninfo\tarchitectures\t${CMAKE_OSX_ARCHITECTURES}\n")
string(APPEND _m "info\tjuce_version\t${FM1_JUCE_TAG}\ninfo\tjuce_source\t${juce_SOURCE_DIR}\n")
string(APPEND _m "info\tlibusb\t${FM1_LIBUSB_STATIC}\n")
foreach(_id IN LISTS FM1_CORE_IDS FM1_EXTRA_CORE_IDS)
    foreach(_k VERSION COMMIT LICENSE FAMILY SOURCE)
        get_property(_v_${_k} GLOBAL PROPERTY FM1_CORE_${_k}_${_id})
    endforeach()
    string(APPEND _m "core\t${_id}\t${_v_VERSION}\t${_v_COMMIT}\t${_v_LICENSE}\t${_v_FAMILY}\t${_v_SOURCE}\n")
endforeach()
get_property(_left_out GLOBAL PROPERTY FM1_CORES_LEFT_OUT)
foreach(_id IN LISTS _left_out)
    string(APPEND _m "gated\t${_id}\n")
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/release-manifest.tsv "${_m}")

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
target_sources(FM1VST PRIVATE plugin/Processor.cpp plugin/Tier2Parameter.cpp plugin/HostRefresh.cpp plugin/Editor.cpp plugin/Theme.cpp
                              plugin/PanelComponent.cpp plugin/RemoteLink.cpp plugin/Look.cpp plugin/Widgets.cpp plugin/Sheet.cpp
                              plugin/StandaloneHooks.cpp plugin/Hub.cpp plugin/EmuClock.cpp plugin/EditorPort.cpp)
# the theme presets (themes/presets.json), embedded: BinaryData::presets_json; the Hub's fallback list
# (plugin/hub/fallback-catalog.json, the un-gated firmwares only: plugin/Hub.h), BinaryData::fallbackcatalog_json
juce_add_binary_data(FM1VST_Themes HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES themes/presets.json
                     plugin/hub/fallback-catalog.json)
set_target_properties(FM1VST_Themes PROPERTIES POSITION_INDEPENDENT_CODE ON)
# the editor's type (plugin/Look.cpp): Inter Tight (plugin/fonts, OFL 1.1), four static weights cut from the variable
# font at build time (JUCE cannot pick a variable font's weight; plugin/fonts/make_font.py, fontTools), embedded:
# FontData::InterTight<Weight>_ttf
set(FM1_FONT_DIR ${CMAKE_BINARY_DIR}/fonts)
set(FM1_FONT_FILES "")
foreach(_w Regular:400 Medium:500 SemiBold:600 Bold:700)
    string(REPLACE ":" ";" _wl ${_w})
    list(GET _wl 0 _name)
    list(GET _wl 1 _wght)
    set(_out ${FM1_FONT_DIR}/InterTight-${_name}.ttf)
    add_custom_command(OUTPUT ${_out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FM1_FONT_DIR}
        COMMAND ${CR_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/plugin/fonts/make_font.py
                ${CMAKE_CURRENT_SOURCE_DIR}/plugin/fonts/InterTight-VF.ttf ${_wght} ${_out}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin/fonts/InterTight-VF.ttf ${CMAKE_CURRENT_SOURCE_DIR}/plugin/fonts/make_font.py
        COMMENT "Inter Tight ${_name} (wght ${_wght})"
        VERBATIM)
    list(APPEND FM1_FONT_FILES ${_out})
endforeach()
juce_add_binary_data(FM1VST_Fonts HEADER_NAME FontData.h NAMESPACE FontData SOURCES ${FM1_FONT_FILES})
set_target_properties(FM1VST_Fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(FM1VST PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_compile_features(FM1VST PUBLIC cxx_std_17)
# the Tier 2 host parameter slots (plugin/Tier2Parameter.h): 0 by default, the host sees the 8 knobs and the 41 panel
# booleans (49), so Live's Configure collects nothing but the knobs. The opt-in for direct mappings of the firmware's
# own parameters: -DFM1_TIER2_SLOTS=79 (the 8 knobs + 79 + 41 = Live's 128)
set(FM1_TIER2_SLOTS 0 CACHE STRING "Tier 2 host parameter slots (the firmware's parameters; 0: none, 79: the opt-in)")
target_compile_definitions(FM1VST PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    FM1_CORES_DIR="${CMAKE_BINARY_DIR}/cores" FM1_TIER2_SLOTS=${FM1_TIER2_SLOTS}
    FM1_DEFAULT_CORE="${FM1_DEFAULT_CORE}")
target_link_libraries(FM1VST
    PRIVATE juce::juce_audio_utils juce::juce_cryptography fm1engine FM1VST_Themes FM1VST_Fonts remotefm1
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
set(FM1_CORE_TARGETS ${FM1_CORE_IDS})
list(TRANSFORM FM1_CORE_TARGETS APPEND _core)
add_dependencies(FM1VST ${FM1_CORE_TARGETS} ${FM1_EXTRA_CORE_TARGETS})

# each format's bundle gets every built core in Contents/Resources/cores, each module and the bundle ad-hoc signed,
# then is copied to ~/Library/Audio/Plug-Ins (VST3, Components)
foreach(_fmt VST3 AU Standalone)
    set(_t FM1VST_${_fmt})
    set(_dst "$<TARGET_BUNDLE_DIR:${_t}>/Contents/Resources/cores")
    # (emptied first: the bundle holds exactly this build's cores, never one a reconfigure left out)
    set(_cmds COMMAND ${CMAKE_COMMAND} -E remove_directory "${_dst}" COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}")
    foreach(_c IN LISTS FM1_CORE_TARGETS)
        list(APPEND _cmds COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${_c}>" "${_dst}/"
                          COMMAND codesign --force -s - "${_dst}/$<TARGET_FILE_NAME:${_c}>")
    endforeach()
    foreach(_f IN LISTS FM1_EXTRA_CORE_FILES)
        get_filename_component(_fn ${_f} NAME)
        list(APPEND _cmds COMMAND ${CMAKE_COMMAND} -E copy "${_f}" "${_dst}/" COMMAND codesign --force -s - "${_dst}/${_fn}")
        set_property(TARGET ${_t} APPEND PROPERTY LINK_DEPENDS ${_f})
    endforeach()
    add_dependencies(${_t} ${FM1_CORE_TARGETS} ${FM1_EXTRA_CORE_TARGETS})
    # a rebuilt core relinks the bundle, so this step (and the install step after it) runs again: the bundle never
    # keeps a stale core
    foreach(_id IN LISTS FM1_CORE_IDS)
        set_property(TARGET ${_t} APPEND PROPERTY LINK_DEPENDS ${CMAKE_BINARY_DIR}/cores/${_id}.fm1core)
    endforeach()
    # the licences of what the bundle carries beyond the cores: the editor's font
    set(_lic "$<TARGET_BUNDLE_DIR:${_t}>/Contents/Resources/licenses")
    list(APPEND _cmds COMMAND ${CMAKE_COMMAND} -E make_directory "${_lic}"
                      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/plugin/fonts/OFL.txt "${_lic}/InterTight-OFL.txt")
    add_custom_command(TARGET ${_t} POST_BUILD ${_cmds}
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:${_t}>"
        COMMENT "FM1VST ${_fmt}: the cores (${FM1_CORE_IDS} ${FM1_EXTRA_CORE_IDS}) into the bundle, ad-hoc signed"
        VERBATIM)
endforeach()
# the install: the VST3 and the AU copied to ~/Library/Audio/Plug-Ins after each build (the default). OFF: the bundles
# stay in the build (a second build tree next to a working install; plugin_test then checks the built bundles)
option(FM1_COPY_PLUGIN "copy the built VST3 / AU to ~/Library/Audio/Plug-Ins after each build" ON)
if(FM1_COPY_PLUGIN)
    juce_enable_copy_plugin_step(FM1VST)
    set(FM1_TEST_VST3 "${FM1_VST3_DIR}/FM1VST.vst3")
    set(FM1_TEST_AU "${FM1_AU_DIR}/FM1VST.component")
else()
    message(STATUS "FM1_COPY_PLUGIN OFF: the plugin is not installed into ~/Library/Audio/Plug-Ins")
    set(FM1_TEST_VST3 "$<TARGET_BUNDLE_DIR:FM1VST_VST3>")
    set(FM1_TEST_AU "$<TARGET_BUNDLE_DIR:FM1VST_AU>")
endif()

# ---- the reference emulator (build/host/emu inside the submodule, gitignored there): the replay test's ground
# truth. Built by the submodule's own tools/emu/build.sh (needs SDL2 from Homebrew) when missing.
option(FM1_REFERENCE_TESTS "build the reference emulator and run the replay tests against it" ON)
set(CR_EMU ${CR}/build/host/emu)
if(FM1_REFERENCE_TESTS AND NOT FM1_HAVE_CR)
    message(STATUS "no choralroot core in this build: no reference emulator, replay_test left out")
    set(FM1_REFERENCE_TESTS OFF)
endif()
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
    if(TARGET gen_choralroot)
        add_dependencies(reference_emu gen_choralroot)
    endif()
endif()

# ---- tests
enable_testing()
# the glue helpers (core-glue/common), on their own: the audio FIFO, the coroutine, the NOR / ticks / panel helpers
foreach(_t fifo coro nor)
    add_executable(common_${_t}_test tests/common/${_t}_test.c ${FM1_COMMON_SOURCES})
    target_include_directories(common_${_t}_test PRIVATE ${FM1_COMMON_DIR} ${FM1_CORE_API_DIR})
    target_compile_options(common_${_t}_test PRIVATE -Wall -Wextra)
    set_target_properties(common_${_t}_test PROPERTIES C_STANDARD 11)
    add_test(NAME common_${_t}_test COMMAND common_${_t}_test ${CMAKE_BINARY_DIR}/common_${_t}_test.bin)
endforeach()
# every core: boots, draws, sounds (core_smoke_<id>), and its parameter map (param_test_<id>), on the loaded module
# (each with its conformance file: the `sound` prelude, the settle time and the key; tests/conformance_conf.h)
function(fm1_conformance_file id out)
    get_property(_glue GLOBAL PROPERTY FM1_CORE_GLUE_${id})
    set(_conf ${_glue}/conformance_${id}.txt)
    if(NOT EXISTS ${_conf})
        set(_conf ${_glue}/conformance.txt)
    endif()
    set(${out} ${_conf} PARENT_SCOPE)
endfunction()

add_library(fm1testlib STATIC tests/script_runner.cpp)
target_link_libraries(fm1testlib PUBLIC fm1engine)
target_include_directories(fm1testlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests ${FM1_COMMON_DIR})   # (common/ppm.h)
target_compile_options(fm1testlib PRIVATE -Wall -Wextra)
target_compile_definitions(fm1testlib PUBLIC
    FM1_CR_DIR="${CR}" FM1_CORE_MODULE="$<$<BOOL:${FM1_HAVE_CR}>:$<TARGET_FILE:choralroot_core>>"
    FM1_REF_EMU="${CR_EMU}" FM1_CORES_BUILD_DIR="${CMAKE_BINARY_DIR}/cores" FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_REF_OUT="${CMAKE_BINARY_DIR}/reference")

add_executable(core_smoke tests/core_smoke.cpp)
target_link_libraries(core_smoke PRIVATE fm1testlib ${CMAKE_DL_LIBS})
target_compile_options(core_smoke PRIVATE -Wall -Wextra)
add_dependencies(core_smoke ${FM1_CORE_TARGETS})
foreach(_id IN LISTS FM1_CORE_IDS)
    fm1_conformance_file(${_id} _conf)
    add_test(NAME core_smoke_${_id}
             COMMAND core_smoke $<TARGET_FILE:${_id}_core> ${CMAKE_BINARY_DIR}/cores/${_id}.ppm ${_conf})
endforeach()

# the conformance test every core passes (tests/core_conformance.cpp: boots, draws, sounds, the flash round trip, a
# double load, a scripted reboot, the descriptor), each with its glue folder's conformance_<id>.txt or conformance.txt
add_executable(core_conformance tests/core_conformance.cpp)
target_link_libraries(core_conformance PRIVATE fm1testlib)
target_compile_options(core_conformance PRIVATE -Wall -Wextra)
add_dependencies(core_conformance ${FM1_CORE_TARGETS})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/conformance)
foreach(_id IN LISTS FM1_CORE_IDS)
    get_property(_fam GLOBAL PROPERTY FM1_CORE_FAMILY_${_id})
    get_property(_lic GLOBAL PROPERTY FM1_CORE_LICENSE_${_id})
    fm1_conformance_file(${_id} _conf)
    add_test(NAME core_conformance_${_id}
             COMMAND core_conformance $<TARGET_FILE:${_id}_core> ${_id} ${_fam} ${_lic} ${_conf}
                     ${CMAKE_BINARY_DIR}/conformance)
    set_tests_properties(core_conformance_${_id} PROPERTIES TIMEOUT 300
                         ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home")
endforeach()

# how heavy each core is (not a test: machine dependent): build/core_bench [SECONDS], every core with its conformance
# file, the real-time factor of 10 s rendered on the plugin's path (tests/core_bench.cpp)
add_executable(core_bench tests/core_bench.cpp)
target_link_libraries(core_bench PRIVATE fm1testlib)
target_compile_options(core_bench PRIVATE -Wall -Wextra)
add_dependencies(core_bench ${FM1_CORE_TARGETS})
set(_bench_args "")
foreach(_id IN LISTS FM1_CORE_IDS)
    fm1_conformance_file(${_id} _conf)
    list(APPEND _bench_args "$<TARGET_FILE:${_id}_core>=${_conf}")
endforeach()
add_custom_target(core_bench_run COMMAND core_bench 10 ${_bench_args} DEPENDS core_bench VERBATIM
                  COMMENT "the cores' real-time factors (10 s each)")

# the managers' SysEx on every core (tests/sysex_test.cpp: INFO, the M-UPGRADE identity, the backup's LIST / GET / PUT
# and the reboot from the flash image, the update keys; core-glue/common/sysex.h), on the plugin's path at 48 kHz
add_executable(sysex_test tests/sysex_test.cpp)
target_link_libraries(sysex_test PRIVATE fm1testlib)
target_compile_options(sysex_test PRIVATE -Wall -Wextra)
add_dependencies(sysex_test ${FM1_CORE_TARGETS})
foreach(_id IN LISTS FM1_CORE_IDS)
    add_test(NAME sysex_test_${_id} COMMAND sysex_test $<TARGET_FILE:${_id}_core> ${_id})
    set_tests_properties(sysex_test_${_id} PROPERTIES TIMEOUT 600
                         ENVIRONMENT "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home")
endforeach()

add_executable(double_load_test tests/double_load_test.cpp)
target_link_libraries(double_load_test PRIVATE fm1testlib)
add_dependencies(double_load_test ${FM1_CORE_TARGETS})
add_test(NAME double_load_test COMMAND double_load_test)
set_tests_properties(double_load_test PROPERTIES RESOURCE_LOCK fm1_reference SKIP_RETURN_CODE 77)

add_executable(halt_test tests/halt_test.cpp)
target_link_libraries(halt_test PRIVATE fm1testlib)
add_dependencies(halt_test ${FM1_CORE_TARGETS})
add_test(NAME halt_test COMMAND halt_test)
set_tests_properties(halt_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(param_test tests/param_test.cpp)
target_link_libraries(param_test PRIVATE fm1testlib)
target_compile_definitions(param_test PRIVATE FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/param_test_home")
add_dependencies(param_test ${FM1_CORE_TARGETS})
foreach(_id IN LISTS FM1_CORE_IDS)
    add_test(NAME param_test_${_id} COMMAND param_test $<TARGET_FILE:${_id}_core>)
    set_tests_properties(param_test_${_id} PROPERTIES SKIP_RETURN_CODE 77)   # (a core without a parameter map)
endforeach()

if(FM1_REFERENCE_TESTS)
    add_executable(replay_test tests/replay_test.cpp)
    target_link_libraries(replay_test PRIVATE fm1testlib)
    add_dependencies(replay_test ${FM1_CORE_TARGETS} reference_emu)
    add_test(NAME replay_test COMMAND replay_test)
    set_tests_properties(replay_test PROPERTIES RESOURCE_LOCK fm1_reference)
endif()

# the plugin's processor, headless, on the plugin's shared code (FM1VST: a static library). The JUCE modules are the
# library's PRIVATE sources: their include paths and definitions are taken from it, not linked in a second time.
add_executable(plugin_test tests/plugin_test.cpp)
target_link_libraries(plugin_test PRIVATE FM1VST fm1engine)
target_include_directories(plugin_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(plugin_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/plugin_test_home"
    FM1_INSTALLED_VST3="${FM1_TEST_VST3}" FM1_INSTALLED_AU="${FM1_TEST_AU}" FM1_PUBLIC_BUILD=$<BOOL:${FM1_PUBLIC_BUILD}>)
add_dependencies(plugin_test ${FM1_CORE_TARGETS} ${FM1_EXTRA_CORE_TARGETS} FM1VST_VST3 FM1VST_AU)
add_test(NAME plugin_test COMMAND plugin_test)
set_tests_properties(plugin_test PROPERTIES SKIP_RETURN_CODE 77)   # (a public build: the cores it needs left out)

# the Tier 2 parameters on the plugin's processor (headless, as plugin_test)
add_executable(tier2_test tests/tier2_test.cpp)
target_link_libraries(tier2_test PRIVATE FM1VST fm1engine)
target_include_directories(tier2_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(tier2_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/tier2_test_home")
add_dependencies(tier2_test ${FM1_CORE_TARGETS})
add_test(NAME tier2_test COMMAND tier2_test)
set_tests_properties(tier2_test PROPERTIES SKIP_RETURN_CODE 77)   # (FM1_TIER2_SLOTS 0: nothing to test; or a public build)

# the eight physical knobs as host parameters, following the firmware's screens (headless, as tier2_test)
add_executable(knob_test tests/knob_test.cpp)
target_link_libraries(knob_test PRIVATE FM1VST fm1engine)
target_include_directories(knob_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(knob_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/knob_test_home")
add_dependencies(knob_test ${FM1_CORE_TARGETS})
add_test(NAME knob_test COMMAND knob_test)
set_tests_properties(knob_test PROPERTIES SKIP_RETURN_CODE 77)   # (a public build: the cores it needs left out)

# the panel GUI, headless (phase 3): the panel rendered offscreen to build/panel/*.png for every theme preset, the LCD
# region against the device's framebuffer, a key's LED, the contrast floors, the custom theme round trip
add_executable(panel_render_test tests/panel_render_test.cpp)
target_link_libraries(panel_render_test PRIVATE FM1VST fm1engine)
target_include_directories(panel_render_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(panel_render_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/panel_test_home" FM1_PANEL_OUT="${CMAKE_BINARY_DIR}/panel")
add_dependencies(panel_render_test ${FM1_CORE_TARGETS})
add_test(NAME panel_render_test COMMAND panel_render_test)
set_tests_properties(panel_render_test PROPERTIES SKIP_RETURN_CODE 77)   # (a public build: the cores it needs left out)

# the editor around the panel, headless: the device and screen views, the corner button (clear of every control at
# every size), the settings sheet and its pages, right-click, the view state, written to build/panel/ui-*.png
add_executable(ui_render_test tests/ui_render_test.cpp)
target_link_libraries(ui_render_test PRIVATE FM1VST fm1engine)
target_include_directories(ui_render_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(ui_render_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/ui_test_home" FM1_PANEL_OUT="${CMAKE_BINARY_DIR}/panel"
    FM1_FELUCCA_FWSC="${FM1EMU_FELUCCA_FWSC}")
add_dependencies(ui_render_test ${FM1_CORE_TARGETS} ${FM1_EXTRA_CORE_TARGETS})
add_test(NAME ui_render_test COMMAND ui_render_test)
set_tests_properties(ui_render_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)   # (a public build: no choralroot)

# Remote FM-1 (plugin/RemoteLink) against libremotefm1's in-process fake unit (FM1EMU_REMOTE_FAKE=1: nothing on USB or
# CoreMIDI is opened): the processor with Remote on (frames, LEDs, keys / buttons / encoders / MASTER, a core switch's
# full resend, SESSION 0 at the toggle off), the unit lock between two links, the host MIDI filter
add_executable(remote_test tests/remote_test.cpp)
target_link_libraries(remote_test PRIVATE FM1VST fm1engine remotefm1)
target_include_directories(remote_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(remote_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/remote_test_home")
add_dependencies(remote_test ${FM1_CORE_TARGETS})
add_test(NAME remote_test COMMAND remote_test)
set_tests_properties(remote_test PROPERTIES SKIP_RETURN_CODE 77)   # (a public build: the cores it needs left out)
set_tests_properties(remote_test PROPERTIES ENVIRONMENT "FM1EMU_REMOTE_FAKE=1" TIMEOUT 180)

# "Show to editors" (plugin/EditorPort.h), headless, on a fake core that answers SysEx from its clock
# (tests/echo_core.c, a module of its own outside build/cores, so no other test lists it): editor_port_test runs the
# port faked (FM1EMU_EDITOR_FAKE=1: the data path, the host's MIDI untouched, the idle clock); editor_port_coremidi_test
# makes the real virtual ports and talks to them as a CoreMIDI client (FM-106's dump request, the echo core, two
# instances' names), and skips (77) where CoreMIDI or FM-106 is not there
add_library(echo_core MODULE tests/echo_core.c)
target_include_directories(echo_core PRIVATE ${FM1_CORE_API_DIR})
set_target_properties(echo_core PROPERTIES PREFIX "" SUFFIX ".fm1core" OUTPUT_NAME echo C_STANDARD 11
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_cores C_VISIBILITY_PRESET hidden)
add_executable(editor_port_test tests/editor_port_test.cpp)
target_link_libraries(editor_port_test PRIVATE FM1VST fm1engine "-framework CoreFoundation")
target_include_directories(editor_port_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
target_compile_definitions(editor_port_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
    FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/editor_port_test_home" FM1_ECHO_CORE="$<TARGET_FILE:echo_core>")
add_dependencies(editor_port_test ${FM1_CORE_TARGETS} echo_core)
add_test(NAME editor_port_test COMMAND editor_port_test)
add_test(NAME editor_port_coremidi_test COMMAND editor_port_test coremidi)
set_tests_properties(editor_port_test editor_port_coremidi_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
                     RESOURCE_LOCK fm1_editor_port)

# The Hub's firmwares on the fm1emu core (plugin/Hub.h), headless: a catalog fixture parses (which entries, the fallback
# list, no gated name in it), a package is downloaded from a file:// fixture Hub and checked (a SHA-256 or size mismatch
# refused), cached, picked and booted; the clock choice reaches the core; the state round trip (the package by id and
# SHA-256, the NOR only), and again offline from the cache; Auto's governor on fake timing (plugin/EmuClock.h)
if(TARGET fm1emu_core)
    add_executable(hub_test tests/hub_test.cpp)
    target_link_libraries(hub_test PRIVATE FM1VST fm1engine)
    target_include_directories(hub_test PRIVATE $<TARGET_PROPERTY:FM1VST,INCLUDE_DIRECTORIES>)
    target_compile_definitions(hub_test PRIVATE $<TARGET_PROPERTY:FM1VST,COMPILE_DEFINITIONS> FM1_BUILT_CORES="${FM1_BUILT_CORES}"
        FM1_SCRATCH_DIR="${CMAKE_BINARY_DIR}/hub_test_home" FM1_HUB_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hub"
        FM1_FALLBACK_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/plugin/hub/fallback-catalog.json")
    add_dependencies(hub_test ${FM1_CORE_TARGETS} ${FM1_EXTRA_CORE_TARGETS})
    # the gated cores' ids (no gated firmware is in the fallback list) and the package it boots
    get_property(_gated_all GLOBAL PROPERTY FM1_CORES_GATED_ALL)
    string(REPLACE ";" "," _gated_csv "${_gated_all}")
    # (registered only where the package is: a machine-local file, as fm1emu_test's; a CI build has none, and
    # tools/release_tests.sh allows no unexpected skip)
    if(EXISTS ${FM1EMU_FELUCCA_FWSC})
        add_test(NAME hub_test COMMAND hub_test ${FM1EMU_FELUCCA_FWSC} "${_gated_csv}")
        set_tests_properties(hub_test PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
    else()
        message(STATUS "hub_test: ${FM1EMU_FELUCCA_FWSC} not found: not registered")
    endif()
endif()
