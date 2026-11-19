/* SPDX-License-Identifier: MPL-2.0 */
/* Core 1 in an .rfm1 (docs/RFM1.md "cpu2"): a firmware's second-core job, handed to a host thread where the host has
 * one, else run by the caller as on a unit whose core 1 is off. For a core's rfm1 back end (its fm1_cpu1_run /
 * plat_cpu2_run in the host build); FM1CORE_RFM1_CPU2 is defined for every unit of an .rfm1 built with
 * fm1_add_rfm1(... WANTS cpu2). Everywhere else (native cores, the FM-1, an .rfm1 without cpu2) these are the
 * one-core answers, inline, so a caller needs no #if.
 *
 * The FM-1's rules hold: one job at a time; between run and wait the caller leaves the job's data alone; the job may
 * not hand a job on (its run answers 0); a job writes no state its caller reads before the wait. The samples must not
 * depend on which side ran the job (the host may run it on either). The job runs on its own stack: code that keeps
 * scratch per core asks rfm1_cpu2_core().
 *
 *   rfm1_cpu2_run(fn, arg)  1: handed over (core 1 runs fn(arg) now); 0: not, run it yourself
 *   rfm1_cpu2_wait()        after a run that answered 1: 0 the job is done; else it was not (run it yourself)
 *   rfm1_cpu2_core()        1 inside a job core 1 runs, 0 elsewhere */
#ifndef RFM1_CPU2_H
#define RFM1_CPU2_H
#include <stdint.h>

#ifdef FM1CORE_RFM1_CPU2
int rfm1_cpu2_run(void (*fn)(uint32_t), uint32_t arg);
int rfm1_cpu2_wait(void);
int rfm1_cpu2_core(void);
#else
static inline int rfm1_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
static inline int rfm1_cpu2_wait(void) { return -1; }
static inline int rfm1_cpu2_core(void) { return 0; }
#endif

#endif
