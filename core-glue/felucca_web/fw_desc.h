/* SPDX-License-Identifier: GPL-3.0-only */
/* The felucca_web family's descriptor (core-api/fm1core.h): the one exported symbol of each <id>.fm1core. Its own
 * translation unit (-Wall -Wextra, no firmware header): the firmware unit (fw_core.h) defines the panel and the
 * fm1core_<P>_* entry points with external (hidden) linkage; they are declared here. A core's desc file sets
 *   FM1CORE_FW_P        the prefix (as its unit)                                              (required)
 *   FM1CORE_FW_NAME     "Felucca", "GHOULBOX", ..: for menus                                  (required)
 *   FM1CORE_FW_REPO     "https://github.com/<o>/<r>" (the commit is appended)                 (required)
 *   FM1CORE_FW_PARAMS   1: the unit has a Tier 2 map (fm1core_<P>_params / _param_epoch / _knob_target)   (0)
 *   FM1CORE_FW_BOOTGUARD 0: no boot_options (as the unit)                                     (1)
 *   FM1CORE_FW_SCL      the second button's name                                              ("SCL", panel.c's)
 * then includes this file. */
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

#ifndef FM1CORE_FW_PARAMS
#define FM1CORE_FW_PARAMS 0
#endif
#ifndef FM1CORE_FW_BOOTGUARD
#define FM1CORE_FW_BOOTGUARD 1
#endif
#ifndef FM1CORE_FW_SCL
#define FM1CORE_FW_SCL "SCL"
#endif
#define FM1CORE_FW_CAT2(a, b) a##b
#define FM1CORE_FW_CAT(a, b) FM1CORE_FW_CAT2(a, b)
#define FM1CORE_FW(n) FM1CORE_FW_CAT(FM1CORE_FW_CAT(fm1core_, FM1CORE_FW_P), FM1CORE_FW_CAT(_, n))

extern fm1panel_t FM1CORE_FW(panel);
void FM1CORE_FW(options)(const char *flash_path, int no_flash, int save_on_exit, int headless);
int FM1CORE_FW(boot_options)(int fail, const char *reason, int stage);
void FM1CORE_FW(init)(int demo);
void FM1CORE_FW(tick)(uint32_t ms);
void FM1CORE_FW(frame)(void);
void FM1CORE_FW(idle)(void);
void FM1CORE_FW(audio)(int16_t *stereo, uint32_t frames);
int FM1CORE_FW(midi_in)(uint32_t pkt);
int FM1CORE_FW(midi_out_take)(uint32_t *pkt);
void FM1CORE_FW(dump)(void);
void FM1CORE_FW(stats)(uint32_t *shed, uint32_t *cpu_pct);
void FM1CORE_FW(ui_info)(char *buf, uint32_t n);
const int8_t *FM1CORE_FW(keymap)(void);
int FM1CORE_FW(halted)(void);
void FM1CORE_FW(shutdown)(void);
uint8_t *FM1CORE_FW(flash)(void);
uint32_t FM1CORE_FW(flash_dirty)(void);
int FM1CORE_FW(flash_stage)(const uint8_t *bytes, uint32_t n);
int FM1CORE_FW(flash_sync)(void);
#if FM1CORE_FW_PARAMS
const fm1param_t *FM1CORE_FW(params)(uint32_t *n);
uint32_t FM1CORE_FW(param_epoch)(void);
int32_t FM1CORE_FW(knob_target)(int role);
#endif

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = FM1CORE_FW_NAME,
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL(FM1CORE_FW_REPO),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    /* the firmware's labels, FM1P_B_* order: firmware/src/panel.c B_NAME (the same in all five) */
    .button_names = {"FX", FM1CORE_FW_SCL, "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &FM1CORE_FW(panel),
    .keymap = NULL,
    .options = FM1CORE_FW(options),
#if FM1CORE_FW_BOOTGUARD
    .boot_options = FM1CORE_FW(boot_options),
#else
    .boot_options = NULL,
#endif
    .init = FM1CORE_FW(init),
    .tick = FM1CORE_FW(tick),
    .frame = FM1CORE_FW(frame),
    .idle = FM1CORE_FW(idle),
    .audio = FM1CORE_FW(audio),
    .midi_in = FM1CORE_FW(midi_in),
    .midi_out_take = FM1CORE_FW(midi_out_take),
    .dump = FM1CORE_FW(dump),
    .stats = FM1CORE_FW(stats),
    .ui_info = FM1CORE_FW(ui_info),
    .shutdown = FM1CORE_FW(shutdown),
    .halted = FM1CORE_FW(halted),
    .flash_stage = FM1CORE_FW(flash_stage),
    .flash = FM1CORE_FW(flash),
    .flash_dirty = FM1CORE_FW(flash_dirty),
    .flash_sync = FM1CORE_FW(flash_sync),
#if FM1CORE_FW_PARAMS
    .param_epoch = FM1CORE_FW(param_epoch),
    .knob_target = FM1CORE_FW(knob_target),
#else
    .param_epoch = NULL,
    .knob_target = NULL,
#endif
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = FM1CORE_FW(keymap)();
#if FM1CORE_FW_PARAMS
    if (!CORE.params)                     /* (the map's ranges come from the firmware's tables: filled once) */
        CORE.params = FM1CORE_FW(params)(&CORE.nparams);
#endif
    return &CORE;
}
