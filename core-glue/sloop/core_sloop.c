/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.8.5 (https://github.com/isod89/sloop-fm1, the pinned submodule cores/sloop-fm1): the SLOOP-family glue
 * (sloop_core.c) on SLOOP's own tree */
#define FM1CORE_SL_C1_BAR 1                /* SLOOP 2.8.3's fm1_cpu1_bar / fm1_cpu1_halt / fm1_c1_mb (sloop_hal.h) */
#define FM1CORE_SL_FW 1                   /* FM1CORE_SL_SLOOP (sloop_firmware.h) */
#include "sloop_core.c"
