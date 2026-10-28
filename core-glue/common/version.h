/* SPDX-License-Identifier: MPL-2.0 */
/* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* What CMake's fm1_add_core passes in (core-glue/common/README.md "version"): every unit of a core is compiled with
 *   FM1CORE_ID="<id>"            FM1CORE_VERSION="<the firmware's version>"   FM1CORE_COMMIT="<the submodule's HEAD>"
 *   FM1CORE_FAMILY="<family>"    FM1CORE_LICENSE="<SPDX>"                     FM1CORE_GATED="<reason>" (only if gated)
 *   FM1CORE_HAS_CREDITS (with CREDITS / CREDITS_FILES: fm1core_credits.h's FM1CORE_CREDITS, FM1CORE_CREDITS_OR_NULL)
 * (and, for the older glues, FM1CORE_<PREFIX>_VERSION / _COMMIT). VERSION "x" in fm1_add_core sets the version
 * directly, else VERSION_FILE / VERSION_REGEX read it from the firmware's sources.
 * APP_VER: the version string of firmwares built with `tools/build.py --release` (Dinghy: APP_VER, default "0.0"),
 * defined here from FM1CORE_VERSION unless the core's DEFINES set it. Include this before the firmware's sources. */
#pragma once

#ifndef FM1CORE_ID
#define FM1CORE_ID "unknown"
#endif
#ifndef FM1CORE_VERSION
#define FM1CORE_VERSION "dev"
#endif
#ifndef FM1CORE_COMMIT
#define FM1CORE_COMMIT "unknown"
#endif
#ifndef FM1CORE_FAMILY
#define FM1CORE_FAMILY FM1CORE_ID
#endif
#ifndef FM1CORE_LICENSE
#define FM1CORE_LICENSE "GPL-3.0-only"
#endif
#ifdef FM1CORE_GATED
#define FM1CORE_GATED_OR_NULL FM1CORE_GATED
#else
#define FM1CORE_GATED_OR_NULL ((const char *)0)
#endif
/* the descriptor's credits (ABI 6): fm1_add_core CREDITS / CREDITS_FILES write them into a generated header */
#ifdef FM1CORE_HAS_CREDITS
#include "fm1core_credits.h"
#define FM1CORE_CREDITS_OR_NULL FM1CORE_CREDITS
#else
#define FM1CORE_CREDITS_OR_NULL ((const char *)0)
#endif
#ifndef APP_VER
#define APP_VER FM1CORE_VERSION
#endif
/* "https://github.com/o/r" -> "https://github.com/o/r@<commit>" (the descriptor's source_url) */
#define FM1CORE_SOURCE_URL(repo) repo "@" FM1CORE_COMMIT
