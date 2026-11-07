/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's app/dsp/synth.c (MPL-2.0 since LOOM-8 0.8) as its own unit, unmodified, as fm1-base's build.py builds
 * an fm1.toml "o2" unit (-O2, no fused multiply-add) */
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#define section(x) aligned(4)               /* (braids.h: its .pool section; Mach-O has none) */
#include "dsp/synth.c"
