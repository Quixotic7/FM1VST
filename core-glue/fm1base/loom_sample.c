/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's app/dsp/sample.c (MPL-2.0 since LOOM-8 0.8) as its own unit, unmodified, as fm1-base's build.py builds
 * an fm1.toml "o2" unit (-O2, no fused multiply-add) */
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
/* the factory set built in, as LOOM-8 0.6.1 had it and as its host simulator and browser emulator still build it
 * (dsp/sample.c SMP_FACT_BUILTIN): since 0.8 the FM-1's image leaves it out for the installer to put in the user bank,
 * which a fresh flash here does not have. sample.c reads PLAT_HOST for that alone (sample.h has none) */
#define PLAT_HOST 1
#define section(x) aligned(4)               /* (its .pool sections: Mach-O has none) */
#include "dsp/sample.c"
