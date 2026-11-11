/* SPDX-License-Identifier: GPL-3.0-only */
/* M-PYRE 1.0.3 (https://github.com/HistoryOfSky/m-pyre, the pinned submodule cores/m-pyre): a SLOOP 2.8.1 fork whose
 * felucca.c has SLOOP's include list, so the SLOOP-family glue (sloop_core.c) runs it as SLOOP */
#define FM1CORE_SL_C1_BAR 1                /* SLOOP 2.8.3's fm1_cpu1_bar / fm1_cpu1_halt / fm1_c1_mb (sloop_hal.h) */
#define FM1CORE_SL_FW 1                   /* FM1CORE_SL_SLOOP (sloop_firmware.h) */
#include "sloop_core.c"
