/* SPDX-License-Identifier: GPL-3.0-only */
/* The melodee family's descriptor (core-api/fm1core.h): the one exported symbol of a core's module. A core's
 * core_<id>_desc.c (its own translation unit, -Wall) defines, then includes this:
 *   MELODEE_DESC_NAME     the firmware's display name ("Melodee", "Tessera")
 *   MELODEE_DESC_URL      its repository (FM1CORE_SOURCE_URL adds the commit)
 *   MELODEE_DESC_PARAMS   1: the unit has a Tier 2 parameter map (fm1core_me_params, _param_epoch, _knob_target);
 *                         0: none (every knob a relative control)
 * The firmware unit (core_<id>.c) defines the panel and the fm1core_me_* entry points with external (hidden)
 * linkage (melodee_glue.h); they are declared here. */
#pragma once
#ifndef MELODEE_DESC_NAME
#error "MELODEE_DESC_NAME: the firmware's display name"
#endif
#ifndef MELODEE_DESC_PARAMS
#define MELODEE_DESC_PARAMS 0
#endif
#include <stddef.h>
#include "fm1core.h"
#include "version.h"

extern fm1panel_t fm1core_me_panel;       /* melodee_glue.h, in the firmware unit */
void fm1core_me_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
int fm1core_me_boot_options(int fail, const char *reason, int stage);
void fm1core_me_init(int demo);
void fm1core_me_tick(uint32_t ms);
void fm1core_me_frame(void);
void fm1core_me_idle(void);
void fm1core_me_audio(int16_t *stereo, uint32_t frames);
int fm1core_me_midi_in(uint32_t pkt);
int fm1core_me_midi_out_take(uint32_t *pkt);
void fm1core_me_dump(void);
void fm1core_me_stats(uint32_t *shed, uint32_t *cpu_pct);
void fm1core_me_ui_info(char *buf, uint32_t n);
const int8_t *fm1core_me_keymap(void);
int fm1core_me_halted(void);
void fm1core_me_shutdown(void);
uint8_t *fm1core_me_flash(void);
uint32_t fm1core_me_flash_dirty(void);
int fm1core_me_flash_stage(const uint8_t *bytes, uint32_t n);
int fm1core_me_flash_sync(void);
#if MELODEE_DESC_PARAMS
const fm1param_t *fm1core_me_params(uint32_t *n);   /* the core's parameter map (in the firmware unit) */
uint32_t fm1core_me_param_epoch(void);
int32_t fm1core_me_knob_target(int role);
#endif

static fm1core_t CORE = {
    .abi_version = FM1CORE_ABI,
    .id = FM1CORE_ID,
    .name = MELODEE_DESC_NAME,
    .version = FM1CORE_VERSION,
    .source_url = FM1CORE_SOURCE_URL(MELODEE_DESC_URL),
    .family = FM1CORE_FAMILY,
    .license = FM1CORE_LICENSE,
    .gated = FM1CORE_GATED_OR_NULL,
    .credits = FM1CORE_CREDITS_OR_NULL,
    .flash_size = 0x100000u,
    /* the printed labels, FM1P_B_* order. firmware/src/panel.c B_NAME calls the second one SCL (the scale layer);
     * tests/plugin_test.cpp expects "SEL" for the Felucca-family cores, so it stays SEL until that test moves */
    .button_names = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                     "OCT-", "OCT+"},
    .params = NULL,
    .nparams = 0,
    .panel = &fm1core_me_panel,
    .keymap = NULL,
    .options = fm1core_me_options,
    .boot_options = fm1core_me_boot_options,
    .init = fm1core_me_init,
    .tick = fm1core_me_tick,
    .frame = fm1core_me_frame,
    .idle = fm1core_me_idle,
    .audio = fm1core_me_audio,
    .midi_in = fm1core_me_midi_in,
    .midi_out_take = fm1core_me_midi_out_take,
    .dump = fm1core_me_dump,
    .stats = fm1core_me_stats,
    .ui_info = fm1core_me_ui_info,
    .shutdown = fm1core_me_shutdown,
    .halted = fm1core_me_halted,
    .flash_stage = fm1core_me_flash_stage,
    .flash = fm1core_me_flash,
    .flash_dirty = fm1core_me_flash_dirty,
    .flash_sync = fm1core_me_flash_sync,
#if MELODEE_DESC_PARAMS
    .param_epoch = fm1core_me_param_epoch,
    .knob_target = fm1core_me_knob_target,
#else
    .param_epoch = NULL,                  /* (no parameter map: every knob a relative control) */
    .knob_target = NULL,
#endif
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    if (abi_version != FM1CORE_ABI)
        return NULL;
    if (!CORE.keymap)
        CORE.keymap = fm1core_me_keymap();
#if MELODEE_DESC_PARAMS
    if (!CORE.params)                         /* (the map's ranges come from the firmware's tables: filled once) */
        CORE.params = fm1core_me_params(&CORE.nparams);
#endif
    return &CORE;
}
