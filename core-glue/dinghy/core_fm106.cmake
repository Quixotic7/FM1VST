# SPDX-License-Identifier: GPL-3.0-only
# FM-106 Beta4 (https://github.com/PatttF/fm-106, tag Beta4): the Dinghy family glue (core-glue/dinghy/) over the
# unmodified submodule cores/fm-106. Its tests/host.c has fallen behind the app (19 errors at Beta4) and platform.c
# includes it by a relative path, so clang's -ivfsoverlay maps that path to fm106_host.c (the overlay file is
# written here, in the build tree). The headers in firmware/gen are committed (NO_GEN); the version is tools/build.py's
# APP_VER. The .pool section attribute (fm106_fx.h, gfx.c) Mach-O rejects becomes an alignment.
#
# GATED (CORES-PLAN.md 6): its engine is a port of Ultramaster KR-106 (GPL-3.0, compatible), but its 128 factory
# presets are the Roland Juno-106 factory patches decoded from SysEx (fm106_presets.h; KR-106 ships them from
# "Factory_Patches.pat" with no statement of their rights), and FM-106 carries no KR-106 copyright notice of its own.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_fm106 ${CMAKE_SOURCE_DIR}/cores/fm-106)
set(_fm106_vfs ${CMAKE_BINARY_DIR}/dinghy/fm106_vfs.yaml)
file(WRITE ${_fm106_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_fm106}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/fm106_host.c\" } ] }\n")
fm1_add_core(fm106 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fm106}
             UNIT core_fm106.c DESC core_fm106_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             GATED "the factory presets are Roland Juno-106 factory patch data of unverified rights (via KR-106), and FM-106 carries no KR-106 copyright notice"
             NO_GEN VERSION Beta4
             INCLUDES ${_fm106}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_fm106_vfs}")
if(TARGET core_fm106_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_fm106.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/fm106_host.c ${_fm106_vfs})
endif()
dinghy_core(fm106 core_fm106.c)
# the golden check's host sim: FM-106's own tests/host.c with the symbols the app has needed since (the survey's six
# lines), written into the build tree from the submodule's file and overlaid the same way
set(_fm106_golden_host ${CMAKE_BINARY_DIR}/dinghy/fm106_hostsim_host.c)
set(_fm106_golden_vfs ${CMAKE_BINARY_DIR}/dinghy/fm106_hostsim_vfs.yaml)
file(READ ${_fm106}/tests/host.c _h)
string(REPLACE "static struct { uint32_t overruns, underruns; } uac;"
       "static struct { uint32_t overruns, underruns, alt, starts, stalls, rate_sets, missed, flowing; } uac;\nstatic volatile uint32_t audio_lates;\nenum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };\nstatic int32_t fm1_adc_read(int c) { (void)c; return -1; }\nstatic uint32_t fm106_now_ms(void) { return fm1_ms; }\nstatic void str_cpy(char *d, const char *s, unsigned max) { unsigned i = 0; if (!max) return; while (s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }"
       _h "${_h}")
file(WRITE ${_fm106_golden_host}.tmp "${_h}")
configure_file(${_fm106_golden_host}.tmp ${_fm106_golden_host} COPYONLY)
file(WRITE ${_fm106_golden_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_fm106}/tests/host.c\", \"external-contents\": \"${_fm106_golden_host}\" } ] }\n")
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3000 ms
dinghy_golden_check(fm106 SCRIPT golden_fm106.txt AT 3000 SOURCE ${CMAKE_SOURCE_DIR}/cores/fm-106/firmware/src/fm106.c
                    DEFINES DINGHY_HOST APP_VER="Beta4"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/fm-106/firmware/src ${CMAKE_SOURCE_DIR}/cores/fm-106/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)" "-ivfsoverlay" "${_fm106_golden_vfs}")
