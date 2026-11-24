/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca's Tier 2 map (core_felucca_params.c) in a fork's .rfm1, for its readouts and live knob labels (rd1: the
 * shim's bridge reads knob_target and the map's text). Included by a fork's unit after fw_core.h, only in an .rfm1 build
 * (FM1CORE_RFM1): the native plugin keeps the fork as it was (the panel only), since only what the bridge reads (names,
 * knob targets, get, text) has been checked against the fork's screens. Where a fork differs from Felucca 1.8.2 its unit
 * sets the map's FM1CORE_FE_* options (core_felucca_params.c) before this include. Its descriptor sets
 * FM1CORE_FW_PARAMS under the same condition. */
#ifdef FM1CORE_RFM1
#define fm1core_fe_params FM1CORE_FW(params)
#define fm1core_fe_param_epoch FM1CORE_FW(param_epoch)
#define fm1core_fe_knob_target FM1CORE_FW(knob_target)
#include "core_felucca_params.c"
#endif
