/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP-family descriptor (core-api/fm1core.h), shared: core_<id>_desc.c defines FM1CORE_SL_NAME (the menu
 * name) and FM1CORE_SL_REPO (the firmware's repository) and includes this. Its own translation unit (-Wall), as
 * core_felucca_desc.c: the firmware unit (core_<id>.c -> sloop_core.c) defines the panel and the fm1core_sl_*
 * entry points with external (hidden) linkage; they are declared here. Every SLOOP-family firmware labels its
 * buttons as Felucca's panel.c B_NAME does (FM1P_B_* order). No parameter map yet (CORES-PLAN.md 4: every knob a
 * relative turn). */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

#ifndef FM1CORE_SL_NAME
#error "FM1CORE_SL_NAME / FM1CORE_SL_REPO (core_<id>_desc.c)"
#endif

extern fm1panel_t fm1core_sl_panel;
void fm1core_sl_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
int fm1core_sl_boot_options(int fail, const char *reason, int stage);
void fm1core_sl_init(int demo);
void fm1core_sl_tick(uint32_t ms);
void fm1core_sl_frame(void);
void fm1core_sl_idle(void);
void fm1core_sl_audio(int16_t *stereo, uint32_t frames);
int fm1core_sl_midi_in(uint32_t pkt);
int fm1core_sl_midi_out_take(uint32_t *pkt);
void fm1core_sl_dump(void);
void fm1core_sl_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_sl_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_sl_keymap(void);
int fm1core_sl_halted(void);
void fm1core_sl_shutdown(void);
uint8_t *fm1core_sl_flash(void);
uint32_t fm1core_sl_flash_dirty(void);
int fm1core_sl_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_sl_flash_sync(void);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = FM1CORE_SL_NAME,
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL(FM1CORE_SL_REPO),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    /* the printed labels, FM1P_B_* order (firmware/src/panel.c B_NAME) */
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_sl_panel,
    .keymap = NULL,
    .options = fm1core_sl_options,
    .boot_options = fm1core_sl_boot_options,
    .init = fm1core_sl_init,
    .tick = fm1core_sl_tick,
    .frame = fm1core_sl_frame,
    .idle = fm1core_sl_idle,
    .audio = fm1core_sl_audio,
    .midi_in = fm1core_sl_midi_in,
    .midi_out_take = fm1core_sl_midi_out_take,
    .dump = fm1core_sl_dump,
    .stats = fm1core_sl_stats,
    .ui_info = fm1core_sl_ui_info,
    .shutdown = fm1core_sl_shutdown,
    .halted = fm1core_sl_halted,
    .flash_stage = fm1core_sl_flash_stage,
    .flash = fm1core_sl_flash,
    .flash_dirty = fm1core_sl_flash_dirty,
    .flash_sync = fm1core_sl_flash_sync,
    .param_epoch = NULL,
    .knob_target = NULL,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_sl_keymap();
    return &CORE;
}
