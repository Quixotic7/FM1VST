/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Melodee core runs: the ONE include list (included once, by melodee_fw.c), from the unmodified
 * submodule cores/melodee. It follows firmware/src/melodee.c (the device's unit), as Melodee's own host builds do:
 * the sound side through tests/hostsim.c (libc.c, the engines with FM6 and CZ-1, voices, mod, FX, usb.c's MIDI
 * queues, TRS MIDI's parser, the song chain, the pattern banks and seq.c: Melodee is its own sequencer; hostsim's
 * main() is renamed and never called), then the core's HAL (melodee_hal.h), the audio ISR (audio.c), then the UI
 * as melodee.c orders it, then the flash storage, the user presets, the projects and the FM6 / CZ-1 stores
 * (MELODEE_FLASH 1: storage.c on melodee_hal.h's NOR hooks instead of storage_hw.c's SPI driver).
 * Built out (they need the hardware), as the host tests build them: MELODEE_USB_AUDIO (usb_audio.c, "Melodee Out /
 * In": the UAC1 functions; hostsim's default, so the GLO menu has no USB audio entries), MELODEE_OTA (the update
 * entry and with it editor.c, the web editor's SysEx, which answers through ota_wire_send), MELODEE_CDC (the serial
 * console), the TRS UART's poll (its parser is in, unused). Melodee has no sample engines (no SAMPLE / GRAIN tables);
 * its icons and keycaps are in. The hook bodies are in melodee_fw.c. */

/* ------------------------------------------------ sound, MIDI, sequencer --- */
#include <stddef.h>
#include <stdint.h>
#define MELODEE_OTA 0                                 /* (melodee.c's defaults are 1: hardware) */
#define MELODEE_USB_AUDIO 0
#define MELODEE_CDC 0
/* the 1 MiB NOR as RAM (melodee_hal.h), declared before the sound side as the Felucca core does (Melodee's
 * eng_sample.c is gone, SMP_USER_XIP is unused) */
#define EMU_FLASH_SIZE 0x100000u
static uint8_t emu_flash[EMU_FLASH_SIZE];
#define main hostsim_main
#include "../../cores/melodee/tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the HAL --- */
#include "melodee_hal.h"
#include "../../cores/melodee/firmware/src/audio.c"   /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define MELODEE_FLASH 1                               /* storage.c on the NOR image: settings, projects, presets */
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "v0.11.1"                     /* melodee.c's default */
#endif
#include "../../cores/melodee/firmware/src/gfx.c"
#include "../../cores/melodee/firmware/src/panel.c"
#include "../../cores/melodee/firmware/src/ui.c"
#include "../../cores/melodee/firmware/src/icons.c"   /* parameter icons (MELODEE_ICONS), used by ui_draw.c */
#include "../../cores/melodee/firmware/src/ui_graph.c"
#include "../../cores/melodee/firmware/src/ui_draw.c"
#include "../../cores/melodee/firmware/src/ui_menu.c"
#include "../../cores/melodee/firmware/src/ui_input.c"
#include "../../cores/melodee/firmware/src/ui_layer.c" /* the quick layers (FX GLO SCL EDIT held) */

/* --------------------------------------------- storage, presets, projects --- */
#include "../../cores/melodee/firmware/src/storage.c"
#include "../../cores/melodee/firmware/src/upreset.c" /* user presets (flash with MELODEE_FLASH) */
#include "../../cores/melodee/firmware/src/project.c" /* projects, the settings record (settings_persist.c) */
#include "../../cores/melodee/firmware/src/cz_store.c" /* CZ-1: Casio tone SysEx */
#include "../../cores/melodee/firmware/src/fm6_store.c" /* FM6: DX7 SysEx, the STORE page (the bank: fm6_bank.c) */
