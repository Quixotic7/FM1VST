/* SPDX-License-Identifier: GPL-3.0-only */
/* The Melodee core: Melodee's firmware (https://github.com/keremimo/melodee, Felucca's fork with the FM6 and CZ-1
 * engines and USB audio; the pinned, unmodified submodule cores/melodee) built as one translation unit with this
 * repo's glue, the equivalent of ChoralRoot's emulator unit:
 *   melodee_fw.c        the emu_fw_* hooks (power-on, the 1 ms tick, the UI frame, audio, MIDI), as emu_fw.c
 *   melodee_firmware.h  the include list (hostsim.c's sound side, the HAL, audio.c, the UI, storage), as
 *                       emu_firmware.h
 *   melodee_hal.h       the HAL on the host (time, input, LCD, LEDs, the NOR image), as emu_hal_fw.h
 * Its includes resolve through the target's include path (cores/melodee/build/gen, firmware/src; see
 * CMakeLists.txt fm1_add_core).
 *
 * exit() and atexit() inside a plugin, as core_choralroot.c: the core ends its "process" where the device would
 * reset into UBOOT (two failed boots in a row, OCT- + OCT+ held 5 s, the USB UBOOT SysEx: exit(3)), and registers
 * its settings flush with atexit(emu_fw_exit). Both names are redirected for this unit:
 *   exit(code)  -> fm1core_exit: runs the registered exit handlers once, closes the flash file, records the code,
 *                  sets the halted flag and RETURNS; the call sites return right after it (melodee_fw.c), and the
 *                  descriptor's wrappers turn the clock into no-ops from then on (audio: silence).
 *   atexit(fn)  -> fm1core_atexit: keeps fn for the descriptor's shutdown.
 * The flash file (emu_flash_path) is stdio; FM1CORE_FLASH_STAGED boots from the bytes given to flash_stage()
 * instead, through the same fopen / fread / fwrite redirection as core_choralroot.c. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"

static void fm1core_exit(int code);
static int fm1core_atexit(void (*fn)(void));
#define exit(code) fm1core_exit(code)
#define atexit(fn) fm1core_atexit(fn)

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

#include "melodee_fw.c"

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
    emu_flash_path[0] = 0;
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

int fm1core_me_halted(void) { return fm1core_halt; }
void fm1core_me_shutdown(void) { fm1core_run_exit(); }

/* ---- the flash (fm1core.h ABI 2) ---- */
uint8_t *fm1core_me_flash(void) { return emu_flash; }
uint32_t fm1core_me_flash_dirty(void) { return emu_flash_writes; }
int fm1core_me_flash_stage(const uint8_t *bytes, uint32_t n)
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
/* make the image current: what settings_poll would save (the settings record: palette, panel table, LEDS, HOLD,
 * favourites, ..) is written now if it differs from the record in the flash (no erase when unchanged). Projects
 * and user presets are written by the firmware the moment they are saved (SAVE), so they are current already;
 * the song being edited is RAM on the device as well (lost at power-off unless SAVEd). 1: current; 0: the
 * firmware defers the write (it never erases while the transport runs: settings_poll's transport_busy) */
int fm1core_me_flash_sync(void)
{
    if (fm1core_halt || !emu_hal.ready || !flash_ok)
        return 1;
    persist_pending = 1;
    settings_poll();
    return persist_pending ? 0 : 1;
}

/* ---- the Tier 2 parameter map (in this unit: it reaches the firmware's statics) ---- */
#include "core_melodee_params.c"
