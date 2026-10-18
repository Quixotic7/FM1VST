/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca core's descriptor (core-api/fm1core.h): the one exported symbol of felucca.fm1core. Its own
 * translation unit (-Wall), as core_choralroot_desc.c: the firmware unit (core_felucca.c) defines the panel and the
 * fm1core_fe_* entry points with external (hidden) linkage; they are declared here. */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

extern fm1panel_t fm1core_fe_panel;       /* core_felucca.c */
void fm1core_fe_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
int fm1core_fe_boot_options(int fail, const char *reason, int stage);
void fm1core_fe_init(int demo);
void fm1core_fe_tick(uint32_t ms);
void fm1core_fe_frame(void);
void fm1core_fe_idle(void);
void fm1core_fe_audio(int16_t *stereo, uint32_t frames);
int fm1core_fe_midi_in(uint32_t pkt);
int fm1core_fe_midi_out_take(uint32_t *pkt);
void fm1core_fe_dump(void);
void fm1core_fe_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_fe_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_fe_keymap(void);
int fm1core_fe_halted(void);
void fm1core_fe_shutdown(void);
uint8_t *fm1core_fe_flash(void);
uint32_t fm1core_fe_flash_dirty(void);
int fm1core_fe_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_fe_flash_sync(void);
const fm1param_t *fm1core_fe_params(uint32_t *n);   /* core_felucca_params.c (in the firmware unit) */
uint32_t fm1core_fe_param_epoch(void);
int32_t fm1core_fe_knob_target(int role);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = "Felucca",
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL("https://github.com/hugelton/Felucca"),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    /* the printed labels, FM1P_B_* order (firmware/src/panel.c B_NAME calls the second one SCL: the scale layer) */
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_fe_panel,
    .keymap = NULL,
    .options = fm1core_fe_options,
    .boot_options = fm1core_fe_boot_options,
    .init = fm1core_fe_init,
    .tick = fm1core_fe_tick,
    .frame = fm1core_fe_frame,
    .idle = fm1core_fe_idle,
    .audio = fm1core_fe_audio,
    .midi_in = fm1core_fe_midi_in,
    .midi_out_take = fm1core_fe_midi_out_take,
    .dump = fm1core_fe_dump,
    .stats = fm1core_fe_stats,
    .ui_info = fm1core_fe_ui_info,
    .shutdown = fm1core_fe_shutdown,
    .halted = fm1core_fe_halted,
    .flash_stage = fm1core_fe_flash_stage,
    .flash = fm1core_fe_flash,
    .flash_dirty = fm1core_fe_flash_dirty,
    .flash_sync = fm1core_fe_flash_sync,
    .param_epoch = fm1core_fe_param_epoch,
    .knob_target = fm1core_fe_knob_target,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_fe_keymap();
    if (!CORE.params)                         /* (the map's ranges come from the firmware's tables: filled once) */
        CORE.params = fm1core_fe_params(&CORE.nparams);
    return &CORE;
}
