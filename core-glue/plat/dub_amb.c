/* SPDX-License-Identifier: GPL-3.0-only */
/* DUB-1's firmware/src/dsp/amb.c as its own unit (as the firmware's build: tools/build.py O2_UNITS), unmodified.
 * EN_HOST: the device's RAM sections (EN_POOL, EN_COLD) off, as its host simulator; EN_PROF (the Setup page's render
 * stages) off with it, so the page shows 0 there */
#define EN_HOST 1
#pragma clang diagnostic ignored "-Weverything"
#pragma STDC FP_CONTRACT OFF
#include "amb.c"
