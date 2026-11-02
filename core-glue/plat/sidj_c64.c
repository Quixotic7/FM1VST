/* SPDX-License-Identifier: GPL-3.0-only */
/* SiDJ's c64/c64.c as its own unit (tools/build.py O2_UNITS: -O2), unmodified: the C64 */
#define SIDJ_HOST 1                       /* (engine.h ENG_POOL: the device's .pool section off) */
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "c64/c64.c"
