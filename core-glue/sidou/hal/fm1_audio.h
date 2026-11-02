/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_audio.h on the host. main.c's ALNK0 interrupt is the core unit's audio(): one
 * 128-frame half per ABI block, always half 0, always due. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_AUDIO_HALF 0x80u
void fm1core_sd_audio_on(int on);
FM1_INLINE void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    (void)buf;
    (void)half_words;
    (void)isr;
    (void)prio;
    fm1core_sd_audio_on(1);
}
FM1_INLINE uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
FM1_INLINE void fm1_audio_ack_aux(uint8_t p) { (void)p; }
FM1_INLINE uint32_t fm1_audio_free_half(void) { return 0u; }
FM1_INLINE void fm1_audio_ack_half(void) {}
FM1_INLINE void fm1_audio_stop(void) { fm1core_sd_audio_on(0); }
