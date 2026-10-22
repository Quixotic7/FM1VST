/* SPDX-License-Identifier: GPL-3.0-only */
/* The PIGEON core (https://github.com/TheShrimpBlimp/pigeon, the pinned, unmodified submodule cores/pigeon at v0.4.1):
 * a sampler on Felucca 1.5.1 (two chopped songs and a 16-pad kit), whose browser unit, web/emu/felucca_web.c, is
 * driven by the family's glue (fw_core.h). Its MP3 decoder (ko_mp3.c + libmad) is linked beside it (core_pigeon.cmake).
 *
 * THE SAMPLE BANK. On the FM-1 the bank (KOB2, 500 KiB: a 4 KiB header and 496 KiB of audio, waveform and MP3 index)
 * lives in the 1 MiB NOR itself, in five fixed extents the PIGEON package frees for it (hal/ko_layout.h ko_bank_phys:
 * 0xA0000, 0xB4000, 0x78000 reclaimed app space, 0xEA000 the USR region, 0x60000), with its chop / compressor
 * metadata sector at 0xB3000; the firmware reads it in place (KO_BANK_XIP). The web unit's nor[] is that same 1 MiB,
 * so the bank is part of the core's flash image: it persists with the plugin state, presets, flash.bin and the
 * site's saves like every other byte, and survives a power cycle, at the device's own addresses. (The web unit's
 * browser-only pool at 0x2000000, web_load_bank's "unlimited" mode, is not used: web_bank_memory stays NULL.)
 * Samples are loaded as on the device: PIGEON's own tools (tools/ko_bank.py upload, the sample loader web/pigeon)
 * send the FL editor protocol's bank commands 90..97 (F0 7D 46 4C, editor.c ko_bank_editor.c: negotiate, begin,
 * data, commit, read, erase, metadata, diagnostics), which the family's SysEx service serves (FM1CORE_FW_EDITOR_C:
 * the frames through core-glue/common/sysex.h, the writes through the web unit's st_erase / st_prog on nor[]). So
 * FM1VST's "Show to editors" port and the site's hosted sample loader drive it as they drive a unit.
 * No factory bank: the release (pigeon-0.4.1.fwsc) carries none (tools/fm1pkg_make.py writes only the boot head and
 * the app area; the KOPOOL2 / KOPOOL3 / USR descriptors only tell the loader to keep those sectors), and the firmware
 * boots with an empty bank: the UI runs, Song 1, Song 2 and the kit are silent until a bank is uploaded ("Empty slots
 * remain silent", docs/PIGEON_V04_RELEASE.md; the emulator page: "No samples are preloaded").
 *
 * What differs from Felucca 1.6.1's unit: a three-second splash (web_boot_ms), the screen reader's frames (not wired
 * here), and the device's USB product name "PIGEON" (usb.c FELUCCA_SAMPLER_ONLY: EditorPort's name). */
#define FM1CORE_FW_P pg
#define FM1CORE_FW_WEB_C "../../cores/pigeon/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/pigeon/firmware/src/editor.c"   /* the editor's SysEx: backup, the bank */
#define FM1CORE_FW_OTA_C "../../cores/pigeon/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"                                             /* (felucca.c; build_signed_release.sh) */
#include "fw_core.h"
