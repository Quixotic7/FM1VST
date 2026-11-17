/* SPDX-License-Identifier: GPL-3.0-only */
/* Mighty Arp's app/dsp/engine.c (MPL-2.0, Nick Culbertson) as its own unit (as fm1-base's build.py: the fm1.toml "o2"
 * unit, -O2, no fused multiply-add), unmodified. PLAT_HOST: plat.h's .pool section and the engine's device-only part
 * off (as build.py o2_objects for the host and the browser emulator) */
#define PLAT_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "engine.c"
