/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot FM-1 core: the firmware unit of the ChoralRoot emulator, compiled here as is.
 *
 * The firmware is built as ONE translation unit (emu_fw.c includes emu_firmware.h, which includes the
 * firmware's own .c files, the host DSP build and the Mac HAL), exactly as the submodule's
 * tools/emu/build.sh does. Including it from here builds it from the unmodified, pinned submodule:
 * nothing is copied or patched. Its includes resolve through the target's include path
 * (cores/ChoralRootFM1/build/gen, firmware/src, tools/emu; see CMakeLists.txt). */
#include "../../cores/ChoralRootFM1/tools/emu/emu_fw.c"
