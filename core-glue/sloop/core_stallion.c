/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion 1.1 (https://github.com/EberonLtd/stallion-fm1, the pinned submodule cores/stallion-fm1): a Clipper 1.3 fork
 * (SLOOP 2.8's line) whose felucca.c has SLOOP's include list plus its own (the AT PRESSURE bar ui_atpop.c, KEY
 * TOUCH key_touch.c through seq.c, the console's clock and flash measurements fm1_clk.h, the render in RAM
 * fm1_hot.h): the SLOOP-family glue (sloop_core.c) runs it as SLOOP, with stallion_hal.h's stand-ins for those */
#define FM1CORE_SL_C1_BAR 1                /* SLOOP 2.8.3's fm1_cpu1_bar / fm1_cpu1_halt / fm1_c1_mb (sloop_hal.h) */
#define FM1CORE_SL_NO_ED_MASTER 1          /* no LIVE page level: main.c reads the MASTER pot only */
#define FM1CORE_SL_STALLION 1              /* stallion_hal.h, ui_atpop.c (sloop_firmware.h) */
#define FM1CORE_SL_FW 1                   /* FM1CORE_SL_SLOOP (sloop_firmware.h) */
#include "sloop_core.c"
