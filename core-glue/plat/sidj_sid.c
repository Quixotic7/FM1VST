/* SPDX-License-Identifier: GPL-3.0-only */
/* SiDJ's sid/sid.c as its own unit (tools/build.py O2_UNITS: -O2), unmodified: the SID chip (C port of reSID 0.16, GPL-2.0-or-later) */
#define SIDJ_HOST 1                       /* (engine.h ENG_POOL: the device's .pool section off) */
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "sid/sid.c"
