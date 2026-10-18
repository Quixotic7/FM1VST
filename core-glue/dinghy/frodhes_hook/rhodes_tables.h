/* SPDX-License-Identifier: GPL-3.0-only */
/* Frodhes core (core_frodhes.c): MIDI out. web/web_hal.c drops it (its midi_send is a no-op), so the unit renames
 * that definition (#define midi_send fm1core_frd_web_midi_send) and puts the name back here: rhodes.c is the first
 * file after web_hal.c, and its #include "rhodes_tables.h" finds this file first (the -iquote directory of
 * core_frodhes.cmake); the app's calls (notes.c, arp.c, frodhes.c) then reach the glue's midi_send. The real header
 * (firmware/gen) follows. */
#undef midi_send
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2);
#include_next "rhodes_tables.h"
