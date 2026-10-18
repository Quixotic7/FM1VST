/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware a SLOOP-family core runs: the ONE include list (included once, by sloop_fw.c), from the unmodified
 * submodule. It follows the firmware's own unity unit, firmware/src/felucca.c, in its order, with the device's HAL
 * headers (firmware/hal/fm1_*.h) replaced by sloop_hal.h, as the firmwares' own host builds do (tests/hostsim.c,
 * tests/ui_pages_test.c; No Man Sky's wasm/nms_wasm.c). Which firmware: FM1CORE_SL_FW (core_<id>.c sets it):
 *   FM1CORE_SL_SLOOP    SLOOP 2.7.1     3 synth parts + drums, the second core, ui_vis
 *   FM1CORE_SL_DX       sloopDX 4.0     SLOOP 2.7 with the DX7 engine (ui_voice), no ui_vis, MIDI program change
 *   FM1CORE_SL_ORBIT    ORBIT 0.7.1     SLOOP 2.4+ line, one core, ORBIT's engines and screens
 *   FM1CORE_SL_NMS      No Man Sky 0.4  SLOOP 2.7.1 with ui_nms / ui_game (no ui_song, icons, ui_vis), its tree
 *                                       one level down (cores/nomanskyFM/firmware)
 * Built out (they need the hardware), as the firmwares' host builds leave them out: FELUCCA_OTA (the M-UPGRADE
 * update entry and with it editor.c, the web editor's SysEx, and recovery.c), FELUCCA_CDC (console.c), FELUCCA_UAC
 * (USB audio in), cpu1.c (the device's boot of the second core: its jobs run inline, sloop_hal.h). lcd.c (the SPI
 * driver) is replaced by sloop_hal.h's lcd_fill / lcd_blit. main.c IS built: its felucca_init (the parts' power-on
 * sounds, autosave_resume, the settings into the song) is called verbatim; fm1_main / fm1_cstart / the TIMER5 ISR
 * are compiled and never run (sloop_fw.c is their loop, on the ABI's clock). */
#include <stddef.h>
#include <stdint.h>
#include "nor.h"                                   /* (core-glue/common) the image the XIP window reads */

#define FM1CORE_SL_SLOOP 1
#define FM1CORE_SL_DX 2
#define FM1CORE_SL_ORBIT 3
#define FM1CORE_SL_NMS 4
#ifndef FM1CORE_SL_FW
#error "FM1CORE_SL_FW: which SLOOP-family firmware (core_<id>.c)"
#endif
#define FM1CORE_SL_HAS_CPU1 (FM1CORE_SL_FW != FM1CORE_SL_ORBIT)

/* ------------------------------------------------------------- the HAL --- */
#define __attribute__(x)                           /* (.pool / .noinit / .lowram sections, alignment: the host) */
#include "felucca_tables.h"
#define memset felucca_memset                      /* libc.c is the device's (freestanding): kept off the host's */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "sloop_hal.h"
#include "gfx.c"
#include "core.h"
#if FM1CORE_SL_HAS_CPU1
#include "cpu1.h"                                   /* (the job API: cpu1.c below, on sloop_hal.h) */
#endif

/* ------------------------------------------------------------ the sound --- */
#include "engines.c"
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"
#include "fx.c"
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0                             /* (TRS MIDI: the UART is hardware; MIDI comes in over "USB") */
#include "usb.c"
#define FELUCCA_ARRANGER 1
#include "arranger.c"
#include "seq.c"
#include "audio.c"
#if FM1CORE_SL_HAS_CPU1
#include "cpu1.c"                                   /* the second core: boot, self-test, state */
#endif

/* -------------------------------------------------------------------- UI --- */
#include "panel.c"
#include "ui.c"
#if FM1CORE_SL_FW != FM1CORE_SL_NMS
#include "ui_song.c"
#endif
#include "ui_studio.c"
#if FM1CORE_SL_FW == FM1CORE_SL_DX
#include "ui_voice.c"
#endif
#if FM1CORE_SL_FW != FM1CORE_SL_NMS
#include "icons.c"
#endif
#include "ui_draw.c"
#if FM1CORE_SL_FW == FM1CORE_SL_SLOOP || FM1CORE_SL_FW == FM1CORE_SL_ORBIT
#include "ui_vis.c"
#endif
#include "ui_layers.c"
#include "ui_menu.c"
#if FM1CORE_SL_FW == FM1CORE_SL_NMS
#include "ui_nms.c"
#include "ui_game.c"
#endif
#include "ui_input.c"

/* --------------------------------------------- storage, presets, projects --- */
#define FELUCCA_FLASH 1
#include "storage.c"
#include "upreset.c"
#include "project.c"
#if FM1CORE_SL_FW != FM1CORE_SL_NMS
#include "splash.c"
#endif
/* main.c's two external functions (the TIMER5 ISR, the reset entry) would be emitted and pull in the device's USB
 * and boot code (inline asm): made static and unused, so the compiler drops them; felucca_init stays callable */
#define fm1_timer5_irq fm1core_sl_t5_unused(void); static void fm1core_sl_t5
#define fm1_cstart fm1core_sl_cstart_unused(void); static void fm1core_sl_cstart
#include "main.c"
#undef fm1_timer5_irq
#undef fm1_cstart
#undef __attribute__
