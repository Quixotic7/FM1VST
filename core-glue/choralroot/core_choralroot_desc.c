/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot core's descriptor (core-api/fm1core.h): the one exported symbol of choralroot.fm1core.
 * Its own translation unit: the firmware unit (core_choralroot.c) defines emu_hal, emu_keymap and the emu_fw_*
 * hooks with external (hidden) linkage, so they are reached here by their emu_hooks.h declarations. The clock
 * entry points are wrapped: once the firmware has called exit() (core_choralroot.c) they do nothing and the audio
 * is silence. */
#include <string.h>
#include "fm1core.h"

#ifndef FM1CORE_CR_VERSION
#define FM1CORE_CR_VERSION "dev"
#endif
#ifndef FM1CORE_CR_COMMIT
#define FM1CORE_CR_COMMIT "unknown"
#endif

int fm1core_cr_halted(void);              /* core_choralroot.c */
void fm1core_cr_shutdown(void);
uint8_t *fm1core_cr_flash(void);
uint32_t fm1core_cr_flash_dirty(void);
int fm1core_cr_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_cr_flash_sync(void);

static void w_tick(uint32_t ms)
{
    if (!fm1core_cr_halted())
        emu_fw_tick(ms);
}
static void w_frame(void)
{
    if (!fm1core_cr_halted())
        emu_fw_frame();
}
static void w_idle(void)
{
    if (!fm1core_cr_halted())
        emu_fw_idle();
}
static void w_audio(int16_t *stereo, uint32_t frames)
{
    if (fm1core_cr_halted())
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
    else
        emu_fw_audio(stereo, frames);
}
static int w_midi_in(uint32_t pkt)
{
    return fm1core_cr_halted() ? 1 : emu_fw_midi_in(pkt);   /* (halted: taken and dropped, never "retry") */
}
static int w_midi_out_take(uint32_t *pkt)
{
    return fm1core_cr_halted() ? 0 : emu_fw_midi_out_take(pkt);
}

static const fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = "choralroot",
    .name = "ChoralRoot",
    .version = FM1CORE_CR_VERSION,
    .source_url = "https://github.com/Quixotic7/ChoralRootFM1@" FM1CORE_CR_COMMIT,
    .flash_size = 0x100000u,
    /* ChoralRoot's roles of the printed buttons (tools/emu/README.md "Not done / stubbed"), EMU_B_* order:
     * FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ */
    .button_names = {"FX", "KEY", "BASS", "LATCH", "EDIT", "OPT", "HOME", "SAVE", "PERF", "METRO", "LOOP", "REC",
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
    .shutdown = fm1core_cr_shutdown,
    .halted = fm1core_cr_halted,
    .flash_stage = fm1core_cr_flash_stage,
    .flash = fm1core_cr_flash,
    .flash_dirty = fm1core_cr_flash_dirty,
    .flash_sync = fm1core_cr_flash_sync,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    return abi_version == FM1CORE_ABI ? &CORE : NULL;
}
