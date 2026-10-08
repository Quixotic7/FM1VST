/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core's descriptor (core-api/fm1core.h): the one exported symbol of melodee.fm1core. Its own
 * translation unit, as core_choralroot_desc.c: the firmware unit (core_melodee.c) defines emu_hal, emu_keymap and
 * the emu_fw_* hooks with external (hidden) linkage, reached here by their emu_hooks.h declarations. The clock entry
 * points are wrapped: once the firmware has called exit() they do nothing and the audio is silence. */
#include <string.h>
#include "fm1core.h"

#ifndef FM1CORE_ME_VERSION
#define FM1CORE_ME_VERSION "dev"
#endif
#ifndef FM1CORE_ME_COMMIT
#define FM1CORE_ME_COMMIT "unknown"
#endif

int fm1core_me_halted(void);              /* core_melodee.c */
void fm1core_me_shutdown(void);
uint8_t *fm1core_me_flash(void);
uint32_t fm1core_me_flash_dirty(void);
int fm1core_me_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_me_flash_sync(void);
const fm1param_t *fm1core_me_params(uint32_t *n);   /* core_melodee_params.c (in the firmware unit) */
uint32_t fm1core_me_param_epoch(void);

static void w_tick(uint32_t ms)
{
    if (!fm1core_me_halted())
        emu_fw_tick(ms);
}
static void w_frame(void)
{
    if (!fm1core_me_halted())
        emu_fw_frame();
}
static void w_idle(void)
{
    if (!fm1core_me_halted())
        emu_fw_idle();
}
static void w_audio(int16_t *stereo, uint32_t frames)
{
    if (fm1core_me_halted())
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
    else
        emu_fw_audio(stereo, frames);
}
static int w_midi_in(uint32_t pkt)
{
    return fm1core_me_halted() ? 1 : emu_fw_midi_in(pkt);   /* (halted: taken and dropped, never "retry") */
}
static int w_midi_out_take(uint32_t *pkt)
{
    return fm1core_me_halted() ? 0 : emu_fw_midi_out_take(pkt);
}

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = "melodee",
    .name = "Melodee",
    .version = FM1CORE_ME_VERSION,
    .source_url = "https://github.com/keremimo/melodee@" FM1CORE_ME_COMMIT,
    .flash_size = 0x100000u,
    /* the printed labels, EMU_B_* order (firmware/src/panel.c B_NAME calls the second one SCL: the scale layer) */
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .hal = &emu_hal,
    .keymap = &emu_keymap[0][0],
    .options = emu_fw_options,
    .boot_options = emu_fw_boot_options,
    .init = emu_fw_init,
    .tick = w_tick,
    .frame = w_frame,
    .idle = w_idle,
    .audio = w_audio,
    .midi_in = w_midi_in,
    .midi_out_take = w_midi_out_take,
    .dump = emu_fw_dump,
    .stats = emu_fw_stats,
    .ui_info = emu_fw_ui_info,
    .shutdown = fm1core_me_shutdown,
    .halted = fm1core_me_halted,
    .flash_stage = fm1core_me_flash_stage,
    .flash = fm1core_me_flash,
    .flash_dirty = fm1core_me_flash_dirty,
    .flash_sync = fm1core_me_flash_sync,
    .param_epoch = fm1core_me_param_epoch,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.params)                         /* (the map's ranges come from the firmware's tables: filled once) */
        CORE.params = fm1core_me_params(&CORE.nparams);
    return &CORE;
}
