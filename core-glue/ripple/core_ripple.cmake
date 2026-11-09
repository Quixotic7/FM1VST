# SPDX-License-Identifier: GPL-3.0-only
# Ripple 0.6.1 (https://github.com/charlesvestal/fm1-ripple, tag v0.6.1 = a710e43; MPL-2.0, with Lua 5.4.7, Plaits /
# stmlib, the currents SDK and the MIDI effects MIT): the first Fantasy CFW (docs/VIRTUAL-CARTS.md section 7), a
# host-only .rfm1 whose droplets come from the shared RemoteFM1 folder. The fm1-base glue (core-glue/fm1base/README.md)
# over the unmodified submodule and its own submodules (base = fm1-base @ 2d8ed74, base/bsp = felucca-bsp @ 773b0b9),
# on Ripple's emulator path (PLAT_HOST, PLAT_WEB: tools/build.py --web's). README.md here.
#
# The units, as fm1-base's build.py --web compiles them:
#   core_ripple.c           the unity build (the platform, app/app.c and its parts), the glue
#   app/lua/lua_one.c       Lua and the droplet API (fm1.toml o2), app/midifx/mfx_one.c (the MIDI effects, o2)
#   the currents            every built-in current (currents/ and currents/extra, as fm1.toml's web gen line takes
#                           them): a C current or a Faust one (its C from currents/gen, ripple_gen.py) through a wrapper
#                           unit in the build folder (rc_entry renamed rc_entry_<name>, as sdk/tools/currents.py --web
#                           writes them), a C++ one (Plaits) compiled from the submodule with -Drc_entry=..; Plaits'
#                           and stmlib's own sources once, with the currents' ripple-cflags
#   ripple_lua_hook.c       the count hook (a droplet that runs too long is stopped)
#   ../common/rfm1_host.c   the host's file API natively (no folder yet); in the .rfm1 the rfm1_host_1 imports
# Two MPL-2.0 files are read through overlays written here into the build folder (a clang VFS overlay, as LOOM-8's
# glue does): app/app.c ("disk mode" is "reload folder") and app/lua/luaport.h (Lua's errors on Asyncify in the .rfm1,
# the count hook, LUAI_MAXSTACK). Each is the submodule's file with the lines below replaced or added, its MPL header
# kept; the configure fails if a line to replace is not there (a new pin: check the overlays).
#
# The generate step: fm1-base's build.py generate() (the fonts, the screen reader's words) without fm1.toml's "gen"
# lines, whose built-in volume needs the device's toolchain (the .cur files); ripple_gen.py instead makes the empty
# disk, the built-in volume (the droplets and a stand-in .cur per compiled-in current) and the Faust currents' C.
set(_rp ${CMAKE_SOURCE_DIR}/cores/fm1-ripple)
set(_rp_ver 0.6.1)
set(_rp_app ${_rp}/app)
set(_rp_gen ${_rp_app}/build/gen)
set(_rp_bin ${CMAKE_BINARY_DIR}/ripple)
# build.py --release: APP_VERSION the release in capitals; the package identity fm1.toml's id_prefix (FM-1_13) + the
# major, minor and patch as two digits each (0.4: FM-1_13000601)
string(TOUPPER "${_rp_ver}" _rp_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_rp_ver}")
set(_rp_id "FM-1_13")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _rp_id "0")
    endif()
    string(APPEND _rp_id "${_d}")
endforeach()

