/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot family's descriptor (core-api/fm1core.h): the one exported symbol of a family core. A descriptor
 * unit (core_<id>_desc.c: its own translation unit, -Wall -Wextra, no firmware header) defines
 *
 *   FM1CORE_CRF_P        the entry points' prefix (as its firmware unit: crf_glue.h)
 *   FM1CORE_CRF_NAME     "ChoralRoot": the menus' name
 *   FM1CORE_CRF_REPO     "https://github.com/..": source_url is it @ the submodule's commit
 *   FM1CORE_CRF_BUTTONS  the 14 labels, FM1P_B_* order (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   FM1CORE_CRF_PARAMS   1: the firmware unit has a Tier 2 map (fm1core_<P>_params / _param_epoch / _knob_target);
 *                        0: none (params NULL, every knob relative)
 *   FM1CORE_CRF_BOOT_OPTIONS  0: the fork has no boot guard (boot_options NULL); default 1
 *
 * and includes this file. */
#pragma once
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

#define FM1CORE_CRF_CAT_(a, b, c) a##b##c
#define FM1CORE_CRF_CAT(a, b, c) FM1CORE_CRF_CAT_(a, b, c)
#define FM1CORE_CRF(name) FM1CORE_CRF_CAT(fm1core_, FM1CORE_CRF_P, _##name)
#ifndef FM1CORE_CRF_BOOT_OPTIONS
#define FM1CORE_CRF_BOOT_OPTIONS 1
#endif

extern fm1panel_t FM1CORE_CRF(panel);
void FM1CORE_CRF(options)(const char *flash_path, int no_flash, int save_on_exit, int headless);
int FM1CORE_CRF(boot_options)(int fail, const char *reason, int stage);
void FM1CORE_CRF(init)(int demo);
void FM1CORE_CRF(tick)(uint32_t ms);
void FM1CORE_CRF(frame)(void);
void FM1CORE_CRF(idle)(void);
void FM1CORE_CRF(audio)(int16_t *stereo, uint32_t frames);
int FM1CORE_CRF(midi_in)(uint32_t pkt);
int FM1CORE_CRF(midi_out_take)(uint32_t *pkt);
void FM1CORE_CRF(dump)(void);
void FM1CORE_CRF(stats)(uint32_t *shed, uint32_t *cpu_pct);
void FM1CORE_CRF(ui_info)(char *buf, uint32_t n);
const int8_t *FM1CORE_CRF(keymap)(void);
int FM1CORE_CRF(halted)(void);
void FM1CORE_CRF(shutdown)(void);
uint8_t *FM1CORE_CRF(flash)(void);
uint32_t FM1CORE_CRF(flash_dirty)(void);
int FM1CORE_CRF(flash_stage)(const uint8_t *bytes, uint32_t n);
int FM1CORE_CRF(flash_sync)(void);
#if FM1CORE_CRF_PARAMS
const fm1param_t *FM1CORE_CRF(params)(uint32_t *n);   /* the map (in the firmware unit: its ranges are the firmware's) */
uint32_t FM1CORE_CRF(param_epoch)(void);
int32_t FM1CORE_CRF(knob_target)(int role);
#endif

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = FM1CORE_CRF_NAME,
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL(FM1CORE_CRF_REPO),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .flash_size = 0x100000u,
    .button_names = FM1CORE_CRF_BUTTONS,
    .params = NULL,
    .nparams = 0,
    .panel = &FM1CORE_CRF(panel),
    .keymap = NULL,
    .options = FM1CORE_CRF(options),
#if FM1CORE_CRF_BOOT_OPTIONS
    .boot_options = FM1CORE_CRF(boot_options),
#else
    .boot_options = NULL,
#endif
    .init = FM1CORE_CRF(init),
    .tick = FM1CORE_CRF(tick),
    .frame = FM1CORE_CRF(frame),
    .idle = FM1CORE_CRF(idle),
    .audio = FM1CORE_CRF(audio),
    .midi_in = FM1CORE_CRF(midi_in),
    .midi_out_take = FM1CORE_CRF(midi_out_take),
    .dump = FM1CORE_CRF(dump),
    .stats = FM1CORE_CRF(stats),
    .ui_info = FM1CORE_CRF(ui_info),
    .shutdown = FM1CORE_CRF(shutdown),
    .halted = FM1CORE_CRF(halted),
    .flash_stage = FM1CORE_CRF(flash_stage),
    .flash = FM1CORE_CRF(flash),
    .flash_dirty = FM1CORE_CRF(flash_dirty),
    .flash_sync = FM1CORE_CRF(flash_sync),
#if FM1CORE_CRF_PARAMS
    .param_epoch = FM1CORE_CRF(param_epoch),
    .knob_target = FM1CORE_CRF(knob_target),
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
        CORE.keymap = FM1CORE_CRF(keymap)();
#if FM1CORE_CRF_PARAMS
    if (!CORE.params)                         /* (the map's ranges come from the firmware's tables: filled once) */
        CORE.params = FM1CORE_CRF(params)(&CORE.nparams);
#endif
    return &CORE;
}
