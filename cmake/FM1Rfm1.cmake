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
#       [RUN_ARGS <args..>])        rfm1_run's options in the test ("--sysex <bank.syx>", "--settle 5000")
#
# Makes rfm1_<id> (build/rfm1/<id>-<version>.rfm1, also in ALL), appended to rfm1_all, and the test rfm1_<id>
# (rfm1_check + rfm1_run). The generate step is the core's own (gen_<id>). The coroutine: core-glue/common/coro.c
# (ucontext) is left out; a core whose units call fm1coro_* (a main loop that never returns: Dinghy, Melodee, Tonitor,
# plat with its SysEx coroutine) gets core-glue/common/coro_asyncify.c instead and Binaryen's Asyncify pass at the link
# (tools/rfm1/rfm1_link.cmake, which tells them apart: the others link exactly as before). Gated cores are refused
# (CORES-PLAN.md 6: an .rfm1 is a published binary).
option(FM1_RFM1 "build the .rfm1 files of the cores whose glue calls fm1_add_rfm1 (Emscripten)" OFF)
set(FM1_RFM1_MEMORY_MIB 8 CACHE STRING "an .rfm1's fixed linear memory in MiB unless its fm1_add_rfm1 says otherwise")
set(FM1_RFM1_OPT -O3 CACHE STRING "the optimisation flag of the .rfm1 builds")
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
    cmake_parse_arguments(A "" "MEMORY_MIB;STACK_KIB;SOURCE_URL" "AUTHORS;LICENSE_FILES;DEFINES;FLAGS;INCLUDES;RUN_ARGS"
                          ${ARGN})
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
    if(_gated)
        message(WARNING "${id} rfm1: not built: the core is gated (${_gated})")
        return()
    endif()
    foreach(_k VERSION LICENSE SOURCE UNIT GEN_TARGET GEN_HEADERS)
        get_property(_${_k} GLOBAL PROPERTY FM1_CORE_${_k}_${id})
    endforeach()
    if(NOT A_MEMORY_MIB)
        set(A_MEMORY_MIB ${FM1_RFM1_MEMORY_MIB})
    endif()
    if(A_MEMORY_MIB GREATER 32 OR A_MEMORY_MIB LESS 1)
        message(FATAL_ERROR "fm1_add_rfm1(${id}): MEMORY_MIB ${A_MEMORY_MIB}: 1 .. 32 (docs/RFM1-FORMAT.md 6)")
    endif()
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
    if("${_tinc};${_tdef};${_topt}" MATCHES "\\$<")
        message(FATAL_ERROR "fm1_add_rfm1(${id}): a generator expression in core_${id}_obj's includes or "
                            "definitions: not supported")
    endif()
    set(_base ${FM1_RFM1_FEATURES} ${FM1_RFM1_OPT} ${FM1_RFM1_CFLAGS})
    foreach(_d IN LISTS _tdef A_DEFINES)
        list(APPEND _base -D${_d})
    endforeach()
    foreach(_i IN LISTS _tinc A_INCLUDES)
        list(APPEND _base -I${_i})
    endforeach()
    list(APPEND _base -I${FM1_COMMON_DIR}/wasm_compat ${_topt} ${A_FLAGS})

    # the units: the core's own, without the coroutine (ucontext), plus the shim; the coroutine on Asyncify is its
    # own object, linked only into a core that calls it (rfm1_link.cmake)
    list(FILTER _srcs EXCLUDE REGEX "/coro\\.c$")
    list(APPEND _srcs ${FM1_COMMON_DIR}/rfm1_shim.c)
    set(_objs "")
    set(_n 0)
    foreach(_s IN LISTS _srcs)
        get_filename_component(_name ${_s} NAME_WE)
        set(_o ${_wdir}/obj/${_n}_${_name}.o)
        math(EXPR _n "${_n} + 1")
        set(_sopt "")
        set(_sdeps "")
        if(NOT _s STREQUAL "${FM1_COMMON_DIR}/rfm1_shim.c")
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
    set(_link_args ${FM1_RFM1_FEATURES} ${FM1_RFM1_OPT})
    set(_link_conf ${_wdir}/link.cmake)
    set(_c "")
    foreach(_v EMCC=FM1_EMCC EMNM=FM1_EMNM WASM_OPT=FM1_WASM_OPT LINK_ARGS=_link_args OBJS=_objs LINK_TAIL=_link_tail
               CORO_OBJ=_coro_o WASM_OPT_FEATURES=FM1_RFM1_WASM_OPT_FEATURES OPT=FM1_RFM1_OPT OUT=_wasm)
        string(REPLACE "=" ";" _v "${_v}")
        list(GET _v 0 _k)
        list(GET _v 1 _src)
        string(APPEND _c "set(${_k} \"${${_src}}\")\n")
    endforeach()
    file(CONFIGURE OUTPUT ${_link_conf} CONTENT "${_c}" @ONLY)
    add_custom_command(OUTPUT ${_wasm}
        COMMAND ${CMAKE_COMMAND} -DCONF=${_link_conf} -P ${FM1_RFM1_TOOLS}/rfm1/rfm1_link.cmake
        DEPENDS ${_objs} ${_coro_o} ${_link_conf} ${FM1_RFM1_TOOLS}/rfm1/rfm1_link.cmake
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
    add_custom_command(OUTPUT ${_out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FM1_RFM1_DIR}
        COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1_pack.py ${_pack_args}
        COMMAND ${CR_PYTHON} ${FM1_RFM1_TOOLS}/rfm1_check.py --quiet ${_out}
        DEPENDS ${_wasm} ${_desc} ${FM1_RFM1_TOOLS}/rfm1_pack.py ${FM1_RFM1_TOOLS}/rfm1_check.py ${_lic_paths}
        COMMENT "rfm1 ${id}: ${id}-${_fver}.rfm1"
        VERBATIM)
    add_custom_target(rfm1_${id} ALL DEPENDS ${_out})
    if(_GEN_TARGET)
        add_dependencies(rfm1_${id} ${_GEN_TARGET})
    endif()
    set_property(GLOBAL APPEND PROPERTY FM1_RFM1_IDS ${id})
    set_property(GLOBAL PROPERTY FM1_RFM1_FILE_${id} ${_out})
    set_property(GLOBAL PROPERTY FM1_RFM1_RUN_ARGS_${id} "${A_RUN_ARGS}")
    message(STATUS "${id} rfm1: ${id}-${_fver}.rfm1, ${A_MEMORY_MIB} MiB")
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
    message(STATUS "rfm1: ${_ids}")
endfunction()
