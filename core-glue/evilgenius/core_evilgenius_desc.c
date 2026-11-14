/* SPDX-License-Identifier: GPL-3.0-only */
/* EVILGENIUS's descriptor: its own core/glue/core_quadrille_desc.c (FIZZ's, kept by the derivative), compiled
 * unchanged, with two things it still has from FIZZ put right: the source_url (FIZZ's says "FMToneSeq (local)") names
 * the public repository at the commit built, and the name is EVILGENIUS (the unit's own SysEx identity, core_quadrille.c,
 * already says "EVILGENIUS <version>"). version.h is #pragma once, so the descriptor's include of it keeps this
 * FM1CORE_SOURCE_URL; its fm1core_get is renamed and wrapped (the CORE table is static in this unit). */
#include "fm1core.h"
#include "version.h"
#undef FM1CORE_SOURCE_URL
#define FM1CORE_SOURCE_URL(repo) "https://github.com/Quixotic7/evilgenius@" FM1CORE_COMMIT
#pragma push_macro("FM1CORE_EXPORT")
#undef FM1CORE_EXPORT
#define FM1CORE_EXPORT static
#define fm1core_get fm1core_eg_fizz_get
#include "core_quadrille_desc.c"
#undef fm1core_get
#pragma pop_macro("FM1CORE_EXPORT")

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version)
{
    CORE.name = "EVILGENIUS";
    return fm1core_eg_fizz_get(abi_version);
}
