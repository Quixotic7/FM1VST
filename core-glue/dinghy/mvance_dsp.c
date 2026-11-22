/* SPDX-License-Identifier: GPL-3.0-only */
/* M-vance's engine unit, firmware/src/mv_dsp.c (unmodified), as tools/build.py builds it: its own translation unit at
 * -O2 (core_mvance.cmake EXTRA_SOURCES), here under DINGHY_HOST (one core, no hardware). Its .pool buffers (the
 * device's linker's section, which Mach-O rejects) become an alignment; the firmware's own warnings are not the glue's. */
#pragma clang diagnostic ignored "-Weverything"
#define section(x) aligned(4)
#include "../../cores/M-vance/firmware/src/mv_dsp.c"
