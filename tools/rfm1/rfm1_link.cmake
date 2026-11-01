# SPDX-License-Identifier: GPL-3.0-only
# The link of one .rfm1 module (cmake/FM1Rfm1.cmake fm1_add_rfm1): cmake -DCONF=<wdir>/link.cmake -P rfm1_link.cmake.
# CONF sets EMCC, EMNM, WASM_OPT, LINK_ARGS (emcc's options before the objects), OBJS, LINK_TAIL (after them), CORO_OBJ
# (core-glue/common/coro_asyncify.c's object), LJ_OBJ (core-glue/common/lj_asyncify.c's), WASM_OPT_FEATURES
# (wasm-opt's names of the build's features), OPT (the -O) and OUT.
#
# A core whose units call neither fm1coro_* nor lj_throw: emcc LINK_ARGS OBJS LINK_TAIL -o OUT, nothing else.
# A core whose units call fm1coro_* (a firmware main loop that never returns: Dinghy, Melodee, Tonitor, ..) and/or
# lj_throw (Lua's errors: Ripple): the same link plus CORO_OBJ and/or LJ_OBJ and --profiling-funcs (the names, for the
# removelist), then wasm-opt --asyncify:
#   asyncify-ignore-imports       no WASI or rfm1_host_1 import unwinds; the asyncify.* calls coro_asyncify.c and
#                                 lj_asyncify.c make are the pass's own (start_unwind is where an unwind starts: only
#                                 what can reach it is instrumented)
#   asyncify-removelist@fm1coro_rfm1_switch   the resumer: an unwind stops there, a rewind starts there
#   asyncify-removelist@lj_try_impl           Lua's try: a throw's unwind stops there (it never rewinds)
#   --remove-exports=asyncify_*   the API the pass exports (the module calls it itself: nothing outside may)
# then OPT and --strip-debug (the names). A warning of wasm-opt (a removelist name that does not exist:
# the resumer inlined or renamed) fails the link, and so does any "asyncify" left in the module.
include(${CONF})

execute_process(COMMAND ${EMNM} --undefined-only ${OBJS} OUTPUT_VARIABLE _nm ERROR_VARIABLE _nm_err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rfm1 link: ${EMNM} failed (${rc}): ${_nm_err}")
endif()
set(_extra "")
set(_remove "")
if(_nm MATCHES "U fm1coro_")
    list(APPEND _extra ${CORO_OBJ})
    list(APPEND _remove fm1coro_rfm1_switch)
    message(STATUS "rfm1 link: the coroutine (fm1coro_*): coro_asyncify.c, wasm-opt --asyncify")
endif()
if(_nm MATCHES "U lj_throw")
    list(APPEND _extra ${LJ_OBJ})
    list(APPEND _remove lj_try_impl)
    message(STATUS "rfm1 link: Lua's errors (lj_throw): lj_asyncify.c, wasm-opt --asyncify")
endif()
if(NOT _extra)
    execute_process(COMMAND ${EMCC} ${LINK_ARGS} ${OBJS} ${LINK_TAIL} -o ${OUT} RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "rfm1 link: emcc failed (${rc})")
    endif()
    return()
endif()

string(REPLACE ";" "," _remove "${_remove}")
set(_pre ${OUT}.pre.wasm)
execute_process(COMMAND ${EMCC} ${LINK_ARGS} ${OBJS} ${_extra} ${LINK_TAIL} --profiling-funcs -o ${_pre}
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rfm1 link: emcc failed (${rc})")
endif()
execute_process(COMMAND ${WASM_OPT} ${_pre} ${WASM_OPT_FEATURES}
                        --asyncify --pass-arg=asyncify-ignore-imports
                        --pass-arg=asyncify-removelist@${_remove}
                        --remove-exports=asyncify_*
                        ${OPT} --strip-debug -o ${OUT}
                RESULT_VARIABLE rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT rc EQUAL 0 OR _out MATCHES "[Ww]arning" OR _err MATCHES "[Ww]arning")
    file(REMOVE ${OUT})
    message(FATAL_ERROR "rfm1 link: wasm-opt --asyncify failed (${rc}) or warned:\n${_out}${_err}")
endif()
file(STRINGS ${OUT} _left REGEX "asyncify")
if(_left)
    file(REMOVE ${OUT})
    message(FATAL_ERROR "rfm1 link: \"asyncify\" still in ${OUT} (an import or export the pass left): ${_left}")
endif()
file(REMOVE ${_pre})
