/* SPDX-License-Identifier: GPL-3.0-only */
/* A coroutine for firmwares whose main loop never returns (core-glue/common/README.md "coro"; the Dinghy family:
 * app_main() loops forever and calls platform_poll() every pass). The firmware's main runs on a stack of its own;
 * where the device would wait (its poll, its delay loops) the glue calls fm1coro_yield(), which returns to whoever
 * resumed it; the glue resumes it once per device millisecond from the ABI's clock (tick, or frame / idle). No OS
 * thread is involved (the ABI's threading contract): the coroutine runs only inside fm1coro_resume(), on the
 * caller's thread, so it may be resumed from different threads as long as the calls are serialised.
 *
 *     static fm1coro_t *co;
 *     static void main_entry(void *arg) { (void)arg; app_main(); }
 *     init():   co = fm1coro_new(main_entry, NULL, 0); fm1coro_resume(co);   (runs the boot up to its first poll)
 *     tick():   ..the ms's input..; fm1coro_resume(co);                       (one pass of the main loop)
 *     platform_poll() / delay_ms() in the glue:  fm1coro_yield();            (a delay: yield until fm1t_ms() is due)
 *     exit():   exit.h's fm1core_exit_park = fm1coro_park;                   (exit() inside: never resumed again)
 *     shutdown / module unload: fm1coro_free(co);
 *
 * Implementation: ucontext (makecontext / swapcontext; deprecated on macOS but working on arm64 and x86_64; the unit
 * defines _XOPEN_SOURCE so ucontext_t has its full size). The stack is mmap'ed with a PROT_NONE guard page below it
 * (an overflow faults instead of corrupting memory); default FM1CORO_STACK. A switch costs about a microsecond (the
 * signal mask is saved): two per device ms is noise. Only one level: resume from outside any coroutine. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM1CORO_STACK (512u * 1024u)

typedef struct fm1coro fm1coro_t;

/* a coroutine that will run fn(arg) on its own stack (stack_bytes 0: FM1CORO_STACK); not started yet. NULL: no
 * memory */
fm1coro_t *fm1coro_new(void (*fn)(void *), void *arg, size_t stack_bytes);
/* run it until it yields, returns or parks. 1: it yielded (resume again later); 0: it has finished (fn returned or
 * fm1coro_park), nothing ran; -1: called from inside a coroutine (not allowed) */
int fm1coro_resume(fm1coro_t *c);
/* inside a coroutine: back to the fm1coro_resume() that ran it (which returns 1). Outside: does nothing */
void fm1coro_yield(void);
/* inside: finish for good (fm1coro_resume returns 0, now and later); the stack is abandoned where it is (exit()).
 * Outside: does nothing (returns) */
void fm1coro_park(void);
int fm1coro_inside(void);                  /* 1 while running on a coroutine's stack */
int fm1coro_done(const fm1coro_t *c);      /* 1 once it finished */
uint32_t fm1coro_resumes(const fm1coro_t *c);
size_t fm1coro_stack_peak(const fm1coro_t *c);   /* bytes of its stack ever touched (high-water mark) */
void fm1coro_free(fm1coro_t *c);           /* from outside; also for a coroutine that never finished */

#ifdef __cplusplus
}
#endif
