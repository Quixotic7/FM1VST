# SPDX-License-Identifier: GPL-3.0-only
# fm1_add_rfm1(): a core declared with fm1_add_core, also as an .rfm1 file (docs/RFM1-FORMAT.md, version 1): the same
# units (the same glue, defines, includes, per-file flags and generate step) compiled by Emscripten into one standalone
# WebAssembly module, with core-glue/common/rfm1_shim.c's flat exports, then packed with its manifest, credits and
# licence texts (tools/rfm1_pack.py) and checked (tools/rfm1_check.py). Docs: docs/RFM1.md.
#
# Nothing is built unless the build is configured with -DFM1_RFM1=ON (emcc on PATH, or -DFM1_EMCC=/path/to/emcc);
# without it the call is a no-op, so a glue can carry it unconditionally. Call it in the core's glue file, after its
# fm1_add_core (and after any set_source_files_properties the glue adds: those are read too):
#
#   fm1_add_rfm1(<id>
#       [AUTHORS <name..>]          the manifest's authors ("Leo Kuroshita (Hügelton Instruments)")
#       [MEMORY_MIB <n>]            the fixed linear memory (default FM1_RFM1_MEMORY_MIB, 8; at most 32)
#       [STACK_KIB <n>]             the C stack inside it (default 1024)
#       [LICENSE_FILES <name=path..>]  the licence texts to carry (SOURCE relative or absolute; "name" is the
#                                   section's first line). Default: the firmware's LICENSE (LICENSE.md, COPYING, ..),
#                                   each file in its LICENSES/ and its LICENSING.md, found at the firmware's root
#       [SOURCE_URL <url>]          the manifest's source_url (default: the descriptor's, "<repo>@<commit>")
#       [DEFINES <d..>] [FLAGS <f..>] [INCLUDES <dirs..>]   more, for the Wasm build only
#       [RUN_ARGS <args..>]         rfm1_run's options in the test ("--sysex <bank.syx>", "--settle 5000")
#       [HOSTED_DESPITE_GATE "<reason>"]  a GATED core still built as an .rfm1, to be hosted (RemoteFM1FW) and
#                                   downloaded, never bundled in an app or a public native build (CORES-PLAN.md 6)
#       [VIRTUAL]                   a Fantasy CFW (docs/VIRTUAL-CARTS.md): host-only, the manifest says "virtual":
#                                   true; MEMORY_MIB up to 512, the file under 16 MiB, the rfm1_host_1 imports allowed
#       [NEEDS <cap..>]             (VIRTUAL) the host capabilities it uses, the manifest's "needs" ("fs1": the files)
#       [FOLDERS <name=r|rw..>]     (VIRTUAL) the shared folder's parts it uses, the manifest's "folders"
#                                   ("audio=r" "code=r" "data=rw")
#       [NO_SKIN]                   leave the firmware's own skin out (below)
#       [NO_LABELS]                 leave the firmware's own button and knob labels out (below)
#       [METADATA <file>]           a RemoteFM1Metadata.json kept with the glue (relative to the calling file), used in
#                                   place of the one at the firmware repository's root: the owner's own skin and labels
#                                   for a firmware whose repository has none (its image paths: relative to that file)
#       [WANTS <cap..>])            host capabilities it uses when a host has them and does without otherwise, the
#                                   manifest's "wants" ("ain1": audio input, docs/VIRTUAL-CARTS.md 8). ain1 in WANTS
#                                   or NEEDS also makes the shim export rfm1_audio_in / rfm1_audio_in_info from the
#                                   core's fm1core_audio_in / fm1core_audio_in_info
#
# A skin (docs/RFM1-METADATA.md): if the firmware's repository has a RemoteFM1Metadata.json at its root (the root of
# the git checkout that holds SOURCE: the submodule, even when SOURCE is a folder inside it), its "skin" (images,
# theme, dim) is packed as the file's skin sections (docs/RFM1-FORMAT.md section 8), unless the call says NO_SKIN
# (the owner's switch: an image whose licence is in doubt). A bad field there is a warning in the build output and is
# left out; it never fails the build. Without that file the .rfm1 is as before, byte for byte.
# Its "labels" too, unless NO_LABELS: tools/rfm1_labels.py writes them as a header (rfm1_build/<id>/meta/) that
# core-glue/common/rfm1_shim.c is compiled with: labels.buttons replace the descriptor's button_names for those buttons
# (the host is given a copy of the descriptor), and the buttons and labels.knobs are the static live labels
# (docs/DYNAMIC-LABELS.md) served while the core has none of its own.
#
# Makes rfm1_<id> (build/rfm1/<id>-<version>.rfm1, also in ALL), appended to rfm1_all, and the test rfm1_<id>
# (rfm1_check + rfm1_run). The generate step is the core's own (gen_<id>). The coroutine: core-glue/common/coro.c
# (ucontext) is left out; a core whose units call fm1coro_* (a main loop that never returns: Dinghy, Melodee, Tonitor,
# plat with its SysEx coroutine) gets core-glue/common/coro_asyncify.c instead and Binaryen's Asyncify pass at the link
# (tools/rfm1/rfm1_link.cmake, which tells them apart: the others link exactly as before). Gated cores are refused
# (CORES-PLAN.md 6: an .rfm1 is a published binary) unless the call says HOSTED_DESPITE_GATE (the owner's decision of
# 2026-10-31 for SLOOP and Clipper: built as their authors intended, the .rfm1 hosted, not distributed in the app).
# (A public build, FM1_PUBLIC_BUILD=ON, still leaves the gated core out altogether: build hosted files without it.)
# Lua: a core whose units call lj_throw (a Lua port whose errors use core-glue/common/lj_asyncify.h: Ripple) gets
# core-glue/common/lj_asyncify.c and the same Asyncify pass, lj_try_impl in the removelist.
option(FM1_RFM1 "build the .rfm1 files of the cores whose glue calls fm1_add_rfm1 (Emscripten)" OFF)
set(FM1_RFM1_MEMORY_MIB 8 CACHE STRING "an .rfm1's fixed linear memory in MiB unless its fm1_add_rfm1 says otherwise")
set(FM1_RFM1_OPT -O3 CACHE STRING "the optimisation flag of the .rfm1 builds")
# more speed for the interpreter (docs/HOST-HELPERS.md 1, measured with rfm1_run --profile): emcc flags on every unit
# and at the link, LTO, a wasm-opt pass after the link (every module, not only the Asyncify ones), and the function
# names kept (for a profile only: a published file carries none)
set(FM1_RFM1_EXTRA_CFLAGS "" CACHE STRING "more emcc flags for every .rfm1 unit and link (-fno-math-errno ..)")
option(FM1_RFM1_LTO "the .rfm1 builds with -flto (the units and the shim inlined into each other)" OFF)
set(FM1_RFM1_POST_OPT "" CACHE STRING "wasm-opt's passes after the link, for every .rfm1 (-O3, -O4 ..); empty: none")
set(FM1_RFM1_HELPERS "" CACHE STRING "C library functions exported by name for the host to run natively (HOST-HELPERS.md)")
option(FM1_RFM1_NAMES "keep the function names in the .rfm1 (rfm1_run --fprofile); never for a published file" OFF)
set(FM1_RFM1_DIR ${CMAKE_BINARY_DIR}/rfm1)
get_filename_component(FM1_RFM1_TOOLS ${CMAKE_CURRENT_LIST_DIR}/../tools ABSOLUTE)
get_filename_component(FM1_RFM1_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
# the WebAssembly the format allows (its section 6: what wasm3 0.5.0 runs): the MVP, sign extension, non-trapping
# float-to-int, bulk memory (memory.copy / fill), mutable globals; Emscripten's defaults add reference types and more
set(FM1_RFM1_FEATURES -mcpu=mvp -msign-ext -mnontrapping-fptoint -mbulk-memory -mmutable-globals)
# the same features as wasm-opt names them (the coroutine's Asyncify pass: tools/rfm1/rfm1_link.cmake)
set(FM1_RFM1_WASM_OPT_FEATURES --mvp-features --enable-sign-ext --enable-nontrapping-float-to-int --enable-bulk-memory
    --enable-mutable-globals)
# the same sound on every host: no fused multiply-add
set(FM1_RFM1_CFLAGS -std=gnu11 -ffp-contract=off -fno-common -DFM1CORE_RFM1=1)
set_property(GLOBAL PROPERTY FM1_RFM1_IDS "")
# a try of cores whose glue has no fm1_add_rfm1 call yet: their ids (or "all": every core of this build), with the
# defaults (no AUTHORS). What their glue needs in Wasm shows at build time
set(FM1_RFM1_EXTRA_CORES "" CACHE STRING "more cores to build as .rfm1 with fm1_add_rfm1's defaults (ids, or all)")

if(FM1_RFM1)
    find_program(FM1_EMCC emcc)
    if(NOT FM1_EMCC)
        message(FATAL_ERROR "FM1_RFM1: emcc not found (install Emscripten, or -DFM1_EMCC=/path/to/emcc)")
    endif()
    execute_process(COMMAND ${FM1_EMCC} --version OUTPUT_VARIABLE _v ERROR_QUIET)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" FM1_EMCC_VERSION "${_v}")
    # Emscripten's tree (musl's COPYRIGHT: the runtime's libc, compiled into every module)
    get_filename_component(_emdir ${FM1_EMCC} DIRECTORY)
    set(FM1_EMSCRIPTEN_ROOT "")
    if(EXISTS ${_emdir}/em-config)
        execute_process(COMMAND ${_emdir}/em-config EMSCRIPTEN_ROOT OUTPUT_VARIABLE FM1_EMSCRIPTEN_ROOT
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    set(FM1_RFM1_MUSL_COPYRIGHT ${FM1_EMSCRIPTEN_ROOT}/system/lib/libc/musl/COPYRIGHT)
    # the coroutine's link (tools/rfm1/rfm1_link.cmake): emnm (does a core call fm1coro_*?) and Emscripten's wasm-opt
    find_program(FM1_EMNM emnm HINTS ${_emdir} ${FM1_EMSCRIPTEN_ROOT} NO_DEFAULT_PATH)
    set(_binaryen "")
    if(EXISTS ${_emdir}/em-config)
        execute_process(COMMAND ${_emdir}/em-config BINARYEN_ROOT OUTPUT_VARIABLE _binaryen
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    find_program(FM1_WASM_OPT wasm-opt HINTS ${_binaryen}/bin NO_DEFAULT_PATH)
    if(NOT FM1_EMNM OR NOT FM1_WASM_OPT)
        message(FATAL_ERROR "FM1_RFM1: emnm or Emscripten's wasm-opt not found (-DFM1_EMNM=.. -DFM1_WASM_OPT=..)")
    endif()
    if(NOT FM1_EMSCRIPTEN_ROOT OR NOT EXISTS ${FM1_RFM1_MUSL_COPYRIGHT})
        message(WARNING "FM1_RFM1: musl's COPYRIGHT not found in Emscripten's tree (${FM1_EMSCRIPTEN_ROOT}): the "
                        ".rfm1 files will carry Emscripten's notice without musl's")
        set(FM1_RFM1_MUSL_COPYRIGHT "")
    endif()
    execute_process(COMMAND git -C ${FM1_RFM1_ROOT} rev-parse --short=12 HEAD OUTPUT_VARIABLE _c
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(FM1_RFM1_BUILT_BY "FM1VST ${PROJECT_VERSION} (${_c}) fm1_add_rfm1, emcc ${FM1_EMCC_VERSION}")
    message(STATUS "rfm1: ${FM1_EMCC} ${FM1_EMCC_VERSION}, ${FM1_RFM1_MEMORY_MIB} MiB, into ${FM1_RFM1_DIR}")
endif()

# the root of the git checkout that holds a firmware's SOURCE (a submodule's root: SOURCE may be a folder in it); ""
# when SOURCE is not in a checkout of its own (FM1VST's tree itself)
function(fm1_rfm1_repo_root src out)
    set(_dir ${src})
    set(${out} "" PARENT_SCOPE)
    while(TRUE)
        if(EXISTS ${_dir}/.git)
            if(NOT _dir STREQUAL FM1_RFM1_ROOT)
                set(${out} ${_dir} PARENT_SCOPE)
            endif()
            return()
        endif()
        get_filename_component(_up ${_dir} DIRECTORY)
        if(_up STREQUAL _dir)
            return()
        endif()
        set(_dir ${_up})
    endwhile()
endfunction()

# the firmware's licence texts: LICENSE (or LICENCE, COPYING, with .md / .txt), each file of LICENSES/, LICENSING.md;
# at the firmware's SOURCE, else in its parents up to its git root (a SOURCE inside a submodule: nomanskyFM/firmware)
function(fm1_rfm1_license_files id src license out)
    set(_dir ${src})
    set(_found "")
    while(TRUE)
        foreach(_n LICENSE LICENSE.md LICENSE.txt LICENCE LICENCE.md COPYING COPYING.md COPYING.txt)
            if(EXISTS ${_dir}/${_n} AND NOT IS_DIRECTORY ${_dir}/${_n})
                list(APPEND _found "${license} ({name})=${_dir}/${_n}")
                break()
            endif()
        endforeach()
        if(_found OR EXISTS ${_dir}/.git)
            break()
        endif()
        get_filename_component(_up ${_dir} DIRECTORY)
        if(_up STREQUAL _dir)
            break()
        endif()
        set(_dir ${_up})
    endwhile()
    if(NOT _found)
        message(WARNING "${id} rfm1: no LICENSE file found at ${src} or above: give fm1_add_rfm1 LICENSE_FILES")
        set(_dir ${src})
    endif()
    file(GLOB _more ${_dir}/LICENSES/*)
    list(SORT _more)
    foreach(_f IN LISTS _more)
        if(NOT IS_DIRECTORY ${_f})
            get_filename_component(_stem ${_f} NAME_WLE)
            list(APPEND _found "${_stem}=${_f}")
        endif()
    endforeach()
    if(EXISTS ${_dir}/LICENSING.md)
        list(APPEND _found "LICENSING.md ({name})=${_dir}/LICENSING.md")
    endif()
    set(${out} "${_found}" PARENT_SCOPE)
endfunction()

function(fm1_add_rfm1 id)
    cmake_parse_arguments(A "VIRTUAL;NO_SKIN;NO_LABELS" "MEMORY_MIB;STACK_KIB;SOURCE_URL;HOSTED_DESPITE_GATE;METADATA"
                          "AUTHORS;LICENSE_FILES;DEFINES;FLAGS;INCLUDES;RUN_ARGS;NEEDS;FOLDERS;WANTS" ${ARGN})
    if(A_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "fm1_add_rfm1(${id}): unknown arguments ${A_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT FM1_RFM1)
        return()
    endif()
    get_property(_glue GLOBAL PROPERTY FM1_CORE_GLUE_${id})
    if(NOT _glue OR NOT TARGET core_${id}_obj)
        return()   # (left out of this build: a public build's gated core, FM1_ONLY_CORES)
    endif()
    get_property(_gated GLOBAL PROPERTY FM1_CORE_GATED_${id})
    if(_gated AND A_HOSTED_DESPITE_GATE)
        message(STATUS "${id} rfm1: gated (${_gated}), built as a hosted file: ${A_HOSTED_DESPITE_GATE}")
    elseif(_gated)
        message(WARNING "${id} rfm1: not built: the core is gated (${_gated})")
        return()
    endif()
    foreach(_k VERSION LICENSE SOURCE UNIT GEN_TARGET GEN_HEADERS)
        get_property(_${_k} GLOBAL PROPERTY FM1_CORE_${_k}_${id})
    endforeach()
    if(NOT A_MEMORY_MIB)
        set(A_MEMORY_MIB ${FM1_RFM1_MEMORY_MIB})
    endif()
    if(A_VIRTUAL)                 # a Fantasy CFW: docs/VIRTUAL-CARTS.md section 2
        set(_maxmem 512)
    else()
        set(_maxmem 32)
        if(A_NEEDS OR A_FOLDERS)
            message(FATAL_ERROR "fm1_add_rfm1(${id}): NEEDS and FOLDERS are for a VIRTUAL cart (docs/VIRTUAL-CARTS.md)")
        endif()
    endif()
    if(A_MEMORY_MIB GREATER _maxmem OR A_MEMORY_MIB LESS 1)
        message(FATAL_ERROR "fm1_add_rfm1(${id}): MEMORY_MIB ${A_MEMORY_MIB}: 1 .. ${_maxmem} (docs/RFM1-FORMAT.md 6, "
                            "docs/VIRTUAL-CARTS.md 2)")
    endif()
    foreach(_f IN LISTS A_FOLDERS)
        if(NOT _f MATCHES "^(audio|code|data)=(r|rw)$")
            message(FATAL_ERROR "fm1_add_rfm1(${id}): FOLDERS \"${_f}\": audio|code|data = r|rw")
        endif()
    endforeach()
    if(NOT A_STACK_KIB)
        set(A_STACK_KIB 1024)
    endif()
    math(EXPR _mem "${A_MEMORY_MIB} * 1048576")
    math(EXPR _stack "${A_STACK_KIB} * 1024")
    string(REGEX REPLACE "[^A-Za-z0-9._+-]" "_" _fver "${_VERSION}")
    set(_wdir ${CMAKE_BINARY_DIR}/rfm1_build/${id})
    set(_out ${FM1_RFM1_DIR}/${id}-${_fver}.rfm1)

    # what the native object library is compiled with: its includes, definitions and options (not its -O)
    get_target_property(_tinc core_${id}_obj INCLUDE_DIRECTORIES)
    get_target_property(_tdef core_${id}_obj COMPILE_DEFINITIONS)
    get_target_property(_topt core_${id}_obj COMPILE_OPTIONS)
    get_target_property(_srcs core_${id}_obj SOURCES)
    foreach(_v _tinc _tdef _topt)
        if(NOT ${_v})
            set(${_v} "")
        endif()
    endforeach()
    list(FILTER _topt EXCLUDE REGEX "^-O")
    if(A_HOSTED_DESPITE_GATE)   # (a hosted file is the firmware as its author builds it: no "gated" in its descriptor)
        list(FILTER _tdef EXCLUDE REGEX "^FM1CORE_GATED=")
    endif()
    if("${_tinc};${_tdef};${_topt}" MATCHES "\\$<")
        message(FATAL_ERROR "fm1_add_rfm1(${id}): a generator expression in core_${id}_obj's includes or "
                            "definitions: not supported")
    endif()
    set(_base ${FM1_RFM1_FEATURES} ${FM1_RFM1_OPT} ${FM1_RFM1_CFLAGS} ${FM1_RFM1_EXTRA_CFLAGS})
    if(FM1_RFM1_LTO)
        list(APPEND _base -flto)
    endif()
    foreach(_d IN LISTS _tdef A_DEFINES)
        list(APPEND _base -D${_d})
    endforeach()
    foreach(_i IN LISTS _tinc A_INCLUDES)
        list(APPEND _base -I${_i})
    endforeach()
    list(APPEND _base -I${FM1_COMMON_DIR}/wasm_compat ${_topt} ${A_FLAGS})

    # the firmware's RemoteFM1Metadata.json (its repository's root): its labels, as the shim's header (below: the skin)
    fm1_rfm1_repo_root(${_SOURCE} _repo)
    set(_meta "")
    if(A_METADATA)
        # the glue's own file (METADATA): it wins over the repository's; its folder is where its paths start
        get_filename_component(_meta ${A_METADATA} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
        if(NOT EXISTS ${_meta})
            message(FATAL_ERROR "fm1_add_rfm1(${id}): METADATA ${_meta} does not exist")
        endif()
        get_filename_component(_repo ${_meta} DIRECTORY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_meta})
    elseif(_repo AND EXISTS ${_repo}/RemoteFM1Metadata.json)
        set(_meta ${_repo}/RemoteFM1Metadata.json)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_meta})
    endif()
    set(_meta_h "")
    set(_meta_msg "")
    set(_shim_meta "")
    if(_meta AND A_NO_LABELS)
        set(_meta_msg ", its RemoteFM1Metadata.json labels left out (NO_LABELS)")
    elseif(_meta)
        set(_meta_h ${_wdir}/meta/rfm1_meta_labels.h)
        set(_jw "")
        if(NOT A_NO_SKIN)
            set(_jw --no-json-warning)   # (rfm1_pack says it, for the skin)
        endif()
        add_custom_command(OUTPUT ${_meta_h}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_wdir}/meta
            COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1_labels.py header --metadata ${_meta} --out ${_meta_h} --id ${id}
                    ${_jw}
            DEPENDS ${_meta} ${FM1_RFM1_TOOLS}/rfm1_labels.py
            COMMENT "rfm1 ${id}: the labels of RemoteFM1Metadata.json"
            VERBATIM)
        list(APPEND _shim_meta -DRFM1_META=1 -I${_wdir}/meta)
        set(_meta_msg ", labels from ${_meta}")
    endif()

    # the units: the core's own, without the coroutine (ucontext), plus the shim; the coroutine on Asyncify is its
    # own object, linked only into a core that calls it (rfm1_link.cmake)
    list(FILTER _srcs EXCLUDE REGEX "/coro\\.c$")
    list(APPEND _srcs ${FM1_COMMON_DIR}/rfm1_shim.c)
    set(_shim_opt "")             # audio input (docs/VIRTUAL-CARTS.md 8): the shim's two exports, only when asked for
    if("ain1" IN_LIST A_WANTS OR "ain1" IN_LIST A_NEEDS)
        set(_shim_opt -DRFM1_AIN1=1)
    endif()
    set(_objs "")
    set(_n 0)
    foreach(_s IN LISTS _srcs)
        get_filename_component(_name ${_s} NAME_WE)
        set(_o ${_wdir}/obj/${_n}_${_name}.o)
        math(EXPR _n "${_n} + 1")
        set(_sopt "")
        set(_sdeps "")
        if(_s STREQUAL "${FM1_COMMON_DIR}/rfm1_shim.c")
            set(_sopt ${_shim_opt} ${_shim_meta})
            set(_sdeps ${_meta_h})
        else()
            get_source_file_property(_sopt ${_s} COMPILE_OPTIONS)
            get_source_file_property(_sdef ${_s} COMPILE_DEFINITIONS)
            get_source_file_property(_sdeps ${_s} OBJECT_DEPENDS)
            foreach(_v _sopt _sdef _sdeps)
                if(NOT ${_v})
                    set(${_v} "")
                endif()
            endforeach()
            foreach(_d IN LISTS _sdef)
                list(APPEND _sopt -D${_d})
            endforeach()
        endif()
        add_custom_command(OUTPUT ${_o}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_wdir}/obj
            COMMAND ${FM1_EMCC} ${_base} ${_sopt} -MD -MF ${_o}.d -c ${_s} -o ${_o}
            DEPENDS ${_s} ${_sdeps}
            DEPFILE ${_o}.d
            COMMENT "rfm1 ${id}: ${_name}.c"
            VERBATIM)
        list(APPEND _objs ${_o})
    endforeach()

    # Lua's errors on Asyncify (lj_asyncify.c): its own object too, linked only into a core that calls lj_throw
    set(_lj_o ${_wdir}/obj/lj_asyncify.o)
    add_custom_command(OUTPUT ${_lj_o}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_wdir}/obj
        COMMAND ${FM1_EMCC} ${FM1_RFM1_FEATURES} ${FM1_RFM1_OPT} ${FM1_RFM1_CFLAGS} -Wall -Wextra -I${FM1_COMMON_DIR}
                -MD -MF ${_lj_o}.d -c ${FM1_COMMON_DIR}/lj_asyncify.c -o ${_lj_o}
        DEPENDS ${FM1_COMMON_DIR}/lj_asyncify.c ${FM1_COMMON_DIR}/lj_asyncify.h
        DEPFILE ${_lj_o}.d
        COMMENT "rfm1 ${id}: lj_asyncify.c"
        VERBATIM)
    set(_coro_o ${_wdir}/obj/coro_asyncify.o)
    add_custom_command(OUTPUT ${_coro_o}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_wdir}/obj
        COMMAND ${FM1_EMCC} ${FM1_RFM1_FEATURES} ${FM1_RFM1_OPT} ${FM1_RFM1_CFLAGS} -Wall -Wextra -I${FM1_COMMON_DIR}
                -MD -MF ${_coro_o}.d -c ${FM1_COMMON_DIR}/coro_asyncify.c -o ${_coro_o}
        DEPENDS ${FM1_COMMON_DIR}/coro_asyncify.c
        DEPFILE ${_coro_o}.d
        COMMENT "rfm1 ${id}: coro_asyncify.c"
        VERBATIM)

    # the module: a WASI reactor (_initialize), no JS, no file system, fixed memory (tools/rfm1/rfm1_link.cmake: the
    # coroutine's Asyncify pass when the units call fm1coro_*)
    set(_wasm ${_wdir}/${id}.wasm)
    set(_link_tail --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sSTACK_SIZE=${_stack} -sINITIAL_MEMORY=${_mem}
                   -sALLOW_MEMORY_GROWTH=0 -sEXPORTED_FUNCTIONS=[] -Wl,--gc-sections)
    set(_link_args ${FM1_RFM1_FEATURES} ${FM1_RFM1_OPT} ${FM1_RFM1_EXTRA_CFLAGS})
    if(FM1_RFM1_LTO)
        list(APPEND _link_args -flto)
    endif()
    if(FM1_RFM1_NAMES)
        list(APPEND _link_tail --profiling-funcs)
    endif()
    foreach(_h IN LISTS FM1_RFM1_HELPERS)   # (docs/HOST-HELPERS.md 3: the host may run it natively)
        list(APPEND _link_tail -Wl,--export-if-defined=${_h})
    endforeach()
    set(_link_conf ${_wdir}/link.cmake)
    set(_c "")
    foreach(_v EMCC=FM1_EMCC EMNM=FM1_EMNM WASM_OPT=FM1_WASM_OPT LINK_ARGS=_link_args OBJS=_objs LINK_TAIL=_link_tail
               CORO_OBJ=_coro_o LJ_OBJ=_lj_o WASM_OPT_FEATURES=FM1_RFM1_WASM_OPT_FEATURES OPT=FM1_RFM1_OPT OUT=_wasm
               POST_OPT=FM1_RFM1_POST_OPT NAMES=FM1_RFM1_NAMES)
        string(REPLACE "=" ";" _v "${_v}")
        list(GET _v 0 _k)
        list(GET _v 1 _src)
        string(APPEND _c "set(${_k} \"${${_src}}\")\n")
    endforeach()
    file(CONFIGURE OUTPUT ${_link_conf} CONTENT "${_c}" @ONLY)
    add_custom_command(OUTPUT ${_wasm}
        COMMAND ${CMAKE_COMMAND} -DCONF=${_link_conf} -P ${FM1_RFM1_TOOLS}/rfm1/rfm1_link.cmake
        DEPENDS ${_objs} ${_coro_o} ${_lj_o} ${_link_conf} ${FM1_RFM1_TOOLS}/rfm1/rfm1_link.cmake
        COMMENT "rfm1 ${id}: linking ${id}.wasm"
        VERBATIM)
    # the descriptor as the module itself reports it (rfm1_run --describe), then the manifest and the sections
    set(_desc ${_wdir}/${id}.describe.json)
    add_custom_command(OUTPUT ${_desc}
        COMMAND rfm1_run --describe --out ${_desc} ${_wasm}
        DEPENDS ${_wasm} rfm1_run
        COMMENT "rfm1 ${id}: the descriptor"
        VERBATIM)
    if(A_LICENSE_FILES)
        set(_lics "")
        foreach(_l IN LISTS A_LICENSE_FILES)
            string(FIND "${_l}" "=" _eq)
            if(_eq LESS 1)
                message(FATAL_ERROR "fm1_add_rfm1(${id}): LICENSE_FILES \"${_l}\": name=path")
            endif()
            string(SUBSTRING "${_l}" 0 ${_eq} _ln)
            math(EXPR _eq "${_eq} + 1")
            string(SUBSTRING "${_l}" ${_eq} -1 _lp)
            if(NOT IS_ABSOLUTE ${_lp})
                set(_lp ${_SOURCE}/${_lp})
            endif()
            if(NOT EXISTS ${_lp})
                message(FATAL_ERROR "fm1_add_rfm1(${id}): ${_lp} not found")
            endif()
            list(APPEND _lics "${_ln}=${_lp}")
        endforeach()
    else()
        fm1_rfm1_license_files(${id} ${_SOURCE} "${_LICENSE}" _lics)
    endif()
    # the glue's (FM1VST's) and the runtime's
    list(APPEND _lics "GPL-3.0-only (FM1VST core glue)=${FM1_RFM1_ROOT}/LICENSE"
                      "MIT (Emscripten runtime)=${FM1_RFM1_TOOLS}/rfm1/emscripten-LICENSE.txt")
    if(FM1_RFM1_MUSL_COPYRIGHT)
        list(APPEND _lics "MIT (musl libc, in the Emscripten runtime)=${FM1_RFM1_MUSL_COPYRIGHT}")
    endif()
    set(_pack_args --wasm ${_wasm} --describe ${_desc} --out ${_out} --built-by "${FM1_RFM1_BUILT_BY}")
    set(_lic_paths "")
    foreach(_l IN LISTS _lics)
        list(APPEND _pack_args --license-file "${_l}")
        string(REGEX REPLACE "^[^=]*=" "" _lp "${_l}")
        list(APPEND _lic_paths ${_lp})
    endforeach()
    foreach(_a IN LISTS A_AUTHORS)
        list(APPEND _pack_args --author "${_a}")
    endforeach()
    if(A_SOURCE_URL)
        list(APPEND _pack_args --source-url "${A_SOURCE_URL}")
    endif()
    foreach(_w IN LISTS A_WANTS)  # the manifest's "wants" (any cart: hosts ignore what they lack)
        list(APPEND _pack_args --wants ${_w})
    endforeach()
    if(A_VIRTUAL)                 # the manifest's "virtual", "needs", "folders" (rfm1_check then applies section 2)
        list(APPEND _pack_args --virtual)
        foreach(_n IN LISTS A_NEEDS)
            list(APPEND _pack_args --needs ${_n})
        endforeach()
        foreach(_f IN LISTS A_FOLDERS)
            list(APPEND _pack_args --folder ${_f})
        endforeach()
    endif()
    # the firmware's own skin (RemoteFM1Metadata.json at its repository's root; rfm1_pack checks it, field by field)
    set(_skin_deps "")
    set(_skin_msg "")
    if(_meta)
        if(A_NO_SKIN)
            set(_skin_msg ", its RemoteFM1Metadata.json skin left out (NO_SKIN)")
        else()
            list(APPEND _pack_args --skin-metadata ${_meta})
            list(APPEND _skin_deps ${_meta})
            set(_skin_msg ", skin from ${_meta}")
            # (the images too, so a changed one repacks: the paths that are there; rfm1_pack does the checking)
            file(READ ${_meta} _mj)
            foreach(_k background landscape)
                string(JSON _ip ERROR_VARIABLE _je GET "${_mj}" skin ${_k})
                if(_je OR "${_ip}" STREQUAL "" OR IS_ABSOLUTE "${_ip}")
                    continue()
                endif()
                get_filename_component(_ipr ${_repo}/${_ip} REALPATH)
                get_filename_component(_rr ${_repo} REALPATH)
                string(FIND "${_ipr}" "${_rr}/" _at)
                if(_at EQUAL 0 AND EXISTS ${_ipr} AND NOT IS_DIRECTORY ${_ipr})
                    list(APPEND _skin_deps ${_ipr})
                endif()
            endforeach()
        endif()
    endif()
    add_custom_command(OUTPUT ${_out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FM1_RFM1_DIR}
        COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1_pack.py ${_pack_args}
        COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1_check.py --quiet ${_out}
        DEPENDS ${_wasm} ${_desc} ${FM1_RFM1_TOOLS}/rfm1_pack.py ${FM1_RFM1_TOOLS}/rfm1_check.py ${_lic_paths}
                ${FM1_RFM1_TOOLS}/rfm1_skin.py ${_skin_deps}
        COMMENT "rfm1 ${id}: ${id}-${_fver}.rfm1"
        VERBATIM)
    add_custom_target(rfm1_${id} ALL DEPENDS ${_out})
    if(_GEN_TARGET)
        add_dependencies(rfm1_${id} ${_GEN_TARGET})
    endif()
    set_property(GLOBAL APPEND PROPERTY FM1_RFM1_IDS ${id})
    set_property(GLOBAL PROPERTY FM1_RFM1_FILE_${id} ${_out})
    set_property(GLOBAL PROPERTY FM1_RFM1_RUN_ARGS_${id} "${A_RUN_ARGS}")
    set(_v "")
    if(A_VIRTUAL)
        set(_v ", virtual (Fantasy CFW): needs ${A_NEEDS}, folders ${A_FOLDERS}")
    endif()
    if(A_WANTS)
        string(APPEND _v ", wants ${A_WANTS}")
    endif()
    string(APPEND _v "${_skin_msg}${_meta_msg}")
    message(STATUS "${id} rfm1: ${id}-${_fver}.rfm1, ${A_MEMORY_MIB} MiB${_v}")
endfunction()

# after every glue file: the tools (rfm1_run, on wasm3), rfm1_all and the tests
function(fm1_rfm1_finish)
    if(NOT FM1_RFM1)
        return()
    endif()
    get_property(_ids GLOBAL PROPERTY FM1_RFM1_IDS)
    set(_extra ${FM1_RFM1_EXTRA_CORES})
    if(_extra STREQUAL "all")
        set(_extra ${FM1_CORE_IDS})
    endif()
    foreach(_id IN LISTS _extra)
        if(NOT _id IN_LIST _ids)
            fm1_add_rfm1(${_id})
        endif()
    endforeach()
    # wasm3 v0.5.0 (third_party/wasm3, MIT), with its release flags
    set(_w3 ${FM1_RFM1_ROOT}/third_party/wasm3/source)
    set(_w3src "")
    foreach(_f bind code compile core emit env exec function info module optimize parse)
        list(APPEND _w3src ${_w3}/m3_${_f}.c)
    endforeach()
    add_library(fm1_wasm3 STATIC ${_w3src})
    target_include_directories(fm1_wasm3 PUBLIC ${_w3})
    target_compile_options(fm1_wasm3 PRIVATE -O3 -fomit-frame-pointer -fno-stack-check -fno-stack-protector -w)
    set_target_properties(fm1_wasm3 PROPERTIES C_STANDARD 11)
    add_executable(rfm1_run ${FM1_RFM1_TOOLS}/rfm1_run/rfm1_run.c)
    target_include_directories(rfm1_run PRIVATE ${FM1_CORE_API_DIR})
    target_compile_options(rfm1_run PRIVATE -O2 -Wall -Wextra)
    target_link_libraries(rfm1_run PRIVATE fm1_wasm3)
    if(MATH_LIB)
        target_link_libraries(rfm1_run PRIVATE ${MATH_LIB})
    endif()
    set_target_properties(rfm1_run PROPERTIES C_STANDARD 11)

    get_property(_ids GLOBAL PROPERTY FM1_RFM1_IDS)
    set(_targets ${_ids})
    list(TRANSFORM _targets PREPEND rfm1_)
    add_custom_target(rfm1_all DEPENDS rfm1_run ${_targets})
    foreach(_id IN LISTS _ids)
        get_property(_f GLOBAL PROPERTY FM1_RFM1_FILE_${_id})
        get_property(_ra GLOBAL PROPERTY FM1_RFM1_RUN_ARGS_${_id})
        string(REPLACE ";" " " _ra "${_ra}")
        add_test(NAME rfm1_${_id}
                 COMMAND ${CMAKE_COMMAND} -DPYTHON=${CR_PYTHON} -DCHECK=${FM1_RFM1_TOOLS}/rfm1_check.py
                         -DRUN=$<TARGET_FILE:rfm1_run> -DFILE=${_f} "-DRUN_ARGS=${_ra}"
                         -P ${FM1_RFM1_TOOLS}/rfm1/rfm1_test.cmake)
        set_tests_properties(rfm1_${_id} PROPERTIES TIMEOUT 300)
    endforeach()
    # a firmware's RemoteFM1Metadata.json: the skin through rfm1_pack / rfm1_check, the labels through rfm1_shim.c
    add_test(NAME rfm1_skin COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1/rfm1_skin_test.py)
    add_test(NAME rfm1_labels COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1/rfm1_labels_test.py --emcc ${FM1_EMCC}
                                      --run $<TARGET_FILE:rfm1_run>)
    message(STATUS "rfm1: ${_ids}")
endfunction()
