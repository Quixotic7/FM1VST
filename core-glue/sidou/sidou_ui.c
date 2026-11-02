/* SPDX-License-Identifier: GPL-3.0-only */
/* platform/fm1/ui.c, unmodified, and what the core's flash_sync reads of it: the settings record (app/settings.h,
 * saved 5 s after the last change, or at once by SAVE) and LAB's edit as last written. */
#include "sidou_glue.h"
#include "ui.c"

int fm1core_sd_settings_current(void)
{
    return !persisted.asked && persisted.seen == persisted.saved && persisted.edit_seen == persisted.edit_saved;
}
