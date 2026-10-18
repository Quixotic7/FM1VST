/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot core's descriptor (core-api/fm1core.h): the one exported symbol of choralroot.fm1core.
 * Its own translation unit (-Wall), with no firmware header: the firmware unit (core_choralroot.c) defines the panel
 * and the fm1core_cr_* entry points with external (hidden) linkage; they are declared here. The clock entry points
 * there do nothing once the firmware has called exit() (the audio is silence). */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

extern fm1panel_t fm1core_cr_panel;       /* core_choralroot.c */
void fm1core_cr_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
int fm1core_cr_boot_options(int fail, const char *reason, int stage);
void fm1core_cr_init(int demo);
void fm1core_cr_tick(uint32_t ms);
void fm1core_cr_frame(void);
void fm1core_cr_idle(void);
void fm1core_cr_audio(int16_t *stereo, uint32_t frames);
int fm1core_cr_midi_in(uint32_t pkt);
int fm1core_cr_midi_out_take(uint32_t *pkt);
void fm1core_cr_dump(void);
void fm1core_cr_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_cr_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_cr_keymap(void);
int fm1core_cr_halted(void);
void fm1core_cr_shutdown(void);
uint8_t *fm1core_cr_flash(void);
uint32_t fm1core_cr_flash_dirty(void);
int fm1core_cr_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_cr_flash_sync(void);
const fm1param_t *fm1core_cr_params(uint32_t *n);   /* core_choralroot_params.c (in the firmware unit) */
uint32_t fm1core_cr_param_epoch(void);
int32_t fm1core_cr_knob_target(int role);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = "ChoralRoot",
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL("https://github.com/Quixotic7/ChoralRootFM1"),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    /* ChoralRoot's roles of the printed buttons (tools/emu/README.md "Not done / stubbed"), FM1P_B_* order:
     * FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ */
    .button_names = {"FX", "KEY", "BASS", "LATCH", "EDIT", "OPT", "HOME", "SAVE", "PERF", "METRO", "LOOP", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_cr_panel,
    .keymap = NULL,
    .options = fm1core_cr_options,
    .boot_options = fm1core_cr_boot_options,
    .init = fm1core_cr_init,
    .tick = fm1core_cr_tick,
    .frame = fm1core_cr_frame,
    .idle = fm1core_cr_idle,
    .audio = fm1core_cr_audio,
    .midi_in = fm1core_cr_midi_in,
    .midi_out_take = fm1core_cr_midi_out_take,
    .dump = fm1core_cr_dump,
    .stats = fm1core_cr_stats,
    .ui_info = fm1core_cr_ui_info,
    .shutdown = fm1core_cr_shutdown,
    .halted = fm1core_cr_halted,
    .flash_stage = fm1core_cr_flash_stage,
    .flash = fm1core_cr_flash,
    .flash_dirty = fm1core_cr_flash_dirty,
    .flash_sync = fm1core_cr_flash_sync,
    .param_epoch = fm1core_cr_param_epoch,
    .knob_target = fm1core_cr_knob_target,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_cr_keymap();
    if (!CORE.params)                         /* (the map's ranges come from the firmware's tables: filled once) */
        CORE.params = fm1core_cr_params(&CORE.nparams);
    return &CORE;
}
