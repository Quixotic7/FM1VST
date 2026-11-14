/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Tessera core runs: the ONE include list (included once, by tessera_fw.c), from the unmodified
 * submodule cores/tessera (v3.2.2). Tessera is Melodee (Tess), a fork of Melodee: the same sources' shape, so this is
 * melodee_firmware.h redone for Tessera's firmware/src/melodee.c (the device's unit), in its order: the sound side
 * through tests/hostsim.c (libc.c, the engines FM6 / ORACLE / TRIO / PHYS / DRUM, voices, mod, FX, usb.c's MIDI queues,
 * TRS MIDI's parser, the generators gen.c, the song chain, seq.c, tessera.c, pid.c, cord.c; hostsim's main() is
 * renamed and never called), then the family's HAL (melodee_hal.h), the audio ISR (audio.c), the UI as melodee.c
 * orders it, the flash storage, the user presets, the projects and the FM6 store (MELODEE_FLASH 1: storage.c on
 * melodee_hal.h's NOR hooks instead of storage_hw.c's SPI driver), then splash.c (the boot's mosaic).
 *
 * The build options are the 3.5.3 release's (.github/workflows/release.yml: MELODEE_LOOK=1 ./build.sh --release, the
 * dual-core package; tools/build.py build_app's defaults: FM6 1, SID 0, SID KIT 0, PHYS 1, PLAITS 0, HELP_PACKED 1;
 * melodee.c / core.h's for the rest: DUAL_CORE 1, CACHE_RAM 0, RAM_AUDIO 0, ADMISSION 0, TRIO 1, ORACLE 1). The engine
 * flags are core_tessera.cmake's DEFINES (core.h reads them before this file is reached).
 * MELODEE_UART 1 as released: TRS MIDI IN's parser (midi_uart.c) and the SYSTEM page's TRS column; there is no UART
 * here, so nothing arrives on it.
 * Built out (they need the hardware), as the host tests build them: MELODEE_USB_AUDIO (usb_audio.c, "Tessera Out /
 * In"), MELODEE_USB_HOST (usb_host.c), MELODEE_OTA (usb.c's SysEx plumbing and the update entry), MELODEE_CDC. The hook
 * bodies are in tessera_fw.c.
 *
 * THE MANAGERS' SYSEX. What MELODEE_OTA 1 compiles for them is here on the glue's transport instead of usb.c's
 * (core-glue/common/sysex.h), as in the Melodee core: the firmware's own editor.c (the web editor's FL protocol: INFO,
 * parameters, user presets, projects, BACKUP_LIST / GET / PUT, the FM6 stores, WATCH pushes; the general preset
 * frames 7D 4D 4C) and ota.c (the M-UPGRADE identity, common/sysex_ota.h; its update never runs). The DX7 dump OUT
 * (fm6_store.c: its send is #if MELODEE_OTA) stays built out.
 *
 * DUAL CORE, INLINE: as melodee_firmware.h (audio_worker.h's one-job mailbox, the job run at the join). */

/* ------------------------------------------------ the build options --- */
#include <stddef.h>
#include <stdint.h>
#define MELODEE_OTA 0                                 /* (melodee.c's defaults are 1: hardware) */
#define MELODEE_USB_AUDIO 0
#define MELODEE_CDC 0
#define MELODEE_USB_HOST 0
#define MELODEE_UART 1                                /* (as released: TRS MIDI IN, its parser; no UART here) */
#define MELODEE_DUAL_CORE 1                           /* (build_options.h's default) */
#define TRK_START_TESS 1                              /* engines.c: the device's power-on start (hostsim: the tests') */

/* ------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void);
#define MIDI_TIME_NOW() fm1_ticks()
#define MIDI_TICKS_PER_MS (1000u * FM1_TICKS_PER_US)

/* ------------------------------------------- the CPU1 worker, inline --- */
#define AW_WORD volatile uint32_t
#define AW_LOAD(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#define AW_STORE(p, v) __atomic_store_n(p, v, __ATOMIC_RELEASE)
#define AW_TICKS() 0u                                 /* (the join never waits: the job has run) */
#define AW_TIMEOUT_TICKS 1u
#define AW_RAM_LOOP static __attribute__((unused))
#include "../../cores/tessera/firmware/src/audio_worker.h"
static int fm1core_me_aw_join(void)                   /* run the pending job here (CPU1's step), then join */
{
    uint32_t done = AW_LOAD(&audio_worker.complete);
    if (audio_worker_online && AW_LOAD(&audio_worker.request) == done + 1u)
        audio_worker_poll(&done);
    return audio_worker_join();
}
#define audio_worker_join fm1core_me_aw_join

/* ------------------------------------------------ sound, MIDI, sequencer --- */
#include "nor.h"                                  /* (core-glue/common) */
#define EMU_FLASH_SIZE FM1NOR_SIZE
#define emu_flash fm1nor_image
#define main hostsim_main
#include "../../cores/tessera/tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the HAL --- */
#include "melodee_hal.h"
static void lcd_gamma_switch(uint32_t opt) { (void)opt; }   /* (lcd.c: the panel's gamma; the core shows RGB565 as is) */
#include "../../cores/tessera/firmware/src/audio.c"   /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define MELODEE_FLASH 1                               /* storage.c on the NOR image: settings, projects, presets */
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "v3.5.3"                      /* (build.py --release 3.5.3; melodee.c's default is stale) */
#endif
#include "../../cores/tessera/firmware/src/gfx.c"
#include "../../cores/tessera/firmware/src/panel.c"
#include "../../cores/tessera/firmware/src/ui.c"
#include "../../cores/tessera/firmware/src/icons.c"   /* parameter icons (MELODEE_ICONS), used by ui_draw.c */
#include "../../cores/tessera/firmware/src/ui_graph.c"
#include "../../cores/tessera/firmware/src/page_random.c" /* GEN / TWEAK: page randomize (melodee.c's order) */
#include "../../cores/tessera/firmware/src/ui_draw.c"
#include "../../cores/tessera/firmware/src/ui_menu.c"
#include "../../cores/tessera/firmware/src/ui_input.c"
#include "../../cores/tessera/firmware/src/ui_layer.c" /* the quick layers (GLO SCL held) */

/* --------------------------------------------- storage, presets, projects --- */
#include "../../cores/tessera/firmware/src/storage.c"
#include "../../cores/tessera/firmware/src/upreset.c" /* user presets (flash with MELODEE_FLASH) */
#include "../../cores/tessera/firmware/src/project.c" /* projects, the settings record */
#include "../../cores/tessera/firmware/src/fm6_store.c" /* FM6: DX7 SysEx, the STORE shortcut */

/* -------------------------------------------- the managers' SysEx (above) --- */
#include "sysex.h"                                    /* (core-glue/common) */
static fm1sx_in_t fm1core_me_sxi;                     /* the request waiting for the main loop (usb.c sx_frame) */
static fm1sx_out_t fm1core_me_sxo;                    /* the replies, CIN 4..7 packets (usb.c sx_out_q) */
static int fm1core_me_sx_accept(const uint8_t *f, uint32_t n)   /* the frames ed_service / ota_service take */
{
    return fm1sx_is_fl(f, n) || fm1sx_is_mupg(f, n) ||
           (n >= 3u && f[0] == 0x7Du && f[1] == 0x4Du && f[2] == 0x4Cu);     /* upreset.c general_sysex_frame */
}
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1sx_send(&fm1core_me_sxo, p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_me_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_me_sxi); }
#define FM1SX_OTA_TRANSPORT_DONE 1
#define SXQ FM1SX_OUT_PKTS
#define so_w (fm1core_me_sxo.w)
#define so_r (fm1core_me_sxo.r)
#define sx_out_q (fm1core_me_sxo.q)                   /* (editor.c ed_pn_put: PANEL's CC / note packets, as is) */
#ifndef MELODEE_ID
#define MELODEE_ID "FM-1_835"                         /* build.py --release 3.5.3: FM-1_8 + X Y (3.3 on) */
#endif
#define FM1SX_OTA_C "../../cores/tessera/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_me_sxi
#define FM1SX_OTA_OUT fm1core_me_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
#define ED_BK_FLASH_PTR(off) ((const uint8_t *)fm1nor_image + (off))   /* (editor_backup.c: the flash as XIP reads it) */
#include "../../cores/tessera/firmware/src/editor.c"  /* the web editor's SysEx */
#undef so_w
#undef so_r
#undef sx_out_q

/* ------------------------------------------------------------ the boot screen --- */
#include "../../cores/tessera/firmware/src/splash.c"  /* splash, splash_lay (main.c) */
