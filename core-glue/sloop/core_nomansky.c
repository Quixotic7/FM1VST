/* SPDX-License-Identifier: GPL-3.0-only */
/* No Man Sky FM 0.9 (https://github.com/hericdk/nomanskyFM, tag v0.9 = 8271539): the
 * SLOOP-family glue (sloop_core.c) on its firmware tree (cores/nomanskyFM/firmware). Upstream's only host build is
 * wasm/nms_wasm.c (the site's simulator, zig cc for wasm32); this core builds the same sources natively, on the
 * device's two-core path (the jobs inline), where the simulator builds FELUCCA_CPU1 0. Its per-LED brightness
 * (fm1_led_lv, the key light effects) reaches the panel as levels (sloop_core.c). Credit (the author's request,
 * README "Use it in your own work"):
 *     Features from No Man Sky FM by Hericdk (github.com/hericdk/nomanskyFM) */
#define FM1CORE_SL_C1_BAR 1                /* (0.9, on SLOOP 2.8.4) fm1_cpu1_bar / fm1_cpu1_halt / fm1_c1_mb (sloop_hal.h) */
#define FM1CORE_SL_FW 4                   /* FM1CORE_SL_NMS (sloop_firmware.h) */
#include "sloop_core.c"
