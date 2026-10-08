/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Felucca core runs: the ONE include list (included once, by felucca_fw.c), from the unmodified
 * submodule cores/Felucca. It follows firmware/src/felucca.c (the device's unit), as Felucca's own host builds do:
 * the sound side through tests/hostsim.c (libc.c, engines, voices, mod, FX, usb.c's MIDI queues, TRS MIDI's parser,
 * the song chain and seq.c: Felucca IS its sequencer, so all of hostsim's sound side is wanted; its main() is
 * renamed and never called), then the core's HAL (felucca_hal.h), the audio ISR (audio.c), then the UI as
 * felucca.c orders it, then the flash storage, the user presets and the projects (FELUCCA_FLASH 1: storage.c on
 * felucca_hal.h's NOR hooks instead of storage_hw.c's SPI driver).
 * Built out (they need the hardware), as the host tests build them: FELUCCA_OTA (the update entry and with it
 * editor.c, the web editor's SysEx, which answers through ota_wire_send), FELUCCA_CDC (the serial console),
 * FELUCCA_UAC (USB audio input), the TRS UART's poll (its parser is in, unused). Kept: every engine (the SAMPLE /
 * GRAIN / DRUM sample tables are generated on the host from assets/samples-cc0, which is in the repo), the icons
 * and keycaps. The hook bodies are in felucca_fw.c. */

/* ------------------------------------------------ sound, MIDI, sequencer --- */
#include <stddef.h>
#include <stdint.h>
#define FELUCCA_OTA 0                                 /* (felucca.c's defaults are 1: hardware) */
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
/* the 1 MiB NOR as RAM (felucca_hal.h): declared first, the SAMPLE engine's user slots are read from it in place
 * (eng_sample.c SMP_USER_XIP: on the device the plain XIP window over flash 0xA0000..) */
#define EMU_FLASH_SIZE 0x100000u
static uint8_t emu_flash[EMU_FLASH_SIZE];
#define SMP_USER_XIP(k) ((const uint8_t *)emu_flash + 0xA0000u + (uint32_t)(k) * 0x14000u)
#define main hostsim_main
#include "../../cores/Felucca/tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the HAL --- */
#include "felucca_hal.h"
#include "../../cores/Felucca/firmware/src/audio.c"   /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define FELUCCA_FLASH 1                               /* storage.c on the NOR image: settings, projects, presets */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "v1.0"                        /* felucca.c's default (build.py --release sets it) */
#endif
#include "../../cores/Felucca/firmware/src/gfx.c"
#include "../../cores/Felucca/firmware/src/panel.c"
#include "../../cores/Felucca/firmware/src/ui.c"
#include "../../cores/Felucca/firmware/src/icons.c"   /* parameter icons (FELUCCA_ICONS), used by ui_draw.c */
#include "../../cores/Felucca/firmware/src/ui_graph.c"
#include "../../cores/Felucca/firmware/src/ui_draw.c"
#include "../../cores/Felucca/firmware/src/ui_menu.c"
#include "../../cores/Felucca/firmware/src/ui_input.c"
#include "../../cores/Felucca/firmware/src/ui_layer.c" /* the quick layers (FX GLO SCL EDIT held) */

/* --------------------------------------------- storage, presets, projects --- */
#include "../../cores/Felucca/firmware/src/storage.c"
#include "../../cores/Felucca/firmware/src/upreset.c" /* user presets (flash with FELUCCA_FLASH) */
#include "../../cores/Felucca/firmware/src/project.c" /* projects, the settings record (settings_persist.c) */
