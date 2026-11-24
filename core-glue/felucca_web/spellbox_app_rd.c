/* SPDX-License-Identifier: GPL-3.0-only */
/* SPELLBOX's app with one reader of its knob row (for core_spellbox.c's feeder, which cannot see main.c's statics):
 * this unit replaces ../../cores/spellbox/src/main.c in core_spellbox.cmake's EXTRA_SOURCES. The app unmodified,
 * then the row as draw_knobs draws it (src/main.c:499): KN_NAME[page * 4 + i] and knob_text. No side effects. */
#include "main.c"                        /* (src/main.c: on the INCLUDES path) */

void fm1core_sb_app_knob(int i, const char **label, char *val);
void fm1core_sb_app_knob(int i, const char **label, char *val)   /* val: 16 bytes */
{
    int k = page * 4 + (i & 3);
    *label = KN_NAME[k];
    knob_text(val, k);
}
