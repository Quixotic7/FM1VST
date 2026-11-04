/* SPDX-License-Identifier: GPL-3.0-only */
/* A stand-in core for tools/rfm1/rfm1_labels_test.py: just a descriptor (no sound), and with -DLIVE=1 live labels of
 * its own (docs/DYNAMIC-LABELS.md: epoch 7, FX = "LIVE", K1 = "LCUT", the rest NULL), to show that a firmware's
 * RemoteFM1Metadata.json labels never take over from a core that has live labels. Only --describe runs it. */
#include <stddef.h>
#include <stdint.h>

#include "fm1core.h"

static fm1panel_t panel;
static const int8_t keymap[6 * FM1P_NCOL];
static const fm1core_t desc = {
    .abi_version = FM1CORE_ABI,
    .id = "labelsfix",
    .name = "Labels fixture",
    .version = "1.0",
    .source_url = "https://example.com/labelsfix@0123456789ab",
    .license = "GPL-3.0-only",
    .flash_size = 4096,
    .button_names = {"GFX", NULL, "GENV"},
    .panel = &panel,
    .keymap = keymap,
};

const fm1core_t *fm1core_get(uint32_t abi) { return abi == FM1CORE_ABI ? &desc : NULL; }

#if LIVE
uint32_t fm1core_labels_epoch(void) { return 7; }
const char *fm1core_label(int32_t which)
{
    return which == FM1CORE_LBL_BTN0 + FM1P_B_FX ? "LIVE" : which == FM1CORE_LBL_KNOB0 + FM1P_E_K1 ? "LCUT" : NULL;
}
#endif
