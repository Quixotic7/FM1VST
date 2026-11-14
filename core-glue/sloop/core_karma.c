/* SPDX-License-Identifier: GPL-3.0-only */
/* KARMA 3.1.2 (https://github.com/eatyourgreensstudio-dotcom/karma-fm1, the pinned submodule cores/karma-fm1): a
 * SLOOP 2.6 fork whose felucca.c has SLOOP's include list (plus ui_op.c; Plaits and Elements in C++, firmware/mi:
 * EXTRA_SOURCES), so the SLOOP-family glue (sloop_core.c) runs it, on one core */
#define FM1CORE_SL_FW 5                   /* FM1CORE_SL_KARMA (sloop_firmware.h) */
#define FELUCCA_ID "FM-1_926"           /* tools/build.py PRODUCT: KARMA's own (the Hub's karma 3.0.3), not SLOOP's 900 */
#include "sloop_core.c"
