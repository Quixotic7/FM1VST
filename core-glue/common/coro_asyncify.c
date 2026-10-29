/* SPDX-License-Identifier: GPL-3.0-only */
/* The coroutine (coro.h) inside an .rfm1 module: the WebAssembly build's replacement for coro.c (ucontext), linked by
 * fm1_add_rfm1 (cmake/FM1Rfm1.cmake) into the cores whose units call fm1coro_*; never into a native core. Same API,
 * same rules: one level, the coroutine runs only inside fm1coro_resume(), on the caller's thread.
 *
 * Implementation: Binaryen's Asyncify, driven from inside the module (an .rfm1 has no JavaScript and imports only
 * WASI). The four asyncify.* imports below are not real imports: wasm-opt --asyncify turns their calls into calls of
 * the functions it adds, so the module that comes out imports none of them (tools/rfm1/rfm1_link.cmake also removes
 * the asyncify_* exports the pass adds). Asyncify has no in-module get_state, so the state is kept here (state).
 *
 *   fm1coro_resume(c)   checks, then calls fm1coro_rfm1_switch through a pointer (no optimiser can inline it into its
 *                       caller, so its name is still there when wasm-opt runs).
 *   fm1coro_rfm1_switch NOT instrumented (the pass's removelist names it): it saves the caller's C stack pointer,
 *                       moves it to the coroutine's (its own stack the first time, else where the last yield left it,
 *                       then start_rewind), and calls fn through a pointer. When that call returns, the coroutine has
 *                       yielded (unwinding: stop_unwind) or finished; the caller's stack pointer is put back. The
 *                       frames of the resumer and above are never unwound: only those between it and the yield.
 *   fm1coro_yield()     calls fm1coro_rfm1_yield through a pointer, which records the coroutine's stack pointer,
 *                       empties the buffer and starts the unwind. Asyncify leaves the function that calls start_unwind
 *                       uninstrumented, as it would an import: the rewind calls it again, it stops the rewind and
 *                       returns, and the coroutine goes on. Through a pointer, it is never inlined into a caller.
 *   fm1coro_park()      (fm1coro_rfm1_park, the same way) marks it done, then unwinds as a yield that is never rewound
 *                       (exit() inside the firmware).
 *
 * Asyncify restores the wasm locals of the rewound frames, not the __stack_pointer global (their prologues are
 * skipped), hence the stack pointer the yield records and the rewind puts back. The Asyncify buffer (the locals saved
 * by an unwind) is FM1CORO_RFM1_DATA bytes per coroutine; both it and the stack are malloc'ed inside the module's
 * fixed memory. Only the functions that can reach a yield are instrumented (wasm-opt's own analysis from the yield
 * and park, through direct and indirect calls). The functions here that move the stack pointer have no shadow-stack
 * frame of their own (no address-taken locals). */
#include "coro.h"

#include <emscripten/stack.h>
#include <stdlib.h>
#include <string.h>

/* (Emscripten's stack_ops.S: sets the C stack pointer, __stack_pointer) */
void _emscripten_stack_restore(uintptr_t sp);

#define FM1CORO_AS(n) __attribute__((import_module("asyncify"), import_name(#n)))
FM1CORO_AS(start_unwind) void fm1coro_as_start_unwind(void *data);
FM1CORO_AS(stop_unwind) void fm1coro_as_stop_unwind(void);
FM1CORO_AS(start_rewind) void fm1coro_as_start_rewind(void *data);
FM1CORO_AS(stop_rewind) void fm1coro_as_stop_rewind(void);

#define FM1CORO_RFM1_DATA (64u * 1024u)
#define FILL 0xA5u

enum { AS_NORMAL, AS_UNWINDING, AS_REWINDING };   /* (Asyncify's own numbering) */
static int fm1coro_as_state;

struct fm1coro {
    void (*fn)(void *);
    void *arg;
    uint8_t *stack;                     /* the coroutine's C stack (grows down from stack + stack_len) */
    size_t stack_len;
    uintptr_t sp;                       /* its stack pointer at the last yield */
    int started, done;
    uint32_t resumes;
    uint32_t *data;                     /* the Asyncify buffer: {current, end}, then the saved locals */
};

static fm1coro_t *cur;                  /* the coroutine running now, or NULL */

fm1coro_t *fm1coro_new(void (*fn)(void *), void *arg, size_t stack_bytes)
{
    fm1coro_t *c;
    if (!fn)
        return NULL;
    if (!stack_bytes)
        stack_bytes = FM1CORO_STACK;
    stack_bytes = (stack_bytes + 15u) & ~(size_t)15u;
    c = (fm1coro_t *)calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->stack = (uint8_t *)malloc(stack_bytes);
    c->data = (uint32_t *)malloc(FM1CORO_RFM1_DATA);
    if (!c->stack || !c->data) {
        free(c->stack);
        free(c->data);
        free(c);
        return NULL;
    }
    memset(c->stack, FILL, stack_bytes);
    c->stack_len = stack_bytes;
    c->fn = fn;
    c->arg = arg;
    return c;
}

