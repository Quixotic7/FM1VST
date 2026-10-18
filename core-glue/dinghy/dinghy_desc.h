/* SPDX-License-Identifier: GPL-3.0-only */
/* The Dinghy family's descriptor (core-api/fm1core.h): the one exported symbol of a Dinghy core module. A
 * core_<id>_desc.c defines FM1CORE_DG_NAME ("OberVave") and FM1CORE_DG_REPO (the repo URL) and includes this file;
 * it is its own translation unit (-Wall -Wextra, no firmware header): the firmware unit (dinghy_glue.h) defines the
 * panel and the fm1core_dg_* entry points with external (hidden) linkage. */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

#if !defined(FM1CORE_DG_NAME) || !defined(FM1CORE_DG_REPO)
#error "define FM1CORE_DG_NAME and FM1CORE_DG_REPO before including dinghy_desc.h"
#endif

extern fm1panel_t fm1core_dg_panel;
void fm1core_dg_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
void fm1core_dg_init(int demo);
void fm1core_dg_tick(uint32_t ms);
void fm1core_dg_frame(void);
void fm1core_dg_idle(void);
void fm1core_dg_audio(int16_t *stereo, uint32_t frames);
int fm1core_dg_midi_in(uint32_t pkt);
int fm1core_dg_midi_out_take(uint32_t *pkt);
void fm1core_dg_dump(void);
void fm1core_dg_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_dg_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_dg_keymap_get(void);
int fm1core_dg_halted(void);
void fm1core_dg_shutdown(void);
uint8_t *fm1core_dg_flash(void);
uint32_t fm1core_dg_flash_dirty(void);
int fm1core_dg_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_dg_flash_sync(void);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = FM1CORE_DG_NAME,
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL(FM1CORE_DG_REPO),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    /* Dinghy's labels (platform.c BTN_*: the printed ones), FM1P_B_* order */
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,                       /* (no parameter map yet: every knob a relative control) */
    .nparams = 0,
    .panel = &fm1core_dg_panel,
    .keymap = NULL,
    .options = fm1core_dg_options,
    .boot_options = NULL,                 /* (the boot guard is the device branch's: the host shims have none) */
    .init = fm1core_dg_init,
    .tick = fm1core_dg_tick,
    .frame = fm1core_dg_frame,
    .idle = fm1core_dg_idle,
    .audio = fm1core_dg_audio,
    .midi_in = fm1core_dg_midi_in,
    .midi_out_take = fm1core_dg_midi_out_take,
    .dump = fm1core_dg_dump,
    .stats = fm1core_dg_stats,
    .ui_info = fm1core_dg_ui_info,
    .shutdown = fm1core_dg_shutdown,
    .halted = fm1core_dg_halted,
    .flash_stage = fm1core_dg_flash_stage,
    .flash = fm1core_dg_flash,
    .flash_dirty = fm1core_dg_flash_dirty,
    .flash_sync = fm1core_dg_flash_sync,
    .param_epoch = NULL,
    .knob_target = NULL,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_dg_keymap_get();
    return &CORE;
}
