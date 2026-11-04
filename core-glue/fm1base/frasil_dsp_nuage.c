/* SPDX-License-Identifier: GPL-3.0-only */
/* FRASIL's engine/dsp/nuage.c (MPL-2.0) as its own unit (as fm1-base's build.py: an fm1.toml "o2" unit, -O2, no fused
 * multiply-add), unmodified. PLAT_HOST: dsp.h's .pool and .ram_text sections off (as build.py o2_objects for the host) */
#define PLAT_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "nuage.c"
