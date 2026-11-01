/* SPDX-License-Identifier: GPL-3.0-only */
/* Niagara Drift's firmware/src/dsp/drone.c (the drone engine; it includes drone_fx.c, the delay and reverb) as its own
 * unit (as the firmware builds it: tools/build.py O2_UNITS, -O2, no fused multiply-add; host/build_host.sh -DOM_HOST),
 * unmodified */
#define OM_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "dsp/drone.c"
