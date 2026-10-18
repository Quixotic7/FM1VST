/* SPDX-License-Identifier: GPL-3.0-only */
/* No environment inside a core. Firmwares and their host sims read test switches from the environment (ChoralRoot's
 * EMU_C1=thread starts a core-1 pthread nobody joins; EMU_OUTPUT, EMU_STUB ..; OberVave's OBV_V2 / OBV_PERF; the
 * hostsim mains' SECS, PRESET ..): a plugin host's inherited environment must not reach any of them. The firmware
 * unit includes this header after exit.h (so after <stdlib.h>, whose prototype must not see the macro) and before
 * the firmware's sources: inside the unit getenv() is NULL for every name, so each switch takes the path its
 * emulator takes with the variable unset (the reference runs of the golden tests do the same). */
#pragma once
#include <stdlib.h>

static inline char *fm1core_getenv(const char *name)
{
    (void)name;
    return (char *)0;
}
#define getenv fm1core_getenv
