/* SPDX-License-Identifier: GPL-3.0-only */
/* GuitChordi's descriptor (core-api/fm1core.h ABI 6). Its own translation unit (-Wall), as core_sidou_desc.c: the
 * core unit (core_guitchordi.c) defines the panel and the fm1core_gc_* entry points with external (hidden) linkage.
 * The button names: the FM-1's printed labels (GuitChordi's app.c reads them by those roles: FX, SEL, ENV the lock,
 * EDIT, GLO, HOME, SAVE, ARP the play mode, PLAY, OCT- / OCT+). No boot guard (boot_options NULL: see the unit), no
 * parameter map: every knob a relative turn. */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

extern fm1panel_t fm1core_gc_panel;
void fm1core_gc_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
void fm1core_gc_init(int demo);
void fm1core_gc_tick(uint32_t ms);
void fm1core_gc_frame(void);
void fm1core_gc_idle(void);
void fm1core_gc_audio(int16_t *stereo, uint32_t frames);
int fm1core_gc_midi_in(uint32_t pkt);
int fm1core_gc_midi_out_take(uint32_t *pkt);
void fm1core_gc_dump(void);
void fm1core_gc_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_gc_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_gc_keymap(void);
int fm1core_gc_halted(void);
void fm1core_gc_shutdown(void);
uint8_t *fm1core_gc_flash(void);
uint32_t fm1core_gc_flash_dirty(void);
int fm1core_gc_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_gc_flash_sync(void);

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = "GuitChordi",
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL("https://github.com/stoogs/guitchordi"),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .credits = FM1CORE_CREDITS_OR_NULL,
    .flash_size = 0x100000u,
    /* the printed labels, FM1P_B_* order */
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_gc_panel,
    .keymap = NULL,
    .options = fm1core_gc_options,
    .boot_options = NULL,
    .init = fm1core_gc_init,
    .tick = fm1core_gc_tick,
    .frame = fm1core_gc_frame,
    .idle = fm1core_gc_idle,
    .audio = fm1core_gc_audio,
    .midi_in = fm1core_gc_midi_in,
    .midi_out_take = fm1core_gc_midi_out_take,
    .dump = fm1core_gc_dump,
    .stats = fm1core_gc_stats,
    .ui_info = fm1core_gc_ui_info,
    .shutdown = fm1core_gc_shutdown,
    .halted = fm1core_gc_halted,
    .flash_stage = fm1core_gc_flash_stage,
    .flash = fm1core_gc_flash,
    .flash_dirty = fm1core_gc_flash_dirty,
    .flash_sync = fm1core_gc_flash_sync,
    .param_epoch = NULL,
    .knob_target = NULL,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_gc_keymap();
    return &CORE;
}
