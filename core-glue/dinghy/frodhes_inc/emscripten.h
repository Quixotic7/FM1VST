/* SPDX-License-Identifier: GPL-3.0-only */
/* A stub <emscripten.h> for Frodhes' web route (web/web_hal.c) built natively as a core (core_frodhes.c): the
 * exports are plain functions, the page's clock is the device's simulated ms, and emscripten_sleep (web_hal.c's
 * platform_poll: the browser's turn) is the coroutine's yield (dinghy_pre.h, included before). */
#pragma once
#define EMSCRIPTEN_KEEPALIVE
/* (+ 1000: web_hal.c takes its first reading as the origin and tests it against 0.0) */
static double emscripten_get_now(void) { return 1000.0 + (double)fm1t_ms(); }
static void emscripten_sleep(unsigned ms) { fm1core_dg_sleep(ms); }
