/* SPDX-License-Identifier: GPL-3.0-only */
/* MOPE-1's firmware/src/dsp/synth.c as its own unit (as tools/build.py builds it: O2_UNITS, -O2, no fused
 * multiply-add), unmodified */
#define OM_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "dsp/synth.c"