# ---- the overlays: the submodule's file, these lines changed (string(REPLACE), each checked), into the build folder
file(MAKE_DIRECTORY ${_rp_bin})
function(ripple_overlay src dst)          # ARGN: old new old new .. (each ";" written <SC>)
    file(READ ${src} _t)
    set(_args ${ARGN})
    list(LENGTH _args _n)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE 0 ${_last} 2)
        math(EXPR _j "${_i} + 1")
        list(GET _args ${_i} _old)
        list(GET _args ${_j} _new)
        string(REPLACE "<SC>" ";" _old "${_old}")   # (a ';' in an argument: <SC>, a CMake list separator else)
        string(REPLACE "<SC>" ";" _new "${_new}")
        string(FIND "${_t}" "${_old}" _at)
        if(_at LESS 0)
            message(FATAL_ERROR "ripple: ${src} has no \"${_old}\" any more: the overlay (core_ripple.cmake) needs a look")
        endif()
        string(REPLACE "${_old}" "${_new}" _t "${_t}")
    endforeach()
    set(_old "")
    if(EXISTS ${dst})
        file(READ ${dst} _old)
    endif()
    if(NOT _old STREQUAL _t)
        file(WRITE ${dst} "${_t}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
endfunction()
set(_rp_note " * Changed by FM1VST (core-glue/ripple/core_ripple.cmake, an overlay of this MPL-2.0 file for its host builds):")
ripple_overlay(${_rp_app}/app.c ${_rp_bin}/app.c
    " * Copyright (C) 2026 Charles Vestal */\n"
    " * Copyright (C) 2026 Charles Vestal\n${_rp_note}\n * the list's \"disk mode\" is \"reload folder\" (the droplets come from the RemoteFM1 folder: core_ripple.c), and\n * \"empty the disk\" reads the folder again after it. */\n"
    "i - ui.n == SYS_DISK ? \"disk mode\" :"
    "i - ui.n == SYS_DISK ? \"reload folder\" :"
    "else if (ui.sel - ui.n == SYS_DISK)\n                    disk_mode(1)<SC>"
    "else if (ui.sel - ui.n == SYS_DISK)\n                    rp_folder_reload()<SC>"
    "                    app_say(\"DISK\", \"EMPTIED\")<SC>\n"
    "                    app_say(\"DISK\", \"EMPTIED\")<SC>\n                    rp_folder_reload()<SC>\n")
ripple_overlay(${_rp_app}/lua/luaport.h ${_rp_bin}/luaport.h
    " * Copyright (C) 2026 Charles Vestal */\n"
    " * Copyright (C) 2026 Charles Vestal\n${_rp_note}\n * the block at its end: Lua's errors in an .rfm1, the Lua stack's limit, the count hook. */\n"
    "#define luai_jmpbuf lj_buf\n#endif\n\n#endif\n"
    "#define luai_jmpbuf lj_buf\n#endif\n
/* FM1VST's host builds (core-glue/ripple): the Lua stack's limit, 20,000 slots (stock 1,000,000 would never fit in
 * the heap, 1 MB: an overflow then says \"stack overflow\", not \"not enough memory\")<SC> every new state gets the count hook
 * (ripple_lua_hook.c: a callback that runs too long is stopped with an error, as the FM-1's watchdog did)<SC> in an
 * .rfm1, errors on Binaryen's Asyncify (core-glue/common/lj_asyncify.h: no setjmp, no wasm exceptions) */
#undef LUAI_MAXSTACK
#define LUAI_MAXSTACK 20000
void rp_lua_open(void *L)<SC>
#define luai_userstateopen(L) rp_lua_open(L)
#if defined FM1CORE_RFM1 && defined __wasm__
#include \"lj_asyncify.h\"
#endif

#endif
")
set(_rp_vfs ${_rp_bin}/ripple_vfs.yaml)
set(_v "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_rp_app}/app.c\", \"external-contents\": \"${_rp_bin}/app.c\" },\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_rp_app}/lua/luaport.h\", \"external-contents\": \"${_rp_bin}/luaport.h\" } ] }\n")
file(WRITE ${_rp_vfs}.in "${_v}")
configure_file(${_rp_vfs}.in ${_rp_vfs} COPYONLY)

# ---- the currents: fm1.toml's web gen line takes currents/ and currents/extra (C, Faust, C++)
file(GLOB _rp_cur_c ${_rp}/currents/*.c ${_rp}/currents/extra/*.c)
file(GLOB _rp_cur_dsp ${_rp}/currents/*.dsp ${_rp}/currents/extra/*.dsp)
file(GLOB _rp_cur_cc ${_rp}/currents/*.cc ${_rp}/currents/extra/*.cc)
list(SORT _rp_cur_c)
list(SORT _rp_cur_dsp)
list(SORT _rp_cur_cc)
file(GLOB_RECURSE _rp_plaits_lib ${_rp}/currents/plaits/plaits/*.cc ${_rp}/currents/plaits/stmlib/*.cc)
list(SORT _rp_plaits_lib)
file(RELATIVE_PATH _rp_rel_bin ${CMAKE_CURRENT_LIST_DIR} ${_rp_bin})
file(RELATIVE_PATH _rp_rel ${CMAKE_CURRENT_LIST_DIR} ${_rp})
set(_rp_reg "/* generated by core_ripple.cmake: the currents compiled in, RP_CUR(name, identifier) (core_ripple.c) */\n")
set(_rp_cur_units "")
set(_rp_cur_wrap_rel "")
foreach(_f IN LISTS _rp_cur_c _rp_cur_dsp _rp_cur_cc)
    get_filename_component(_stem ${_f} NAME_WE)
    get_filename_component(_ext ${_f} LAST_EXT)
    string(MAKE_C_IDENTIFIER "${_stem}" _ident)
    string(APPEND _rp_reg "RP_CUR(\"${_stem}\", ${_ident})\n")
    if(_ext STREQUAL ".cc")
        continue()
    endif()
    if(_ext STREQUAL ".dsp")
        set(_inc "${_rp_gen}/currents-web/${_ident}_current.inc")
    else()
        set(_inc "${_f}")
    endif()
    set(_w "/* generated by core_ripple.cmake: the current ${_stem}, as sdk/tools/currents.py --web writes it */\n")
    string(APPEND _w "#define rc_entry rc_entry_${_ident}\n#include \"${_inc}\"\n")
    set(_wf ${_rp_bin}/cur_${_ident}.c)
    set(_old "")
    if(EXISTS ${_wf})
        file(READ ${_wf} _old)
    endif()
    if(NOT _old STREQUAL _w)
        file(WRITE ${_wf} "${_w}")
    endif()
    list(APPEND _rp_cur_wrap_rel ${_rp_rel_bin}/cur_${_ident}.c)
endforeach()
set(_old "")
if(EXISTS ${_rp_bin}/ripple_currents.h)
    file(READ ${_rp_bin}/ripple_currents.h _old)
endif()
if(NOT _old STREQUAL _rp_reg)
    file(WRITE ${_rp_bin}/ripple_currents.h "${_rp_reg}")
endif()
set(_rp_cc_rel "")
foreach(_f IN LISTS _rp_cur_cc _rp_plaits_lib)
    file(RELATIVE_PATH _r ${_rp} ${_f})
    list(APPEND _rp_cc_rel ${_rp_rel}/${_r})
endforeach()

set(_rp_gen_py "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', {k: v for k, v in b.read_conf(b.APP / 'fm1.toml').items() if k != 'gen'}), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
set(_rp_inc ${_rp_app} ${_rp_app}/lua ${_rp}/sdk/include ${_rp}/sdk/faust ${_rp}/base/include ${_rp}/base/device
             ${_rp}/base/lib ${_rp_bin} ${_rp_app}/third_party/lua-5.4.7/src)
fm1_add_core(ripple ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_rp} UNIT core_ripple.c DESC core_ripple_desc.c
             EXTRA_SOURCES ripple_lua_hook.c ../common/rfm1_host.c ${_rp_rel}/app/lua/lua_one.c
                           ${_rp_rel}/app/midifx/mfx_one.c ${_rp_cur_wrap_rel} ${_rp_cc_rel}
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "Ripple by Charles Vestal (MPL-2.0): droplets (Lua) playing currents (Faust, C). Built on fm1-base and felucca-bsp (MPL-2.0), from Felucca by Leo Kuroshita (Hügelton Instruments). Lua 5.4 (MIT, Lua.org, PUC-Rio). Currents in Faust (GRAME). Mutable Instruments Plaits and stmlib by Emilie Gillet (MIT). Sophie (Matt Estela and the digisophie contributors) and MonkSynth (Jonathan Taylor), MIT. The MIDI effects from e16-launcher (MIT). Fonts: Barlow Semi Condensed (The Barlow Project Authors) and Terminus (Dimitar Toshkov Zhekov), SIL OFL 1.1. A Fantasy CFW: runs in RemoteFM1 hosts, its droplets from your RemoteFM1 folder (code/ripple/droplets, code/ripple/lib)."
             GEN felucca_font.h ann_words.h disk_image.h builtin_image.h GEN_ROOT app GEN_PYTHON "${_rp_gen_py}"
             EXTRA_GEN "${CMAKE_CURRENT_LIST_DIR}/ripple_gen.py --gen build/gen"
             PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_rp_ver}
             FLAGS -ffp-contract=off "-ivfsoverlay${_rp_vfs}"
             DEFINES APP_VERSION="${_rp_app_ver}" FELUCCA_ID="${_rp_id}" PLAT_HOST PLAT_WEB
             INCLUDES ${_rp_inc})
if(TARGET core_ripple_obj)
    set(_g ${CMAKE_CURRENT_LIST_DIR})
    set_property(SOURCE ${_g}/core_ripple.c APPEND PROPERTY OBJECT_DEPENDS ${_rp_bin}/app.c ${_rp_bin}/luaport.h
                 ${_rp_vfs} ${_rp_bin}/ripple_currents.h)
    # Lua and the droplet API, the MIDI effects (fm1.toml o2: build.py's flags, -DPLAT_HOST -DPLAT_WEB, each one's
    # directory first); the luaport.h overlay; RIPPLE_HEAP_KB: Lua's heap (the FM-1's 112 KB; a Fantasy CFW's 1 MB)
    set_source_files_properties(${_g}/${_rp_rel}/app/lua/lua_one.c PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off;-ivfsoverlay${_rp_vfs};-DRIPPLE_HEAP_KB=1024;-I${_rp_app}/lua"
        OBJECT_DEPENDS "${_rp_bin}/luaport.h;${_rp_vfs}")
    set_source_files_properties(${_g}/${_rp_rel}/app/midifx/mfx_one.c PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off;-I${_rp_app}/midifx")
    set_source_files_properties(${_g}/ripple_lua_hook.c PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-ivfsoverlay${_rp_vfs}" OBJECT_DEPENDS "${_rp_bin}/luaport.h;${_rp_vfs}")
    # the C and Faust currents' wrappers (sdk/tools/currents.py --web's flags)
    foreach(_w IN LISTS _rp_cur_wrap_rel)
        set_source_files_properties(${_g}/${_w} PROPERTIES
            COMPILE_OPTIONS "-w;-ffp-contract=off;-I${_rp}/currents;-I${_rp_gen}/currents-web")
    endforeach()
    # the C++ currents and Plaits' and stmlib's sources (currents.py --web: CXXFLAGS, the ripple-cflags
    # -Iplaits/shim -Iplaits -DTEST; each current's rc_entry and C names its own: -Drc_entry, -DRC_WEB_ID)
    set(_cxx "-std=c++11;-nostdinc++;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-DRIPPLE_CURRENT;-w;-ffp-contract=off;-I${_rp}/currents/plaits/shim;-I${_rp}/currents/plaits;-DTEST")
    foreach(_f IN LISTS _rp_cur_cc)
        get_filename_component(_stem ${_f} NAME_WE)
        string(MAKE_C_IDENTIFIER "${_stem}" _ident)
        file(RELATIVE_PATH _r ${_rp} ${_f})
        set_source_files_properties(${_g}/${_rp_rel}/${_r} PROPERTIES
            COMPILE_OPTIONS "${_cxx};-Drc_entry=rc_entry_${_ident};-DRC_WEB_ID=${_ident}")
    endforeach()
    foreach(_f IN LISTS _rp_plaits_lib)
        file(RELATIVE_PATH _r ${_rp} ${_f})
        set_source_files_properties(${_g}/${_rp_rel}/${_r} PROPERTIES COMPILE_OPTIONS "${_cxx}")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): a Fantasy CFW (docs/VIRTUAL-CARTS.md), the folder's
# code (its droplets) read, its data written (none yet: data.save stays on the disk, in the flash image). The licence
# texts: Ripple's MPL-2.0 (fm1-base's and felucca-bsp's are the same text) and LICENSING.md, fm1-base's and
# felucca-bsp's notes, Lua's, the SDK's, Plaits' MIT, the fonts' OFL
fm1_add_rfm1(ripple VIRTUAL NEEDS fs1 FOLDERS code=r data=rw AUTHORS "Charles Vestal" MEMORY_MIB 16
             LICENSE_FILES "MPL-2.0 ({name}, fm1-base, felucca-bsp)=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "MIT (Lua 5.4.7)=app/third_party/lua-5.4.7/LICENSE"
                           "MIT (the currents SDK, the currents, the built-in droplets: sdk/LICENSE)=sdk/LICENSE"
                           "MIT (Mutable Instruments Plaits and stmlib)=currents/plaits/LICENSE"
                           "OFL-1.1 (the Barlow font in {name})=base/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=base/assets/fonts/Terminus-LICENSE.txt"
             RUN_ARGS --folder ${CMAKE_CURRENT_LIST_DIR}/test_folder --press PLAY@3000 --expect-ui run:a1tone)
