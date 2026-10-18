/* SPDX-License-Identifier: GPL-3.0-only */
/* AMB-1's app/dsp/amb.c (MPL-2.0) as its own unit (as fm1-base's build.py: an fm1.toml "o2" unit, -O2, no fused
 * multiply-add), unmodified. EN_HOST: its .pool section off (Mach-O has none) */
#define EN_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "amb.c"
