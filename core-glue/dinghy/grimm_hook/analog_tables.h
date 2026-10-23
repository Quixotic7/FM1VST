/* SPDX-License-Identifier: GPL-3.0-only */
/* GR!MM core (core_grimm.c): MIDI out and the keys' glow. web/fm1_web.c drops both (its midi_send and fm1_led_glow are
 * no-ops: the page has no MIDI out and shows no glow), so the names are pointed at the glue here, after fm1_web.c has
 * defined them: dsp.c, the first file grimm.c includes after platform.c (the web shim), includes "analog_tables.h",
 * which finds this file first (the -iquote directory of core_grimm.cmake); the app's calls (grimm.c's MIDI out, the
 * FX layer's glow on the keys) then reach the glue. The real header (firmware/gen) follows. */
#define midi_send fm1core_grm_midi_send
#define fm1_led_glow fm1core_grm_led_glow
static void fm1core_grm_midi_send(uint32_t st, uint32_t d1, uint32_t d2);
static void fm1core_grm_led_glow(uint32_t id, int on);
#include_next "analog_tables.h"
