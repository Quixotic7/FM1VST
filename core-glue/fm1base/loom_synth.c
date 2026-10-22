/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's app/dsp/synth.c (GPL-3.0-only) as its own unit, unmodified, as fm1-base's build.py builds
 * an fm1.toml "o2" unit (-O2, no fused multiply-add) */
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "dsp/synth.c"
