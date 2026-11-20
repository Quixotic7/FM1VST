/* SPDX-License-Identifier: GPL-3.0-only */
/* tkdemo's descriptor (core-api/fm1core.h) */
#include <string.h>

#include "tkdemo.h"
#include "version.h"

static const fm1core_t desc = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = "TK Demo",
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL("https://github.com/Quixotic7/FM1VST"),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = sizeof tkd_flash,
    .panel = &tkd_panel,
    .keymap = tkd_keymap,
    .options = tkd_options,
    .init = tkd_init,
    .tick = tkd_tick,
    .frame = tkd_frame,
    .idle = tkd_idle,
    .audio = tkd_audio,
    .midi_in = tkd_midi_in,
    .midi_out_take = tkd_midi_out_take,
    .dump = tkd_nop,
    .stats = tkd_stats,
    .ui_info = tkd_ui_info,
    .shutdown = tkd_nop,
    .halted = tkd_halted,
    .flash_stage = tkd_flash_stage,
    .flash = tkd_flash_ptr,
    .flash_dirty = tkd_flash_dirty,
    .flash_sync = tkd_flash_sync,
    .credits = FM1CORE_CREDITS_OR_NULL,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi) { return abi == FM1CORE_ABI ? &desc : NULL; }
