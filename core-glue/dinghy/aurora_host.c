/* SPDX-License-Identifier: GPL-3.0-only */
/* AURORA's computer-side driver for the AURORA core (core_aurora.c), in place of its tests/host.c. platform.c includes
 * tests/fake_fm1.h (the fake FM-1 the browser and the host share: the panel, the screen, the MIDI in queue, the RAM
 * settings and bank, the second core as a serial call) and then, under DINGHY_HOST, "../../tests/host.c" by a
 * relative path: that file is a program (a script, a WAV, PPM files, main(), exit at its end), so
 * core_aurora.cmake maps the path to this file with clang's -ivfsoverlay (as core_fm106.cmake does); the firmware
 * tree stays unmodified. What host.c supplies, here:
 *   platform_poll  the coroutine's yield (dinghy_pre.h): one pass of app_loop per device millisecond. The clock
 *                  (fm1_ms), the panel and the audio are the glue's (tick, hal_in, the ISR), not the poll's
 *   midi_send      a USB-MIDI packet into the glue's out queue (platform.c's midi_send: CIN = status >> 4)
 *   midi_sysex     a whole F0 .. F7 message (the sound dump, SAVE then GLO) as USB-MIDI SysEx packets
 * After Dinghy's tests/host.c (zvenson) and AURORA's (twochannels), GPL-3.0-only. */
static void platform_poll(void) { fm1core_dg_poll(); }
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { fm1core_dg_midi_out(st, d1, d2); }
static void midi_sysex(const uint8_t *d, uint32_t n) { fm1core_dg_sysex_out(d, n); }
