/* SPDX-License-Identifier: GPL-3.0-only */
/* Clipper 1.3 (https://github.com/isod89/clipper-fm1, the pinned submodule cores/clipper-fm1): a SLOOP 2.8 fork whose
 * felucca.c has SLOOP's include list, so the SLOOP-family glue (sloop_core.c) runs it as SLOOP */
#define FM1CORE_SL_C1_BAR 1                /* SLOOP 2.8.3's fm1_cpu1_bar / fm1_c1_mb (sloop_hal.h) */
#define FM1CORE_SL_NO_ED_MASTER 1          /* no LIVE page level: main.c reads the MASTER pot only */
#define FM1CORE_SL_FW 1                   /* FM1CORE_SL_SLOOP (sloop_firmware.h) */
#include "sloop_core.c"
