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
/* Arctic Scream 2.0 (core_arcticscream.c, FM1CORE_GRM_DEVICE_SLOTS): its web/fm1_web.c makes the browser's sample slots
 * 1 MiB (SMP_SLOT_SIZE, SAMPLE_AREA_LEN 4 MiB); the FM-1's are samples.c's own 72 KiB in platform.c's 288 KiB area. Set
 * back to the device's here, before samples.c (fm1_web.c's RAM area stays 4 MiB: the first 288 KiB used) */
#ifdef FM1CORE_GRM_DEVICE_SLOTS
#undef SMP_SLOT_SIZE
#undef SAMPLE_AREA_LEN
#define SAMPLE_AREA_LEN 0x00048000u
#endif
#include_next "analog_tables.h"
