/* SPDX-License-Identifier: GPL-3.0-only */
/* The descriptor of a plat-family core (core-api/fm1core.h): the one exported symbol of <id>.fm1core. Its own
 * translation unit (-Wall), as core_felucca_desc.c: the firmware unit defines the panel and the fm1core_pl_* entry
 * points (plat_glue.h) with external (hidden) linkage; they are declared here. A core's core_<id>_desc.c defines
 *   FM1CORE_PL_TITLE    the menu name ("X0X")
 *   FM1CORE_PL_REPO     the firmware's repository ("https://github.com/charlesvestal/fm1-x0x")
 *   FM1CORE_PL_BUTTONS  the 14 printed labels, FM1P_B_* order
 * then includes this file. No parameter map yet (CORES-PLAN.md 4: every knob a relative turn). */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

extern fm1panel_t fm1core_pl_panel;
void fm1core_pl_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
int fm1core_pl_boot_options(int fail, const char *reason, int stage);
void fm1core_pl_init(int demo);
void fm1core_pl_tick(uint32_t ms);
void fm1core_pl_frame(void);
void fm1core_pl_idle(void);
void fm1core_pl_audio(int16_t *stereo, uint32_t frames);
int fm1core_pl_midi_in(uint32_t pkt);
int fm1core_pl_midi_out_take(uint32_t *pkt);
void fm1core_pl_dump(void);
void fm1core_pl_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_pl_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_pl_keymap(void);
int fm1core_pl_halted(void);
void fm1core_pl_shutdown(void);
uint8_t *fm1core_pl_flash(void);
uint32_t fm1core_pl_flash_dirty(void);
int fm1core_pl_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_pl_flash_sync(void);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = FM1CORE_PL_TITLE,
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL(FM1CORE_PL_REPO),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    .button_names = FM1CORE_PL_BUTTONS,
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_pl_panel,
    .keymap = NULL,
    .options = fm1core_pl_options,
    .boot_options = fm1core_pl_boot_options,
    .init = fm1core_pl_init,
    .tick = fm1core_pl_tick,
    .frame = fm1core_pl_frame,
    .idle = fm1core_pl_idle,
    .audio = fm1core_pl_audio,
    .midi_in = fm1core_pl_midi_in,
    .midi_out_take = fm1core_pl_midi_out_take,
    .dump = fm1core_pl_dump,
    .stats = fm1core_pl_stats,
    .ui_info = fm1core_pl_ui_info,
    .shutdown = fm1core_pl_shutdown,
    .halted = fm1core_pl_halted,
    .flash_stage = fm1core_pl_flash_stage,
    .flash = fm1core_pl_flash,
    .flash_dirty = fm1core_pl_flash_dirty,
    .flash_sync = fm1core_pl_flash_sync,
    .param_epoch = NULL,
    .knob_target = NULL,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_pl_keymap();
    return &CORE;
}
