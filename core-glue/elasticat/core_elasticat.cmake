# SPDX-License-Identifier: GPL-3.0-only
# Elasticat FM-1 (https://github.com/Quixotic7/elasticatfm1, GPL-3.0-only): Elasticat, the norns script by Quixotic7,
# rewritten in C as a Fantasy CFW (docs/VIRTUAL-CARTS.md): a host-only .rfm1 reading samples from the shared
# RemoteFM1 folder (/audio). Native C written against the ABI: the firmware implements fm1core_get itself
# (src/plat/ec_core.c), so this glue only lists its units. README.md here.
#
# The source: the submodule cores/elasticatfm1, or -DEC_SOURCE=/path/to/elasticatfm1 (a working tree, while the engine
# is being written: its src/dsp and src/seq join by the glob below).
set(EC_SOURCE ${CMAKE_SOURCE_DIR}/cores/elasticatfm1 CACHE PATH "Elasticat FM-1's tree (default: the submodule)")
set(_ec ${EC_SOURCE})
file(GLOB _ec_units CONFIGURE_DEPENDS ${_ec}/src/plat/*.c ${_ec}/src/io/*.c ${_ec}/src/ui/*.c ${_ec}/src/dsp/*.c
     ${_ec}/src/seq/*.c ${_ec}/src/app/*.c)
list(FILTER _ec_units EXCLUDE REGEX "/ec_fs_native\\.c$")   # (the repo's own native stand-in; here the host's API)
list(SORT _ec_units)
set(_ec_rel "")
foreach(_f IN LISTS _ec_units)
    file(RELATIVE_PATH _r ${CMAKE_CURRENT_LIST_DIR} ${_f})
    list(APPEND _ec_rel ${_r})
endforeach()
fm1_add_core(elasticat ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ec} UNIT core_elasticat.c DESC core_elasticat_desc.c
             EXTRA_SOURCES ${_ec_rel} ../common/rfm1_host.c
             FAMILY elasticat LICENSE GPL-3.0-only
             CREDITS "Elasticat by Quixotic7, ported from the norns script Elasticat (https://github.com/Quixotic7/Elasticat, GPL-3.0) to the FM-1. A Fantasy CFW: runs in RemoteFM1 hosts, its samples from your RemoteFM1 folder (audio/)."
             NO_GEN
             VERSION_FILE src/plat/ec_version.h VERSION_REGEX "EC_VERSION \"([^\"]+)\""
             FLAGS -ffp-contract=off
             INCLUDES ${_ec}/src/plat ${_ec}/src/io ${_ec}/src/ui ${_ec}/src/dsp ${_ec}/src/seq ${_ec}/src/app)
if(TARGET core_elasticat_obj)
    foreach(_r IN LISTS _ec_rel)       # (the same sound everywhere: no fused multiply-add)
        set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/${_r} APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
    endforeach()
endif()

# the .rfm1: a Fantasy CFW reading /audio, writing its own /data (later: exports), 256 MiB (0.6.0) (the sample store's arena,
# src/io/ec_samplestore.h). The test boots a saved project (test_project.flash: four tracks on the folder's test loop
# -- loop with p-locks and a region lock, granular, slice poly, chopped with a ratchet -- built through panel input by
# the repo's host/glue_project.txt) and presses PLAY at 1.5 s: the sequencer must fire trigs and the mix be audible
#
# E6 (0.4.0): audio input (docs/VIRTUAL-CARTS.md 8): WANTS ain1 (it runs without; the INPUT machine and the
# SAMPLER's input source are then silent). The test feeds the test loop through rfm1_audio_in (latency 256);
# rfm1_elasticat_ain_off turns the input off at 5 s (state 0).
# 0.6.0 (the modes, docs/UX-V2.md): MEMORY_MIB 256 (the sample arena 219 MiB); the old (0.5) project record loads
# (bank A, slots 1-16). FILE (printed SAVE) at 2 s while playing opens the SAMPLER (FILE itself is gated while
# playing); the chord is B11 (key 25), which arms a take on the next bar: it is recording (rec:rec) by the end. The
# take is written to /data only when it ends, after the run.
# 0.6.1 (docs/UX-V2.md 9/10): projects are files in /data/projects (the flash keeps APP SETTINGS and the last name); the
# test's old flash record still loads once; two inserts per track (the arena 200 MiB).
set(_ec_run --folder ${CMAKE_CURRENT_LIST_DIR}/test_folder --flash-in ${CMAKE_CURRENT_LIST_DIR}/test_project.flash
            --settle 3000 --seconds 4 --press PLAY@1500 --keys 25 --min-rms 0.02 --expect-ui ec0.6.10
            --expect-ui play:1 --expect-ui file:testloop_bpm120.wav --expect-ui ready:1 --expect-ui missing:0
            --expect-ui seq:on --audio-in ${CMAKE_CURRENT_LIST_DIR}/test_folder/audio/testloop_bpm120.wav
            --audio-in-latency 256)
fm1_add_rfm1(elasticat VIRTUAL NEEDS fs1 WANTS ain1 FOLDERS audio=r data=rw MEMORY_MIB 256 AUTHORS "Quixotic7"
             RUN_ARGS ${_ec_run} --press SAVE@2000 --expect-ui ain:1 --expect-ui lat:256
                      --expect-ui cat:SAMPLER --expect-ui rec:rec)
get_property(_ec_file GLOBAL PROPERTY FM1_RFM1_FILE_elasticat)
if(_ec_file)
    string(REPLACE ";" " " _ec_ra "${_ec_run};--audio-in-off-at;5000;--expect-ui;ain:0;--expect-ui;lat:0")
    add_test(NAME rfm1_elasticat_ain_off
             COMMAND ${CMAKE_COMMAND} -DPYTHON=${CR_PYTHON} -DCHECK=${FM1_RFM1_TOOLS}/rfm1_check.py
                     -DRUN=$<TARGET_FILE:rfm1_run> -DFILE=${_ec_file} "-DRUN_ARGS=${_ec_ra}"
                     -P ${FM1_RFM1_TOOLS}/rfm1/rfm1_test.cmake)
    set_tests_properties(rfm1_elasticat_ain_off PROPERTIES TIMEOUT 300)
endif()