/* the Asyncify buffer emptied before an unwind (a rewind reads it as the unwind left it) */
static void fm1coro_data_reset(fm1coro_t *c)
{
    c->data[0] = (uint32_t)(uintptr_t)(c->data + 2);
    c->data[1] = (uint32_t)((uintptr_t)c->data + FM1CORO_RFM1_DATA);
}

/* the switch: in wasm-opt's removelist (FM1Rfm1.cmake), so it is where an unwind stops and a rewind starts. After
 * start_rewind the next call must be the coroutine's own (an instrumented call in between would rewind itself) */
__attribute__((noinline)) int fm1coro_rfm1_switch(fm1coro_t *c)
{
    void (*fn)(void *) = c->fn;
    void *arg = c->arg;
    const uintptr_t back = emscripten_stack_get_current();
    c->resumes++;
    cur = c;
    if (c->started) {
        _emscripten_stack_restore(c->sp);           /* (where the rewound frames left it) */
        fm1coro_as_state = AS_REWINDING;
        fm1coro_as_start_rewind(c->data);
    } else {
        c->started = 1;
        _emscripten_stack_restore((uintptr_t)(c->stack + c->stack_len));
    }
    fn(arg);
    if (fm1coro_as_state == AS_UNWINDING) {         /* it yielded (or parked) */
        fm1coro_as_state = AS_NORMAL;
        fm1coro_as_stop_unwind();
    } else {
        c->done = 1;                                /* fn returned */
    }
    cur = NULL;
    _emscripten_stack_restore(back);
    return c->done ? 0 : 1;
}

/* (a pointer: the switch is never inlined into its caller, its name stays for the removelist) */
static int (*volatile fm1coro_rfm1_switch_p)(fm1coro_t *) = fm1coro_rfm1_switch;

int fm1coro_resume(fm1coro_t *c)
{
    if (cur)
        return -1;
    if (!c || c->done)
        return 0;
    return fm1coro_rfm1_switch_p(c);
}

/* the yield and the park: the functions that call start_unwind. Asyncify does not instrument them (to the pass they
 * are the runtime, as an import would be): a rewind calls the yield again, which stops the rewind and returns. So they
 * must stay functions of their own, reached through pointers: inlined into a caller, that caller would be the one left
 * uninstrumented, and a rewind would run it again from its start */
__attribute__((noinline)) void fm1coro_rfm1_yield(void)
{
    fm1coro_t *c;
    if (fm1coro_as_state == AS_REWINDING) {         /* called again by the rewind: the coroutine goes on */
        fm1coro_as_state = AS_NORMAL;
        fm1coro_as_stop_rewind();
        return;
    }
    c = cur;
    if (!c)
        return;
    c->sp = emscripten_stack_get_current();
    fm1coro_data_reset(c);
    fm1coro_as_state = AS_UNWINDING;
    fm1coro_as_start_unwind(c->data);               /* (unwinds: its callers return, up to the switch) */
}

__attribute__((noinline)) void fm1coro_rfm1_park(void)
{
    fm1coro_t *c = cur;
    if (!c)
        return;
    c->done = 1;
    c->sp = emscripten_stack_get_current();
    fm1coro_data_reset(c);
    fm1coro_as_state = AS_UNWINDING;
    fm1coro_as_start_unwind(c->data);               /* (unwinds for good: never rewound) */
}

static void (*volatile fm1coro_rfm1_yield_p)(void) = fm1coro_rfm1_yield;
static void (*volatile fm1coro_rfm1_park_p)(void) = fm1coro_rfm1_park;
void fm1coro_yield(void) { fm1coro_rfm1_yield_p(); }
void fm1coro_park(void) { fm1coro_rfm1_park_p(); }

int fm1coro_inside(void) { return cur != NULL; }
int fm1coro_done(const fm1coro_t *c) { return c ? c->done : 1; }
uint32_t fm1coro_resumes(const fm1coro_t *c) { return c ? c->resumes : 0; }

size_t fm1coro_stack_peak(const fm1coro_t *c)
{
    size_t i;
    if (!c || !c->started)
        return 0;
    for (i = 0; i < c->stack_len && c->stack[i] == FILL; i++)
        ;
    return c->stack_len - i;
}

void fm1coro_free(fm1coro_t *c)
{
    if (!c || c == cur)
        return;
    free(c->stack);
    free(c->data);
    free(c);
}
