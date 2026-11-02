/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's instrument, firmware/app/ravel_app.c, as its own unit (tools/build_fm1.py O2_UNITS: -O2, no fused
 * multiply-add), unmodified. The FM-1 build's defines: the release's version (RAVEL_BUILD, the hub's), no audio input
 * (RV_NO_AUDIO_IN); RV_FM1 only places state in the device's RAM sections, so it is left out */
#define RAVEL_BUILD FM1CORE_VERSION
#define RV_NO_AUDIO_IN 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "ravel_app.c"
