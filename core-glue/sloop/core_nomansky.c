/* SPDX-License-Identifier: GPL-3.0-only */
/* No Man Sky FM 0.4 (https://github.com/hericdk/nomanskyFM, main at 4304597: no tags; the hub's package was
 * published 2026-10-16 23:24 UTC, 37 s after that commit): the SLOOP-family glue (sloop_core.c) on its firmware
 * tree (cores/nomanskyFM/firmware). Upstream's only host build is wasm/nms_wasm.c (the site's simulator, zig cc for
 * wasm32); this core builds the same sources natively, on the device's two-core path (the jobs inline), where the
 * simulator builds FELUCCA_CPU1 0. Its per-LED brightness (fm1_led_lv, the key light effects) reaches the panel as
 * levels (sloop_core.c). Credit (the author's request, README "Use it in your own work"):
 *     Features from No Man Sky FM by Hericdk (github.com/hericdk/nomanskyFM) */
#define FM1CORE_SL_FW 4                   /* FM1CORE_SL_NMS (sloop_firmware.h) */
#include "sloop_core.c"
