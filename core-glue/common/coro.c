/* SPDX-License-Identifier: GPL-3.0-only */
/* The coroutine (coro.h), on ucontext. _XOPEN_SOURCE before any header: without it macOS's ucontext_t lacks the
 * machine context its functions write (a smaller struct, memory overwritten). */
#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1                      /* (MAP_ANON) */
#endif
#include "coro.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#define FILL 0xA5u

struct fm1coro {
    ucontext_t ctx, back;               /* the coroutine's; the resumer's */
    void (*fn)(void *);
    void *arg;
    uint8_t *map;                       /* the mapping: guard page + stack */
    size_t map_len, page, stack_len;
    int started, done;
    uint32_t resumes;
};

static fm1coro_t *cur;                  /* the coroutine running now, or NULL */

static void trampoline(void)
{
    fm1coro_t *c = cur;
    c->fn(c->arg);
    c->done = 1;
    cur = NULL;
    setcontext(&c->back);               /* (never returns) */
}

fm1coro_t *fm1coro_new(void (*fn)(void *), void *arg, size_t stack_bytes)
{
    fm1coro_t *c;
    long pg = sysconf(_SC_PAGESIZE);
    size_t page = pg > 0 ? (size_t)pg : 16384u;
    if (!fn)
        return NULL;
    if (!stack_bytes)
        stack_bytes = FM1CORO_STACK;
    stack_bytes = (stack_bytes + page - 1u) / page * page;
    c = (fm1coro_t *)calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->map_len = stack_bytes + page;
    c->map = (uint8_t *)mmap(NULL, c->map_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (c->map == (uint8_t *)MAP_FAILED) {
        free(c);
        return NULL;
    }
    mprotect(c->map, page, PROT_NONE);  /* the guard page: the stack grows down into it */
    c->page = page;
    c->stack_len = stack_bytes;
    memset(c->map + page, FILL, stack_bytes);
    c->fn = fn;
    c->arg = arg;
    return c;
}

int fm1coro_resume(fm1coro_t *c)
{
    if (cur)
        return -1;
    if (!c || c->done)
        return 0;
    if (!c->started) {
        c->started = 1;
        getcontext(&c->ctx);
        c->ctx.uc_stack.ss_sp = c->map + c->page;
        c->ctx.uc_stack.ss_size = c->stack_len;
        c->ctx.uc_link = NULL;
        makecontext(&c->ctx, trampoline, 0);
    }
    c->resumes++;
    cur = c;
    swapcontext(&c->back, &c->ctx);
    cur = NULL;
    return c->done ? 0 : 1;
}

void fm1coro_yield(void)
{
    fm1coro_t *c = cur;
    if (!c)
        return;
    cur = NULL;
    swapcontext(&c->ctx, &c->back);
    cur = c;                            /* (resumed: fm1coro_resume set it too) */
}

void fm1coro_park(void)
{
    fm1coro_t *c = cur;
    if (!c)
        return;
    c->done = 1;
    cur = NULL;
    setcontext(&c->back);               /* (never returns: the stack stays as it was) */
}

int fm1coro_inside(void) { return cur != NULL; }
int fm1coro_done(const fm1coro_t *c) { return c ? c->done : 1; }
uint32_t fm1coro_resumes(const fm1coro_t *c) { return c ? c->resumes : 0; }

size_t fm1coro_stack_peak(const fm1coro_t *c)
{
    size_t i;
    if (!c)
        return 0;
    for (i = 0; i < c->stack_len && c->map[c->page + i] == FILL; i++)
        ;
    return c->stack_len - i;
}

void fm1coro_free(fm1coro_t *c)
{
    if (!c || c == cur)
        return;
    munmap(c->map, c->map_len);
    free(c);
}
