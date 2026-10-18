/* SPDX-License-Identifier: GPL-3.0-only */
/* core-glue/common/coro: a main loop that never returns, run as a coroutine and resumed once per "device ms", as a
 * Dinghy glue will drive app_main: yields from deep inside its call stack (a poll inside a delay inside the loop),
 * locals and floating point kept across switches, two coroutines interleaved (two loaded cores are two modules, but
 * one module may run more than one), a coroutine that returns, fm1coro_park (exit() inside the firmware), resume
 * refused from inside, the stack's high-water mark, and the cost of a switch. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "coro.h"

static int fails;
static void check(int c, const char *what)
{
    printf("  %s  %s\n", c ? "ok  " : "FAIL", what);
    fails += !c;
}

/* ---- a firmware: app_main() loops forever; platform_poll() is one device ms; delay_ms() polls */
static unsigned fw_ms;          /* the "device ms" the glue gives each resume */
static unsigned polls, passes;
static double acc;
static int depth_seen, refused_inside;
static fm1coro_t *self_co;

static void platform_poll(void)
{
    polls++;
    fm1coro_yield();            /* back to the glue's tick: the next resume is the next ms */
}
static void delay_ms(unsigned n)
{
    const unsigned t0 = fw_ms;
    while (fw_ms - t0 < n)
        platform_poll();
}
/* recursion: a big stack frame chain, yielding at the bottom. Each frame's pad escapes (the empty asm takes its
 * address and clobbers memory), so an optimised build (Release: -O3) keeps 200 real frames: it can neither drop the
 * pad nor turn the recursion into a loop (tail-recursion elimination with an accumulator) */
__attribute__((noinline)) static int deep(int n)
{
    char pad[256];
    int r;
    memset(pad, n, sizeof pad);
    __asm__ volatile("" : : "r"(pad) : "memory");
    if (n == 0) {
        depth_seen = 1;
        platform_poll();
        return pad[0];
    }
    r = deep(n - 1) + pad[1];
    __asm__ volatile("" : : "r"(pad) : "memory");
    return r;
}
static void app_main(void *arg)
{
    double x = 1.0;
    (void)arg;
    refused_inside = fm1coro_resume(self_co) == -1;
    for (;;) {
        passes++;
        x = x * 1.0000001 + 0.5;           /* (a callee-saved float register across the switch) */
        acc = x;
        if (passes == 10)
            (void)deep(200);
        if (passes % 100 == 0)
            delay_ms(5);
        platform_poll();
    }
}

/* ---- a second, finite one */
static int seq_out[16], seq_n;
static void counter(void *arg)
{
    int i, base = *(int *)arg;
    for (i = 0; i < 5; i++) {
        seq_out[seq_n++] = base + i;
        fm1coro_yield();
    }
}

/* ---- exit() inside: parked */
static int after_park;
static void exits(void *arg)
{
    (void)arg;
    fm1coro_yield();
    fm1coro_park();             /* (exit.h's fm1core_exit_park) */
    after_park = 1;             /* never reached */
}

int main(void)
{
    {
        fm1coro_t *c = fm1coro_new(app_main, NULL, 0);
        double ref = 1.0;
        unsigned ms, ok = 1, i;
        self_co = c;
        check(c != NULL && !fm1coro_inside(), "a coroutine made (512 KiB stack, guard page)");
        for (ms = 0; ms < 5000 && ok; ms++) {
            fw_ms = ms;
            ok = fm1coro_resume(c) == 1 && !fm1coro_inside();
        }
        for (i = 0; i < passes; i++)
            ref = ref * 1.0000001 + 0.5;
        check(ok, "5000 resumes, each returned after one yield");
        check(polls == 5000, "one poll per resume (5000)");
        /* every 100th pass waits 5 ms: 5 polls in the delay + 1 at the end of the pass */
        check(passes > 0 && passes < 5000 && fabs(acc - ref) < 1e-9,
              "the loop's state and floating point kept across switches");
        check(depth_seen, "a yield from 200 frames deep came back");
        check(refused_inside, "resume from inside a coroutine refused (-1)");
        printf("        (stack peak %zu bytes)\n", fm1coro_stack_peak(c));
        check(fm1coro_stack_peak(c) > 200 * 256 && fm1coro_stack_peak(c) < FM1CORO_STACK,
              "the stack's high-water mark (deep recursion seen)");
        {
            struct timespec a, b;
            const int N = 20000;
            double us;
            clock_gettime(CLOCK_MONOTONIC, &a);
            for (i = 0; i < (unsigned)N; i++) {
                fw_ms++;
                fm1coro_resume(c);
            }
            clock_gettime(CLOCK_MONOTONIC, &b);
            us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3 / N;
            printf("        (a resume + yield: %.2f us)\n", us);
            check(us < 50.0, "a switch pair is cheap (< 50 us)");
        }
        fm1coro_free(c);            /* (never finished: freed as it stands) */
    }
    {
        int a = 100, b = 200, k;
        fm1coro_t *x = fm1coro_new(counter, &a, 64 * 1024), *y = fm1coro_new(counter, &b, 64 * 1024);
        int rx = 1, ry = 1;
        for (k = 0; k < 4; k++) {
            rx = fm1coro_resume(x);
            ry = fm1coro_resume(y);
        }
        check(rx == 1 && ry == 1 && seq_n == 8 && seq_out[0] == 100 && seq_out[1] == 200 && seq_out[6] == 103 &&
                  seq_out[7] == 203,
              "two coroutines interleaved, each with its own stack");
        fm1coro_resume(x);          /* the fifth value */
        rx = fm1coro_resume(x);     /* the loop ends: fn returns */
        check(rx == 0 && fm1coro_done(x) && fm1coro_resume(x) == 0, "a coroutine that returns: done, not resumed");
        fm1coro_free(x);
        fm1coro_free(y);
    }
    {
        fm1coro_t *z = fm1coro_new(exits, NULL, 0);
        int r1 = fm1coro_resume(z), r2 = fm1coro_resume(z), r3 = fm1coro_resume(z);
        printf("        (park: %d %d %d after %d done %d)\n", r1, r2, r3, after_park, fm1coro_done(z));
        check(r1 == 1 && r2 == 0 && r3 == 0 && !after_park && fm1coro_done(z),
              "fm1coro_park: finished for good where it stood");
        fm1coro_free(z);
    }
    fm1coro_yield();
    fm1coro_park();
    check(1, "yield / park outside a coroutine: no-ops");
    printf("coro_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
