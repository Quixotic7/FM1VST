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

# the .rfm1: a Fantasy CFW reading /audio, writing its own /data (later: exports), 128 MiB (the sample store's arena,
# src/io/ec_samplestore.h). The test boots a saved project (test_project.flash: four tracks on the folder's test loop
# -- loop with p-locks and a region lock, granular, slice poly, chopped with a ratchet -- built through panel input by
# the repo's host/glue_project.txt) and presses PLAY at 1.5 s: the sequencer must fire trigs and the mix be audible
fm1_add_rfm1(elasticat VIRTUAL NEEDS fs1 FOLDERS audio=r data=rw MEMORY_MIB 128 AUTHORS "Quixotic7"
             RUN_ARGS --folder ${CMAKE_CURRENT_LIST_DIR}/test_folder
                      --flash-in ${CMAKE_CURRENT_LIST_DIR}/test_project.flash --settle 3000 --seconds 4
                      --press PLAY@1500 --keys 26 --min-rms 0.02 --expect-ui ec0.3.0 --expect-ui play:1
                      --expect-ui file:testloop_bpm120.wav --expect-ui ready:1 --expect-ui missing:0
                      --expect-ui seq:on)
