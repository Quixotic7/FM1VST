/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_uart.h on the host. The TRS jack's UART1: no bytes (MIDI comes in over "USB", the
 * core's midi_in). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

FM1_INLINE void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len)
{
    (void)ring;
    (void)len;
}
FM1_INLINE uint32_t fm1_uart1_rx_take(void) { return 0u; }
