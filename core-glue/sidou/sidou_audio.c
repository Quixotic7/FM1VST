/* SPDX-License-Identifier: GPL-3.0-only */
/* platform/fm1/audio.c, unmodified but for two lines (core_sidou.cmake's overlay): the top-level `.set` that names
 * the transport for the emulator tests is left out, and a producer waiting for the audio interrupt to drain the
 * register ring gives the clock back (fm1core_sd_wait) in the place of the CPU's `idle`. */
#include "sidou_glue.h"
#include "audio.c"
