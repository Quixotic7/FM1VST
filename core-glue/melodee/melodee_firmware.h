/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Melodee core runs: the ONE include list (included once, by melodee_fw.c), from the unmodified
 * submodule cores/melodee (v1.4.5). It follows firmware/src/melodee.c (the device's unit), as Melodee's own host
 * builds do (tests/ui_test.c): the sound side through tests/hostsim.c (libc.c, the engines with Prophet, FM6, CZ-1,
 * SID and the x0x 909, voices, mod, FX, usb.c's MIDI queues, TRS MIDI's parser, the song chain, the pattern banks
 * and seq.c; hostsim's main() is renamed and never called), then the core's HAL (melodee_hal.h), the audio ISR
 * (audio.c), then the UI as melodee.c orders it, then the flash storage, the user presets, the projects and the
 * FM6 / CZ-1 stores (MELODEE_FLASH 1: storage.c on melodee_hal.h's NOR hooks instead of storage_hw.c's SPI driver).
 *
 * The build options are the release workflow's (.github/workflows/release.yml: DUAL_CORE 1, RAM_AUDIO 1), except:
 *   MELODEE_CORE1_FALLBACK 1  (release: 0, which traps on a CPU1 failure; the inline worker below never fails, and
 *                              1 renders alone instead of trapping if it ever did)
 *   MELODEE_CACHE_RAM 0       (the data cache as RAM is hardware; with RAM_AUDIO and CACHE_RAM the undo keeps one
 *                              SRAM level until the cache self-test passes and then eight in the cache: 0 keeps
 *                              the eight in SRAM, the device's result)
 * Built out (they need the hardware), as the host tests build them: MELODEE_USB_AUDIO (usb_audio.c, "Melodee Out /
 * In": the UAC1 functions), MELODEE_OTA (usb.c's SysEx plumbing and the update entry), MELODEE_CDC (the serial
 * console), the TRS UART's poll (its parser is in, unused). The hook bodies are in melodee_fw.c.
 *
 * THE MANAGERS' SYSEX (research/managers-2026-10-19/MANAGERS.md). What MELODEE_OTA 1 compiles for them is here on the
 * glue's transport instead of usb.c's (core-glue/common/sysex.h): the firmware's own editor.c (the web editor's FL
 * protocol: INFO, parameters, user presets, projects, BACKUP_LIST / GET / PUT, the CZ banks and FM6B_* stores, WATCH
 * pushes; the general preset frames 7D 4D 4C and the Prophet's Sequential frames 01 31 / 01 32 it takes too) and ota.c
 * (the M-UPGRADE identity, common/sysex_ota.h; its update never runs). Their hooks: ota_wire_send = the glue's reply
 * ring, ota_frame_get / done = its frame buffer, so_w / so_r / SXQ = the ring's indices (editor.c ed_room). The main
 * loop (melodee_fw.c me_main) calls ed_service and ota_service where main.c does; editor.c's waits (ed_flash_stop:
 * fm1_wdt_feed in a loop) yield on the coroutine as every other wait here. The DX7 / CZ dumps OUT (fm6_store.c,
 * cz_store.c: their sends are #if MELODEE_OTA in the firmware) stay built out: requests for them get no reply.
 *
 * DUAL CORE, INLINE. Melodee 1.4 renders FM6 / Prophet pairs, the batch of voice jobs and the FX bus / reverb on
 * CPU1 (audio_worker.h: a one-job mailbox, CPU0 submits, CPU1 runs, CPU0 joins before it uses the result). The
 * core has one thread (the ABI's threading contract), so the worker runs INLINE: audio_worker.h is included first
 * with the host's atomics, the worker is online (as fm1_multicore_start leaves it on the device), and every
 * audio_worker_join() first runs the pending job on the caller's stack (audio_worker_poll, the worker loop's own
 * step), then joins as the device does. The job is the device's, the same code on the same state; only its timing
 * moves (CPU1 runs it while CPU0 prepares; here at the join). The voice budget is then the device's (voice.c
 * voice_budget: the selected allowance with CPU1 online, not the one-core cap of 16). */

/* ------------------------------------------------ the build options --- */
#include <stddef.h>
#include <stdint.h>
#define MELODEE_OTA 0                                 /* (melodee.c's defaults are 1: hardware) */
#define MELODEE_USB_AUDIO 0
#define MELODEE_CDC 0
#define MELODEE_DUAL_CORE 1                           /* (build_options.h's default; hostsim alone builds it 0) */
#define MELODEE_CORE1_FALLBACK 1
#define MELODEE_RAM_AUDIO 1                           /* the release profile: VBUDGET 18 (core.h) */
#define MELODEE_CACHE_RAM 0

/* ------------------------------------------------------ the clock --- */
/* the firmware's clock is TIMER4 at 24 MHz (hal/fm1_time.h; audio.c's DAC_TICKS, the hold times), and melodee.c
 * times MIDI with it (usb.c's ring: midi_in_time, the audio ISR's block timeline): both before hostsim.c, which
 * would leave usb.c on fm1_ms (melodee_hal.h defines fm1_ticks) */
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
#include "../../cores/melodee/firmware/src/audio_worker.h"
static int fm1core_me_aw_join(void)                   /* run the pending job here (CPU1's step), then join */
{
    uint32_t done = AW_LOAD(&audio_worker.complete);
    if (audio_worker_online && AW_LOAD(&audio_worker.request) == done + 1u)
        audio_worker_poll(&done);
    return audio_worker_join();
}
#define audio_worker_join fm1core_me_aw_join

/* ------------------------------------------------ sound, MIDI, sequencer --- */
/* the 1 MiB NOR as RAM (melodee_hal.h), declared before the sound side as the Felucca core does */
#include "nor.h"                                  /* (core-glue/common) */
#define EMU_FLASH_SIZE FM1NOR_SIZE
#define emu_flash fm1nor_image
#define main hostsim_main
#include "../../cores/melodee/tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the HAL --- */
#include "melodee_hal.h"
#include "../../cores/melodee/firmware/src/audio.c"   /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define MELODEE_FLASH 1                               /* storage.c on the NOR image: settings, projects, presets */
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "v1.4.5"                      /* melodee.c's default */
#endif
/* panel_setup (ui_input.c: Calibration, from the menu or OCT- + OCT+ at power-on) waits in loops of its own for the
 * buttons and knobs; melodee_fw.c runs the main loop on a coroutine, and these loops yield there (fm1_wdt_feed) */
#include "../../cores/melodee/firmware/src/gfx.c"
#include "../../cores/melodee/firmware/src/panel.c"
#include "../../cores/melodee/firmware/src/ui.c"
#include "../../cores/melodee/firmware/src/icons.c"   /* parameter icons (MELODEE_ICONS), used by ui_draw.c */
#include "../../cores/melodee/firmware/src/ui_graph.c"
#include "../../cores/melodee/firmware/src/ui_draw.c"
#include "../../cores/melodee/firmware/src/ui_menu.c"
#include "../../cores/melodee/firmware/src/ui_input.c"
#include "../../cores/melodee/firmware/src/ui_layer.c" /* the quick layers (GLO SCL held) */

/* --------------------------------------------- storage, presets, projects --- */
#include "../../cores/melodee/firmware/src/storage.c"
#include "../../cores/melodee/firmware/src/upreset.c" /* user presets (flash with MELODEE_FLASH) */
#include "../../cores/melodee/firmware/src/project.c" /* projects, the settings record (settings_persist.c) */
#include "../../cores/melodee/firmware/src/cz_store.c" /* CZ-1: Casio tone SysEx */
#include "../../cores/melodee/firmware/src/fm6_store.c" /* FM6: DX7 SysEx, the STORE page */

/* -------------------------------------------- the managers' SysEx (above) --- */
#include "sysex.h"                                    /* (core-glue/common) */
static fm1sx_in_t fm1core_me_sxi;                     /* the request waiting for the main loop (usb.c sx_frame) */
static fm1sx_out_t fm1core_me_sxo;                    /* the replies, CIN 4..7 packets (usb.c sx_out_q) */
static int fm1core_me_sx_accept(const uint8_t *f, uint32_t n)   /* the frames ed_service / ota_service take */
{
    return fm1sx_is_fl(f, n) || fm1sx_is_mupg(f, n) ||
           (n >= 3u && f[0] == 0x7Du && f[1] == 0x4Du && f[2] == 0x4Cu) ||   /* upreset.c general_sysex_frame */
           (n >= 2u && f[0] == 0x01u && (f[1] == 0x31u || f[1] == 0x32u));    /* editor_prophet.c p5_native_frame */
}
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1sx_send(&fm1core_me_sxo, p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_me_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_me_sxi); }
#define FM1SX_OTA_TRANSPORT_DONE 1
#define SXQ FM1SX_OUT_PKTS
#define so_w (fm1core_me_sxo.w)
#define so_r (fm1core_me_sxo.r)
#define MELODEE_ID "FM-1_900"                         /* (melodee.c: the package identity) */
#define FM1SX_OTA_C "../../cores/melodee/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_me_sxi
#define FM1SX_OTA_OUT fm1core_me_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
#define ED_BK_FLASH_PTR(off) ((const uint8_t *)fm1nor_image + (off))   /* (editor_backup.c: the flash as XIP reads it) */
#include "../../cores/melodee/firmware/src/editor.c"  /* the web editor's SysEx */
#undef so_w
#undef so_r
