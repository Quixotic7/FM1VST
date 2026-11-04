/* SPDX-License-Identifier: GPL-3.0-only */
/* ECHOFIELD's app/dsp/engine.c (MIT, Philip Bos) as its own unit (as fm1-base's build.py: an fm1.toml "o2" unit, -O2,
 * no fused multiply-add), unmodified. PLAT_HOST: its .ram_text section and plat.h's .pool off (as build.py
 * o2_objects for the host and the browser emulator) */
#define PLAT_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "engine.c"
