/* SPDX-License-Identifier: GPL-3.0-only */
/* Viscera's UI unit with one reader added (this file): core_viscera.cmake compiles THIS in
 * place of core/vsc_ui.c in _vs_units (same -w, same INCLUDES ${_vs}/core), so vsc_ui.c's statics (page, view, slot,
 * slot_text, intro_on ...) are visible here; the firmware file itself is unchanged. */
#include "vsc_ui.c"

/* knob_row's cell k (vsc_ui.c knob_row: slot(k), slot_text(s, n, v)): 1 given, 0 an empty slot (K_NONE), -1 no knob row
 * on the screen now (the intro; MIXER, INPUT, SAVE draw no knob_row) */
int vsc_ui_rd_col(unsigned k, char name[10], char val[10])
{
    unsigned s;
    name[0] = val[0] = 0;
    if (intro_on && (int32_t)(frame_ms - intro_at) < (int32_t)INTRO_MS)
        return -1;
    if (page == PG_MIXER || page == PG_INPUT || page == PG_SAVE)
        return -1;
    s = slot(k & 3u);
    if (s == K_NONE)
        return 0;
    slot_text(s, name, val);
    return 1;
}
