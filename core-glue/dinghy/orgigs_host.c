/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs' computer-side driver for the orgigs core (core_orgigs.c), in place of its tests/host.c. platform.c includes
 * "../../tests/host.c" by a relative path under DINGHY_HOST: that file is a program (a script, a WAV, PPM files,
 * main(), exit at its end), so core_orgigs.cmake maps the path to this file with clang's -ivfsoverlay; the firmware
 * tree stays unmodified. The fake FM-1 itself is the firmware's own sim/sim_platform.h (shared by its tests/host.c
 * and its browser simulator): the panel (fm1_in, host_edges / host_released / host_note_edges, host_enc[], the LEDs
 * host_led[] + host_led_bg[]), the screen fb[] and gfx.c, the two settings sectors host_store[] and the MIDI in
 * queue mq[]. What it leaves to its host, here:
 *   midi_send            into the glue's out queue (USB-MIDI packets, CIN = status >> 4)
 *   sysex_send           a whole F0..F7 message into the glue's out queue (the device's ota_wire_send)
 *   host_settings_saved  a store_save happened: core_orgigs.c copies the changed sector into the NOR image
 *   platform_poll        the coroutine's yield (dinghy_pre.h): one pass of the main loop per device millisecond
 * After OrGigs' tests/host.c and sim/wasm_host.c (Bobo, with Claude), GPL-3.0-only. */
#include "sim_platform.h"

static uint8_t fm1core_org_saved;              /* store_save ran since the last pass */
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }
static int sysex_send(const uint8_t *p, uint32_t n)
{
    fm1core_dg_sysex_out(p, n);
    return 0;
}
static void host_settings_saved(void) { fm1core_org_saved = 1; }
static void platform_poll(void) { fm1core_dg_poll(); }
