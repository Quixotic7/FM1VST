/* SPDX-License-Identifier: GPL-3.0-only */
/* ETCH (https://github.com/SikkenWorks/ETCH, the pinned submodule cores/ETCH): the core unit. ETCH is a native
 * FM-1 application (firmware/fm1-native: bringup.c and the units build.py links with it) on Felucca's HAL, with no
 * emulator of its own, and the app RUPTURE is built from (RUPTURE's vendor/etch-v1.88-source): the same glue
 * (sikken_glue.h, sikken_hal.h, hal/fm1_flash.h; core_rupture.c's header says what runs in the place of the
 * device's interrupts). This unit is bringup.c, unmodified (ETCH has no pi32v2 instruction in it), over the
 * family's HAL (the core's VFS overlay, sikken.cmake), and the glue. build.py's other 13 units are their own
 * (core_etch.cmake EXTRA_SOURCES). Not here: USB audio (ETCH_NATIVE_UAC 0), the TRS jack, the update session and
 * the USB UBOOT key (both dropped as they arrive, printed). OCT- + OCT+ held 5 s (UBOOT), OCT- held at power-on: exit
 * 3, the core halts. Input: a button press is held at least 120 ms (the main loop reads the buttons every 100 ms).
 * At a cold boot ETCH asks for a workflow (SIMPLE / ADVANCED, SEL confirms) and is silent until then; the run's
 * RUN_ARGS press SEL (core_etch.cmake). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) */
#include "version.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

/* ---- what the device's linker script gives fm1_cstart: its copy loops (.bss, .pool, .data, .ram_text) run over
 * nothing here (a core's RAM is fresh, its data initialised, at each power-on) ---- */
static uint32_t fm1core_sk_lnk[1];
#define _bss_start fm1core_sk_lnk
#define _bss_end fm1core_sk_lnk
#define _pool_start fm1core_sk_lnk
#define _pool_end fm1core_sk_lnk
#define _data_load fm1core_sk_lnk
#define _data_start fm1core_sk_lnk
#define _data_end fm1core_sk_lnk
#define _rt_load fm1core_sk_lnk
#define _rt_start fm1core_sk_lnk
#define _rt_end fm1core_sk_lnk

/* libc.c is the device's (freestanding, included by bringup.c): its memset / memcpy / memcmp kept off the host's */
#define memset etch_libc_memset
#define memcpy etch_libc_memcpy
#define memcmp etch_libc_memcmp
#include "bringup.c"                      /* (the submodule's, unmodified) */
#undef memset
#undef memcpy
#undef memcmp

/* the page and project of the dump (sikken_glue.h) */
#define SIKKEN_PAGE_NAME() etch_ui_title(native_page)
#define SIKKEN_PROJECT (native_project_browser + 1u)
#define FM1CORE_SK_HOLD_MS 120u           /* ETCH reads the buttons on a 100 ms control tick (bringup.c's main loop) */
#include "sikken_glue.h"
