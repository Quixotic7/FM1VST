/* SPDX-License-Identifier: GPL-3.0-only */
/* The Speak37 core's descriptor (core-api/fm1core.h ABI 6). Its own translation unit (-Wall), as the other glues':
 * the firmware unit (core_speak37.c) defines the panel and the fm1core_sp_* entry points with external (hidden)
 * linkage; they are declared here. The buttons are the FM-1's printed labels (sp_panel_fm1.c gives them Speak37's
 * jobs: SEL the next page, HOME PLAY, EDIT the keys' mode / MOPE held, ..), FM1P_B_* order. No parameter map
 * (CORES-PLAN.md 4: every knob a relative turn). */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

extern fm1panel_t fm1core_sp_panel;
void fm1core_sp_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
int fm1core_sp_boot_options(int fail, const char *reason, int stage);
void fm1core_sp_init(int demo);
void fm1core_sp_tick(uint32_t ms);
void fm1core_sp_frame(void);
void fm1core_sp_idle(void);
void fm1core_sp_audio(int16_t *stereo, uint32_t frames);
int fm1core_sp_midi_in(uint32_t pkt);
int fm1core_sp_midi_out_take(uint32_t *pkt);
void fm1core_sp_dump(void);
void fm1core_sp_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_sp_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_sp_keymap(void);
int fm1core_sp_halted(void);
void fm1core_sp_shutdown(void);
uint8_t *fm1core_sp_flash(void);
uint32_t fm1core_sp_flash_dirty(void);
int fm1core_sp_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_sp_flash_sync(void);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = "Speak37",
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL("https://github.com/genkigenki/Speak37"),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .credits = FM1CORE_CREDITS_OR_NULL,
    .flash_size = 0x100000u,
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_sp_panel,
    .keymap = NULL,
    .options = fm1core_sp_options,
    .boot_options = fm1core_sp_boot_options,
    .init = fm1core_sp_init,
    .tick = fm1core_sp_tick,
    .frame = fm1core_sp_frame,
    .idle = fm1core_sp_idle,
    .audio = fm1core_sp_audio,
    .midi_in = fm1core_sp_midi_in,
    .midi_out_take = fm1core_sp_midi_out_take,
    .dump = fm1core_sp_dump,
    .stats = fm1core_sp_stats,
    .ui_info = fm1core_sp_ui_info,
    .shutdown = fm1core_sp_shutdown,
    .halted = fm1core_sp_halted,
    .flash_stage = fm1core_sp_flash_stage,
    .flash = fm1core_sp_flash,
    .flash_dirty = fm1core_sp_flash_dirty,
    .flash_sync = fm1core_sp_flash_sync,
    .param_epoch = NULL,
    .knob_target = NULL,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_sp_keymap();
    return &CORE;
}
