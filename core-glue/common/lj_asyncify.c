/* SPDX-License-Identifier: GPL-3.0-only */
/* Lua's LUAI_THROW / LUAI_TRY on Binaryen Asyncify (lj_asyncify.h), unwind only: no rewind, no JavaScript, no wasm
 * exceptions. Compiled by cmake/FM1Rfm1.cmake for every .rfm1 and linked only into a module whose units call
 * lj_throw (tools/rfm1/rfm1_link.cmake); never into a native core (natively Lua keeps its setjmp).
 *
 *   lj_try(b, L, f, ud)  -> lj_try_impl through a pointer (never inlined: its name survives to wasm-opt's
 *                           removelist): records the C stack pointer, calls f(L, ud). If f comes back with the
 *                           Asyncify state "unwinding" and this try is the target, it stops the unwind, puts the
 *                           stack pointer back and returns (luaD_rawrunprotected then reads lj.status, set by
 *                           luaD_throw). If this try is not the target (luaD_throw re-throwing to an outer try), it
 *                           just returns, still unwinding: its instrumented callers keep unwinding.
 *   lj_throw(b)          -> lj_throw_impl through a pointer: the function that calls start_unwind, which Asyncify
 *                           leaves uninstrumented (it is "the runtime"): it empties the buffer and starts the unwind;
 *                           every instrumented frame between it and lj_try_impl saves its locals into the buffer and
 *                           returns at once. The buffer is never read back (no rewind).
 *
 * The buffer: 64 KiB holds the deepest unwind at LUAI_MAXCCALLS 80 (the spike's measure, with room). A deeper one
 * would overflow it: Asyncify then traps (unreachable), it never writes past it. */
#include <stdint.h>
#include <emscripten/stack.h>
#include "lj_asyncify.h"

void _emscripten_stack_restore(uintptr_t sp);

#define LJ_AS(n) __attribute__((import_module("asyncify"), import_name(#n)))
LJ_AS(start_unwind) void lj_as_start_unwind(void *data);
LJ_AS(stop_unwind) void lj_as_stop_unwind(void);

#define LJ_DATA (64u * 1024u)
static uint32_t lj_data[LJ_DATA / 4u];
static lj_buf *lj_target;
static int lj_unwinding;

__attribute__((noinline)) void lj_try_impl(lj_buf *b, void *L, void (*f)(void *, void *), void *ud)
{
    b->sp = emscripten_stack_get_current();
    f(L, ud);
    if (lj_unwinding && lj_target == b) {
        lj_unwinding = 0;
        lj_as_stop_unwind();
        _emscripten_stack_restore(b->sp);
    }
}

__attribute__((noinline)) void lj_throw_impl(lj_buf *b)
{
    lj_target = b;
    lj_data[0] = (uint32_t)(uintptr_t)(lj_data + 2);
    lj_data[1] = (uint32_t)(uintptr_t)lj_data + LJ_DATA;
    lj_unwinding = 1;
    lj_as_start_unwind(lj_data);
}

static void (*volatile lj_try_p)(lj_buf *, void *, void (*)(void *, void *), void *) = lj_try_impl;
static void (*volatile lj_throw_p)(lj_buf *) = lj_throw_impl;
void lj_try(lj_buf *b, void *L, void (*f)(void *, void *), void *ud) { lj_try_p(b, L, f, ud); }
void lj_throw(lj_buf *b) { lj_throw_p(b); }
