/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot FM-1 core: the firmware unit of the ChoralRoot emulator, compiled here as is.
 *
 * The firmware is built as ONE translation unit (emu_fw.c includes emu_firmware.h, which includes the
 * firmware's own .c files, the host DSP build and the Mac HAL), exactly as the submodule's
 * tools/emu/build.sh does. Including it from here builds it from the unmodified, pinned submodule:
 * nothing is copied or patched. Its includes resolve through the target's include path
 * (cores/ChoralRootFM1/build/gen, firmware/src, tools/emu; see CMakeLists.txt).
 *
 * exit() and atexit() inside a plugin. The emulator ends its process in two places: emu_fw.c's boot guard
 * (BOOT_UBOOT: exit(3)) and emu_firmware.h's CR_REBOOT() (Options > Flash Data in SAFE MODE: exit(0)); and it
 * registers its flash / settings save with atexit(emu_fw_exit). In a host neither may touch the process, so both
 * names are redirected for this unit only:
 *   exit(code)  -> fm1core_exit: does what exit() would do to the device's state (runs the registered exit
 *                  handlers once, closes the flash file so nothing later reaches it), records the code, sets the
 *                  halted flag and RETURNS. The caller's code then runs on to its end (harmless: see below);
 *                  the descriptor's wrappers (core_choralroot_desc.c) turn tick / frame / idle / audio / MIDI into
 *                  no-ops from then on (audio: silence). Powering on again = reloading the module.
 *   atexit(fn)  -> fm1core_atexit: keeps fn for fm1core_cr_shutdown (the descriptor's shutdown). Ignored once
 *                  halted (the process would be gone: nothing registered after it runs).
 * What runs on after a returning exit():
 *   - the UBOOT path (emu_boot_guard, inside init): init continues with a normal power-on; the flash path was
 *     cleared by fm1core_exit, so the boot runs on a RAM-only flash and never writes the file (the emulator exits
 *     before emu_flash_open, so the file is untouched there too).
 *   - CR_REBOOT (cu_flash_erase, inside frame's cr_ui_input): the rest of the frame runs (cr_ui_frame, the draw);
 *     it is SAFE MODE (cr_safe), so no settings are saved, and the flash file is closed anyway. No memory is
 *     touched that a returning exit could corrupt: both sites are plain statements followed by ordinary code. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"                      /* (FM1CORE_FLASH_STAGED) */

static void fm1core_exit(int code);
static int fm1core_atexit(void (*fn)(void));
#define exit(code) fm1core_exit(code)
#define atexit(fn) fm1core_atexit(fn)

/* Booting from bytes (fm1core.h flash_stage). emu_hal_fw.h's emu_flash_open / emu_flash_sync do stdio on
 * emu_flash_path; when that path is FM1CORE_FLASH_STAGED the "file" is the stage below instead, exactly as the web
 * build's emu_web.c does: the open "reads" the staged bytes into emu_flash[], every later write-through is dropped
 * (emu_flash[] is the truth; the host reads it with flash()). Any other path is real stdio. */
#define FM1CORE_STAGE_SIZE 0x100000u
static uint8_t fm1core_stage[FM1CORE_STAGE_SIZE];
static uint32_t fm1core_stage_len;        /* bytes staged (0: a fresh, erased flash) */
static uint32_t fm1core_stage_pos;
static FILE *const FM1CORE_STAGE_F = (FILE *)&fm1core_stage_pos;   /* a handle nothing dereferences */
static FILE *fm1core_fopen(const char *path, const char *mode)
{
    if (strcmp(path, FM1CORE_FLASH_STAGED))
        return fopen(path, mode);
    fm1core_stage_pos = 0;
    return mode[0] == 'r' && !fm1core_stage_len ? NULL : FM1CORE_STAGE_F;
}
static size_t fm1core_fread(void *dst, size_t sz, size_t n, FILE *f)
{
    size_t want = sz * n, have;
    if (f != FM1CORE_STAGE_F)
        return fread(dst, sz, n, f);
    have = fm1core_stage_pos < fm1core_stage_len ? fm1core_stage_len - fm1core_stage_pos : 0;
    if (want > have)
        want = have;
    memcpy(dst, fm1core_stage + fm1core_stage_pos, want);
    fm1core_stage_pos += (uint32_t)want;
    return sz ? want / sz : 0;
}
static size_t fm1core_fwrite(const void *src, size_t sz, size_t n, FILE *f)
{
    return f == FM1CORE_STAGE_F ? n : fwrite(src, sz, n, f);
}
static int fm1core_fseek(FILE *f, long off, int wh) { return f == FM1CORE_STAGE_F ? 0 : fseek(f, off, wh); }
static int fm1core_fflush(FILE *f) { return f == FM1CORE_STAGE_F ? 0 : fflush(f); }
#define fopen fm1core_fopen
#define fread fm1core_fread
#define fwrite fm1core_fwrite
#define fseek fm1core_fseek
#define fflush fm1core_fflush

#include "../../cores/ChoralRootFM1/tools/emu/emu_fw.c"

#undef exit
#undef atexit
#undef fopen
#undef fread
#undef fwrite
#undef fseek
#undef fflush

/* ---- the glue (hidden: reached only through the descriptor) ---- */
#define FM1CORE_NATEXIT 8
static void (*fm1core_atexit_fn[FM1CORE_NATEXIT])(void);
static int fm1core_natexit;
static int fm1core_halt;                  /* 0 running; 0x100 | code once the firmware exited */
static int fm1core_down;                  /* the exit handlers ran (exit or shutdown) */

static int fm1core_atexit(void (*fn)(void))
{
    if (fm1core_halt || fm1core_natexit >= FM1CORE_NATEXIT)
        return fm1core_halt ? 0 : -1;
    fm1core_atexit_fn[fm1core_natexit++] = fn;
    return 0;
}

static void fm1core_run_exit(void)        /* what exit() does to the device: the handlers (last first), the file */
{
    if (fm1core_down)
        return;
    fm1core_down = 1;
    while (fm1core_natexit > 0)
        fm1core_atexit_fn[--fm1core_natexit]();
    if (emu_flash_f) {
        if (emu_flash_f != FM1CORE_STAGE_F) {
            fflush(emu_flash_f);
            fclose(emu_flash_f);
        }
        emu_flash_f = NULL;
    }
    emu_flash_path[0] = 0;                /* (a later emu_flash_open: RAM only) */
    fflush(stdout);
}

static void fm1core_exit(int code)
{
    if (fm1core_halt)
        return;
    printf("fm1core: the firmware called exit(%d): halted (reload the core to power on again)\n", code);
    fm1core_run_exit();
    fm1core_halt = 0x100 | (code & 0xFF);
}

int fm1core_cr_halted(void) { return fm1core_halt; }
void fm1core_cr_shutdown(void) { fm1core_run_exit(); }

/* ---- the flash (fm1core.h ABI 2) ---- */
uint8_t *fm1core_cr_flash(void) { return emu_flash; }
uint32_t fm1core_cr_flash_dirty(void) { return emu_flash_writes; }
int fm1core_cr_flash_stage(const uint8_t *bytes, uint32_t n)
{
    if (n > FM1CORE_STAGE_SIZE)
        n = FM1CORE_STAGE_SIZE;
    if (!bytes)
        n = 0;
    if (n)
        memcpy(fm1core_stage, bytes, n);
    fm1core_stage_len = n;
    return 1;
}
/* make the image current: the settings record the UI would save after its quiet time (cr_settings_poll waits
 * CRS_QUIET_MS after a change and CRS_IDLE_MS of silence) is captured and written now if it differs from the one in
 * the flash (crs_write: no erase when unchanged). 1: the image is current; 0: a save is still pending (a loop is
 * playing: the firmware defers flash erases then, CR_SETTINGS_BUSY). Not in SAFE MODE (nothing is saved there),
 * not before the power-on or after a halt. */
int fm1core_cr_flash_sync(void)
{
    if (fm1core_halt || !crs_loaded || cr_safe)
        return 1;
    cr_settings_save();
    return crs_pending ? 0 : 1;
}
