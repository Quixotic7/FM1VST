/* SPDX-License-Identifier: GPL-3.0-only */
/* FiMba-1's firmware/src/dsp/kalimba.c as its own unit (as the firmware's builds: -O2, no fused multiply-add),
 * unmodified */
#define OM_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "dsp/kalimba.c"
