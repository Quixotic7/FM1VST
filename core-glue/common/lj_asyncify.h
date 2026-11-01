/* SPDX-License-Identifier: GPL-3.0-only */
/* Lua's errors in an .rfm1 (FM1CORE_RFM1): LUAI_THROW / LUAI_TRY on Binaryen's Asyncify, unwind only, so a module
 * needs neither setjmp/longjmp (wasm exceptions, which the format forbids) nor JavaScript. Included last by a Lua
 * port's configuration header (luaconf.h's local part; ripple: core-glue/ripple's luaport.h overlay) in .rfm1 builds;
 * lj_asyncify.c is the code, linked by tools/rfm1/rfm1_link.cmake when a unit calls lj_throw (the link then runs
 * wasm-opt --asyncify with lj_try_impl in the removelist).
 *
 * The rule that keeps it sound: a throw unwinds every frame between it and its try, and nothing ever rewinds. So no
 * other Asyncify user (core-glue/common/coro_asyncify.c's fm1coro) may unwind while Lua frames are on the stack:
 * a core that has both must never yield an fm1coro inside Lua. */
#ifndef FM1_LJ_ASYNCIFY_H
#define FM1_LJ_ASYNCIFY_H

/* the "jump buffer": the C stack pointer at the try (put back after the unwind) */
typedef struct {
    unsigned long sp;
} lj_buf;
void lj_throw(lj_buf *b);
void lj_try(lj_buf *b, void *L, void (*f)(void *, void *), void *ud);

#undef LUAI_THROW
#undef LUAI_TRY
#undef luai_jmpbuf
#define luai_jmpbuf lj_buf
#define LUAI_THROW(L, c) lj_throw(&(c)->b)
/* LUAI_TRY's only use is luaD_rawrunprotected (Lua 5.4), whose body `a` is `(*f)(L, ud);`: the try calls f itself,
 * in a function of its own that Asyncify leaves uninstrumented (the removelist), where the unwind stops */
#define LUAI_TRY(L, c, a) lj_try(&(c)->b, (void *)(L), (void (*)(void *, void *))f, ud);

#endif
