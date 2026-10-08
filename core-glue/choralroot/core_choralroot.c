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

static void fm1core_exit(int code);
static int fm1core_atexit(void (*fn)(void));
#define exit(code) fm1core_exit(code)
#define atexit(fn) fm1core_atexit(fn)

#include "../../cores/ChoralRootFM1/tools/emu/emu_fw.c"

#undef exit
#undef atexit

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
        fflush(emu_flash_f);
        fclose(emu_flash_f);
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
