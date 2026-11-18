/* SPDX-License-Identifier: GPL-3.0-only */
/* M-Vsynth2's engine unit, firmware/src/vs_dsp.c (unmodified), as tools/build.py builds it: its own translation unit
 * (core_mvsynth2.cmake EXTRA_SOURCES), here under DINGHY_HOST (one core, no hardware). Its .pool buffers (the device's
 * linker's section, which Mach-O rejects) become an alignment; the firmware's own warnings are not the glue's. */
#pragma clang diagnostic ignored "-Weverything"
#define section(x) aligned(4)
#include "../../cores/M-Vsynth2/firmware/src/vs_dsp.c"
