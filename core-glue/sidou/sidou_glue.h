/* SPDX-License-Identifier: GPL-3.0-only */
/* What the SIDOU core's units share (core-glue/sidou/README.md): the HAL shims (hal/) call into the core unit
 * (core_sidou.c) through these, since the firmware's platform files are separate translation units, each with its
 * own copy of the header-only HAL. Glue-only names carry fm1core_sd_ / FM1CORE_SD_. */
#pragma once
#include <stdint.h>

/* time: the device ms the ABI's tick gave, TIMER4 (24 MHz) from it; simulated time never moves inside a call */
uint32_t fm1core_sd_ms(void);
uint32_t fm1core_sd_ticks(void);
/* a wait for an interrupt (the main loop's `idle`, a producer waiting for the audio ISR to drain, the watchdog's
 * feed in a wait): the main loop's coroutine gives the clock back to the host. Outside it: nothing */
void fm1core_sd_wait(void);
int fm1core_sd_can_wait(void);                  /* 1 on the main loop's coroutine */
/* a reset (2), UBOOT or the update loader (3): exit() inside the core (common/exit.h): it halts */
void fm1core_sd_exit(int code);
/* the 1 MiB NOR image (common/nor.c) read in place: the XIP window */
uint8_t *fm1core_sd_nor(void);
/* the MASTER pot 0..1023 (the panel's), the battery's divider */
int32_t fm1core_sd_adc(uint32_t ch);
/* ALNK0's pending register as store_fm1.c reads it (bit 7: a block is due): 0, a block is never late here */
extern volatile uint8_t fm1core_sd_alnk_con2;

/* the key matrix's state as hal/fm1_input.h keeps it (input.c is its only includer), written by the core unit's
 * tick: the debounced keys and buttons, their press edges, the encoder detents, the scan's frame count */
#define FM1CORE_SD_NCOL 11u
#define FM1CORE_SD_NENC 7u
typedef struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    int16_t enc_steps[FM1CORE_SD_NENC]; /* + = clockwise */
    uint32_t frames;
} fm1core_sd_in_t;
extern volatile fm1core_sd_in_t fm1core_sd_in;
extern uint8_t fm1core_sd_led[FM1CORE_SD_NCOL];      /* fm1_led: lit, packed row bits per column */
extern uint8_t fm1core_sd_led_dim[FM1CORE_SD_NCOL];  /* fm1_led_dim: the glow */

/* the LCD (core-glue/sidou/sidou_lcd.c, in the place of platform/fm1/lcd.c): the panel's RAM, big-endian RGB565 as
 * sent, and its write count; on once DISPON was sent */
extern uint16_t fm1core_sd_lcd[240u * 240u];
extern uint32_t fm1core_sd_lcd_writes;
extern uint8_t fm1core_sd_lcd_on;

/* the MIDI unit (sidou_midi.c, over platform/fm1/midi.c and Felucca's usb.c): the host's USB-MIDI packets in, the
 * firmware's out; USB up and configured (a host is there) */
int fm1core_sd_midi_in(uint32_t pkt);
int fm1core_sd_midi_out_take(uint32_t *pkt);
void fm1core_sd_usb_attached(void);
void fm1core_sd_midi_dump(void);
