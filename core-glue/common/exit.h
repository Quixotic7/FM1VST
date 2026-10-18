/* SPDX-License-Identifier: GPL-3.0-only */
/* exit() and atexit() inside a plugin (core-glue/common/README.md). A firmware ends its "process" where the device
 * resets (UBOOT, a reboot after an erase, a fatal error) and registers its save-at-exit with atexit(). In a host
 * neither may touch the process, so the firmware unit includes this header BEFORE the firmware's sources and both
 * names are redirected for that unit only:
 *
 *   exit(code)  -> fm1core_exit: runs the registered handlers once (last first), then the glue's cleanup hook (close
 *                  the flash file: fm1core_exit_cleanup), records the code, sets the halted flag and RETURNS. The
 *                  descriptor's wrappers turn the clock into no-ops from then on (fm1core_halted(): audio silence).
 *                  If the glue set fm1core_exit_park (a coroutine glue: coro.h fm1coro_park), it is called last and
 *                  never returns to the firmware: the firmware's stack is abandoned at the exit() call.
 *   atexit(fn)  -> fm1core_atexit: keeps fn (up to FM1CORE_NATEXIT) for fm1core_run_exit (the descriptor's
 *                  shutdown). Ignored once halted.
 *
 * Usage (the firmware unit):
 *     #include <stdio.h>
 *     #include <stdlib.h>          (exit.h includes it too: its prototypes must not see the macros)
 *     #include "exit.h"            (defines the two macros)
 *     #include "<the firmware>.c"
 *     #include "exit_end.h"        (undefines them)
 *     ... static void my_cleanup(void) {..}  and, before init:  fm1core_exit_cleanup = my_cleanup;
 *
 * PITFALL: exit() now returns. A call site written as noreturn (`exit(1); for (;;);`, or code that relies on not
 * coming back) runs on: hang loops spin forever in the clock thread. Check every exit() in the firmware; for a
 * coroutine glue set fm1core_exit_park; otherwise wrap the site (a glue-side macro) so it returns at once.
 * Header-only: its state is static, once per firmware unit (one per module). */
#pragma once
#include <stdio.h>
#include <stdlib.h>

#ifndef FM1CORE_NATEXIT
#define FM1CORE_NATEXIT 8
#endif

static void (*fm1core_atexit_fn[FM1CORE_NATEXIT])(void);
static int fm1core_natexit;
static int fm1core_halt;                  /* 0 running; 0x100 | code once the firmware exited */
static int fm1core_down;                  /* the exit handlers ran (exit or shutdown) */
static void (*fm1core_exit_cleanup)(void);   /* the glue's: after the handlers (close the flash file, ..) */
static void (*fm1core_exit_park)(void);   /* the glue's: never returns (a coroutine glue parks the firmware) */

static int fm1core_atexit(void (*fn)(void))
{
    if (fm1core_halt || fm1core_natexit >= FM1CORE_NATEXIT)
        return fm1core_halt ? 0 : -1;
    fm1core_atexit_fn[fm1core_natexit++] = fn;
    return 0;
}

/* what exit() does to the device's state, once: the handlers (last first), then the glue's cleanup. Also the
 * descriptor's shutdown (idempotent). */
static void fm1core_run_exit(void)
{
    if (fm1core_down)
        return;
    fm1core_down = 1;
    while (fm1core_natexit > 0)
        fm1core_atexit_fn[--fm1core_natexit]();
    if (fm1core_exit_cleanup)
        fm1core_exit_cleanup();
    fflush(stdout);
}

static void fm1core_exit(int code)
{
    if (!fm1core_halt) {
        printf("fm1core: the firmware called exit(%d): halted (reload the core to power on again)\n", code);
        fm1core_run_exit();
        fm1core_halt = 0x100 | (code & 0xFF);
    }
    if (fm1core_exit_park)
        fm1core_exit_park();
}

static inline int fm1core_halted(void) { return fm1core_halt; }

#define exit(code) fm1core_exit(code)
#define atexit(fn) fm1core_atexit(fn)
