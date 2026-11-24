/* SPDX-License-Identifier: GPL-3.0-only */
/* tkdemo: a tiny test firmware for the Move takeover (docs/RFM1-TAKEOVER.md, tk1). The FM-1 side (tkdemo.c): a
 * screen and a triangle per held key; the Move side (tkdemo_move.c, only in a build with FM1_HOST_TK). */
#ifndef TKDEMO_H
#define TKDEMO_H
#include <stdint.h>
#include "fm1core.h"

extern fm1panel_t tkd_panel;
extern const int8_t tkd_keymap[6 * FM1P_NCOL];
extern uint8_t tkd_flash[4096];
void tkd_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
void tkd_init(int demo);
void tkd_tick(uint32_t ms);
void tkd_frame(void);
void tkd_idle(void);
void tkd_audio(int16_t *stereo, uint32_t frames);
int tkd_midi_in(uint32_t pkt);
int tkd_midi_out_take(uint32_t *pkt);
void tkd_nop(void);
void tkd_stats(uint32_t *shed, uint32_t *cpu);
void tkd_ui_info(char *buf, uint32_t n);
int tkd_halted(void);
int tkd_flash_stage(const uint8_t *bytes, uint32_t n);
uint8_t *tkd_flash_ptr(void);
uint32_t tkd_flash_dirty(void);
int tkd_flash_sync(void);

void tkd_move_init(void);      /* tkdemo_move.c: registers the takeover (a no-op without FM1_HOST_TK) */
#endif
