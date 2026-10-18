# SPDX-License-Identifier: GPL-3.0-only
# fm1_add_core(): one firmware as a loadable core module (core-api/fm1core.h). Each core's call lives in its glue
# folder, core-glue/<family>/core.cmake (or core_<id>.cmake when a family has several cores), which the top-level
# CMakeLists picks up by a glob: adding a core never edits a shared file. Template (core-glue/common/README.md):
#
#   fm1_add_core(<id> ${CMAKE_CURRENT_LIST_DIR}
#       SOURCE <dir>              the firmware's submodule (cores/<name>, unmodified; absolute)
#       UNIT <file.c>             the firmware unit (glue dir relative): the firmware's sources + the glue (-w)
#       DESC <file.c>             the descriptor (its own translation unit, -Wall -Wextra)
#       [EXTRA_SOURCES <f..>]     more glue units (glue dir relative; -Wall)
#       [FAMILY <family>]         the descriptor's family (default: <id>)
#       [LICENSE <spdx>]          the firmware's licence (default GPL-3.0-only)
#       [GATED "<reason>"]        kept out of public builds (FM1_PUBLIC_BUILD=ON); no double quotes in the reason
#       generate step, one of:
#         GEN <headers..>         what <SOURCE>/<GEN_ROOT>/tools/build.py generate() writes into <GEN_ROOT>/build/gen
#           [GEN_ROOT <dir>]      (SOURCE relative; default ".": a fork whose build.py is in felucca-base/ says so)
#           [GEN_PYTHON <code>]   instead of "__import__('build').generate()" (one Python expression, no ';': a
#                                 CMake list separator; run with PYTHONPATH=<GEN_ROOT>/tools)
#           [EXTRA_GEN <cmd..>]   more generators, one command line each, run after it with the same python in
#                                 <GEN_ROOT> ("tools/gen_909_tables.py", "tools/gen_x.py --out build/gen/x.h")
#           [GEN_OUTPUTS <f..>]   what EXTRA_GEN writes outside build/gen (SOURCE relative), for the dependencies
#           [PYTHON_NEEDS <m..>]  checked at configure time: python modules ("numpy"), or "raqm" (Pillow with
#                                 libraqm). The generators run with DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib set
#                                 through cmake -E env (not /bin/sh: SIP strips DYLD_* there), so Homebrew's libraqm
#                                 is found
#         NO_GEN                  nothing generated (the headers are committed)
#       version, one of:
#         VERSION <string>        given (a firmware whose version is not in a source file: APP_VER)
#         VERSION_FILE <file> VERSION_REGEX <re> [VERSION_GROUP n]   the regex's group n on the file's first
#                                 matching line (SOURCE relative)
#       [FLAGS <flags..>]         compile flags for the firmware unit ("-Dsection(x)=aligned(4)": the .pool section
#                                 attribute Mach-O rejects)
#       [INCLUDES <dirs..>] [DEFINES <defs..>]   (SOURCE/firmware/src and the gen dir are on the path already)
#       [SUBMODULES_RECURSIVE]    the firmware has submodules of its own: configure fails unless they are checked out
#       [PREFIX <XX>])            the older glues' FM1CORE_<XX>_VERSION / _COMMIT defines (optional)
#
# Every unit of the core gets FM1CORE_ID, FM1CORE_VERSION, FM1CORE_COMMIT, FM1CORE_FAMILY, FM1CORE_LICENSE (and
# FM1CORE_GATED) as string defines (core-glue/common/version.h), and core-api/ and core-glue/common/ on its include
# path; core-glue/common/{nor,fifo,coro}.c are linked into every core.
# Makes: gen_<id> (the generate step, unless NO_GEN), core_<id>_obj (OBJECT: the units, -O2, hidden, PIC), core_<id>
# (STATIC, for tests that link a core directly) and <id>_core (MODULE: build/cores/<id>.fm1core, exporting only
# fm1core_get); appends <id> to FM1_CORE_IDS (the bundles and the per-core tests take them from there) and records
# the glue folder, family and licence as the global properties FM1_CORE_GLUE_<id>, FM1_CORE_FAMILY_<id>,
# FM1_CORE_LICENSE_<id> (the conformance test reads <glue>/conformance.txt).
set(FM1_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/../core-glue/common)
get_filename_component(FM1_COMMON_DIR ${FM1_COMMON_DIR} ABSOLUTE)
set(FM1_COMMON_SOURCES ${FM1_COMMON_DIR}/nor.c ${FM1_COMMON_DIR}/fifo.c ${FM1_COMMON_DIR}/coro.c)
get_filename_component(FM1_CORE_API_DIR ${CMAKE_CURRENT_LIST_DIR}/../core-api ABSOLUTE)
set_source_files_properties(${FM1_COMMON_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-fno-common")
set(FM1_CORE_IDS "")

# PYTHON_NEEDS: one check per (python, need), cached
function(fm1_python_check id need)
    string(MAKE_C_IDENTIFIER "FM1_PY_HAS_${need}" var)
    if(NOT DEFINED ${var})
        if(need STREQUAL "raqm")
            set(code "from PIL import features; import sys; sys.exit(0 if features.check('raqm') else 1)")
        else()
            set(code "import ${need}")
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
                        ${CR_PYTHON} -c "${code}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
        if(rc EQUAL 0)
            set(${var} ON CACHE INTERNAL "")
        else()
            set(${var} OFF CACHE INTERNAL "")
        endif()
    endif()
    if(NOT ${var})
        message(FATAL_ERROR "${id} core: its generators need '${need}' in ${CR_PYTHON} "
                            "(raqm: Pillow built with libraqm, brew install libraqm; or -DCR_PYTHON=...)")
    endif()
endfunction()

function(fm1_add_core id glue)
    cmake_parse_arguments(A "NO_GEN;SUBMODULES_RECURSIVE"
        "SOURCE;UNIT;DESC;PREFIX;VERSION;VERSION_FILE;VERSION_REGEX;VERSION_GROUP;GEN_ROOT;GEN_PYTHON;FAMILY;LICENSE;GATED"
        "GEN;EXTRA_GEN;GEN_OUTPUTS;PYTHON_NEEDS;INCLUDES;DEFINES;FLAGS;EXTRA_SOURCES" ${ARGN})
    if(A_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "fm1_add_core(${id}): unknown arguments ${A_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT IS_ABSOLUTE ${glue})
        set(glue ${CMAKE_SOURCE_DIR}/${glue})
    endif()
    if(NOT A_FAMILY)
        set(A_FAMILY ${id})
    endif()
    if(NOT A_LICENSE)
        set(A_LICENSE "GPL-3.0-only")
    endif()
    if(A_GATED AND FM1_PUBLIC_BUILD)
        message(STATUS "${id} core: left out of this public build (gated: ${A_GATED})")
        return()
    endif()
    if(NOT A_SOURCE OR NOT A_UNIT OR NOT A_DESC)
        message(FATAL_ERROR "fm1_add_core(${id}): SOURCE, UNIT and DESC are required")
    endif()
    set(src ${A_SOURCE})
    file(GLOB _any ${src}/*)
    if(NOT _any)
        message(FATAL_ERROR "${src} is empty: run git submodule update --init --recursive")
    endif()
    # SUBMODULES_RECURSIVE: every submodule the firmware's own .gitmodules lists is checked out
    if(A_SUBMODULES_RECURSIVE)
        if(EXISTS ${src}/.gitmodules)
            file(STRINGS ${src}/.gitmodules _paths REGEX "^[ \t]*path[ \t]*=")
            foreach(_l IN LISTS _paths)
                string(REGEX REPLACE "^[ \t]*path[ \t]*=[ \t]*" "" _p "${_l}")
                string(STRIP "${_p}" _p)
                file(GLOB _sub ${src}/${_p}/*)
                if(NOT _sub)
                    message(FATAL_ERROR "${id} core: ${src}/${_p} (a submodule of the firmware) is empty: run "
                                        "git submodule update --init --recursive")
                endif()
            endforeach()
        else()
            message(WARNING "${id} core: SUBMODULES_RECURSIVE, but ${src} has no .gitmodules")
        endif()
    endif()

    # the generate step
    if(NOT A_GEN_ROOT)
        set(A_GEN_ROOT ".")
    endif()
    get_filename_component(groot ${src}/${A_GEN_ROOT} ABSOLUTE)
    set(gen ${groot}/build/gen)
    set(gen_headers "")
    if(NOT A_NO_GEN)
        if(NOT A_GEN)
            message(FATAL_ERROR "fm1_add_core(${id}): GEN <headers> or NO_GEN")
        endif()
        if(NOT EXISTS ${groot}/tools/build.py AND NOT A_GEN_PYTHON)
            message(FATAL_ERROR "${id} core: no ${groot}/tools/build.py (give GEN_PYTHON, or NO_GEN)")
        endif()
        foreach(_n IN LISTS A_PYTHON_NEEDS)
            fm1_python_check(${id} ${_n})
        endforeach()
        if(NOT A_GEN_PYTHON)
            set(A_GEN_PYTHON "__import__('build').generate()")
        endif()
        list(LENGTH A_GEN_PYTHON _n)
        if(_n GREATER 1)
            message(FATAL_ERROR "fm1_add_core(${id}): GEN_PYTHON must not contain ';' (a CMake list separator): one "
                                "expression, e.g. \"__import__('build').generate('app')\"")
        endif()
        list(TRANSFORM A_GEN PREPEND ${gen}/ OUTPUT_VARIABLE gen_headers)
        set(_outs ${A_GEN_OUTPUTS})
        list(TRANSFORM _outs PREPEND ${src}/)
        set(_env ${CMAKE_COMMAND} -E env DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib PYTHONPATH=${groot}/tools)
        set(_cmds COMMAND ${_env} ${CR_PYTHON} -c "${A_GEN_PYTHON}")
        foreach(_c IN LISTS A_EXTRA_GEN)
            separate_arguments(_args UNIX_COMMAND "${_c}")
            list(APPEND _cmds COMMAND ${_env} ${CR_PYTHON} ${_args})
        endforeach()
        file(GLOB gen_scripts CONFIGURE_DEPENDS ${groot}/tools/*.py)
        add_custom_command(
            OUTPUT ${gen_headers} ${_outs}
            ${_cmds}
            WORKING_DIRECTORY ${groot}
            DEPENDS ${gen_scripts}
            COMMENT "${id}: generating the build/gen headers"
            VERBATIM)
        add_custom_target(gen_${id} DEPENDS ${gen_headers} ${_outs})
        list(APPEND gen_headers ${_outs})
    endif()

    # the version (the firmware's own string) and the commit (the submodule's HEAD)
    set(ver "dev")
    if(DEFINED A_VERSION)
        set(ver "${A_VERSION}")
    elseif(A_VERSION_FILE)
        if(NOT A_VERSION_GROUP)
            set(A_VERSION_GROUP 1)
        endif()
        file(STRINGS ${src}/${A_VERSION_FILE} _v REGEX "${A_VERSION_REGEX}")
        if(_v)
            list(GET _v 0 _v)
            string(REGEX MATCH "${A_VERSION_REGEX}" _m "${_v}")
            if(_m)
                set(ver "${CMAKE_MATCH_${A_VERSION_GROUP}}")
            endif()
        endif()
    endif()
    execute_process(COMMAND git -C ${src} rev-parse HEAD OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT commit)
        set(commit "unknown")
    endif()
    set(_gn "")
    if(A_GATED)
        set(_gn ", gated: ${A_GATED}")
    endif()
    message(STATUS "${id} core: version ${ver}, commit ${commit}, family ${A_FAMILY}, ${A_LICENSE}${_gn}")

    set(defs FM1CORE_ID="${id}" FM1CORE_VERSION="${ver}" FM1CORE_COMMIT="${commit}" FM1CORE_FAMILY="${A_FAMILY}"
             FM1CORE_LICENSE="${A_LICENSE}")
    if(A_GATED)
        list(APPEND defs FM1CORE_GATED="${A_GATED}")
    endif()
    if(A_PREFIX)
        list(APPEND defs FM1CORE_${A_PREFIX}_VERSION="${ver}" FM1CORE_${A_PREFIX}_COMMIT="${commit}")
    endif()
    set(extra ${A_EXTRA_SOURCES})
    list(TRANSFORM extra PREPEND ${glue}/)
    set(obj core_${id}_obj)
    add_library(${obj} OBJECT ${glue}/${A_UNIT} ${glue}/${A_DESC} ${extra} ${FM1_COMMON_SOURCES})
    set(_inc ${gen})
    if(EXISTS ${src}/firmware/src)
        list(APPEND _inc ${src}/firmware/src)
    endif()
    target_include_directories(${obj} PRIVATE ${_inc} ${A_INCLUDES} ${FM1_CORE_API_DIR} ${FM1_COMMON_DIR})
    # (-fno-common: the large zero-initialised globals, the NOR image and the panel, go to bss, not __common, whose
    # alignment ld would reduce with a warning)
    target_compile_options(${obj} PRIVATE -O2 -fvisibility=hidden -fno-common)
    target_compile_definitions(${obj} PRIVATE ${defs} ${A_DEFINES})
    set_target_properties(${obj} PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
    if(NOT A_NO_GEN)
        add_dependencies(${obj} gen_${id})
    endif()
    # the firmware's own code: warnings off (as its host builds); the generated headers are included by the unit:
    # rebuild it when they change
    set_source_files_properties(${glue}/${A_UNIT} PROPERTIES
                                COMPILE_OPTIONS "-w;${A_FLAGS}" OBJECT_DEPENDS "${gen_headers}")
    set_source_files_properties(${glue}/${A_DESC} ${extra} PROPERTIES
                                COMPILE_OPTIONS "-Wall;-Wextra")

    add_library(core_${id} STATIC $<TARGET_OBJECTS:${obj}>)
    target_include_directories(core_${id} PUBLIC ${FM1_CORE_API_DIR})
    add_library(${id}_core MODULE $<TARGET_OBJECTS:${obj}>)
    set_target_properties(${id}_core PROPERTIES PREFIX "" SUFFIX ".fm1core" OUTPUT_NAME ${id}
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cores)
    target_link_options(${id}_core PRIVATE -Wl,-exported_symbol,_fm1core_get)
    if(MATH_LIB)
        target_link_libraries(core_${id} PUBLIC ${MATH_LIB})
        target_link_libraries(${id}_core PRIVATE ${MATH_LIB})
    endif()
    set_property(GLOBAL PROPERTY FM1_CORE_GLUE_${id} ${glue})
    set_property(GLOBAL PROPERTY FM1_CORE_FAMILY_${id} ${A_FAMILY})
    set_property(GLOBAL PROPERTY FM1_CORE_LICENSE_${id} ${A_LICENSE})
    set(FM1_CORE_IDS ${FM1_CORE_IDS} ${id} PARENT_SCOPE)
endfunction()
