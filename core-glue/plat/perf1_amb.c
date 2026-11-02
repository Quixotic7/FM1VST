/* SPDX-License-Identifier: GPL-3.0-only */
/* PERF-1's firmware/src/dsp/amb.c (with the FM6 engine it includes, dsp/fm6.inc) as its own unit, as the firmware's
 * build compiles it (tools/build.py O2_UNITS), unmodified. EN_CORE2 is off: the reverb runs inline, as on a unit
 * whose second core is held (en_core2_attach is never called). */
#define EN_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "amb.c"
