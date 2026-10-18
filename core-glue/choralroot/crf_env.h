/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot family's firmware units read no environment (included before the fork's emu_fw.c, after
 * <stdlib.h>). The emulators' test switches are environment variables read at power-on and in the tick:
 *   EMU_C1=thread   cr_cpu1.h runs core 1's job on a pthread that is never joined (a plugin unloading the module
 *                   with it parked would leave a thread in unmapped code); =nostart / =hang: the second core
 *                   broken on purpose
 *   EMU_OUTPUT, EMU_CHORUS, EMU_MASTER, EMU_SHED_OVER / _AT, EMU_STAGES, EMU_STUB: comparison and overload models
 *   PM_EMU_MIDI, PM_RIG_DEMO (PurpleMonkey): a scripted MIDI keyboard, a placeholder doll
 * A host's inherited environment must not reach any of them: inside the core getenv() is NULL for every name, so
 * each takes the path the emulator takes with the variable unset (the reference runs of the tests do the same). */
#pragma once
#include <stdlib.h>

static inline char *fm1core_crf_getenv(const char *name)
{
    (void)name;
    return (char *)0;
}
#define getenv fm1core_crf_getenv
