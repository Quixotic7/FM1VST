/* SPDX-License-Identifier: GPL-3.0-only */
/* platform/fm1/input.c, unmodified but for the wait for the next scan frame (fm1core_sd_wait, in the place of the
 * CPU's `idle`; core_sidou.cmake's overlay), over hal/fm1_input.h's scan state, which core_sidou.c's tick writes. */
#include "sidou_glue.h"
#include "input.c"
