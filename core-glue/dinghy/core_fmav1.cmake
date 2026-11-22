# SPDX-License-Identifier: GPL-3.0-only
# FMav-1 0.6.4 (https://github.com/wootr0p/fmav1, tag v0.6.4 = 945edb2): wootrop & gpix's semi-modular mono synth on
# OberVave 3.3.2 (zvenson), so on Dinghy's platform. The Dinghy family glue (core-glue/dinghy/) over the unmodified
# submodule cores/fmav1, on its own host shim tests/host.c (OBV_HOST + OBV_WASM, as OberVave's core: core_obervave.c is
# the model; the scaffold's felucca_web template does not fit this tree).
# The headers in firmware/gen are committed (NO_GEN); the version is tools/build.py's APP_VER (--release 0.6.4).
# The .pool section attribute Mach-O rejects becomes an alignment (FLAGS).
set(_fma ${CMAKE_SOURCE_DIR}/cores/fmav1)
set(_fma_ver 0.6.4)
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(fmav1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fma}
             UNIT core_fmav1.c DESC core_fmav1_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "FMav-1 by wootrop & gpix, on OberVave 3.3.2 by zvenson, over Dinghy (zvenson) and Felucca's platform by Leo Kuroshita (Hügelton Instruments) with SLOOP's changes (isod89), GPL-3.0-only. The ladder VCF's model and laws after FMini-1 by Ruppert (GPL-3.0-only), its calibration from the CA-72 project's measurements (GPL-3.0-or-later). The FM-1's own voice: Bigorna's formant synthesiser and dictionary by Nuno Zimas, GPL-3.0-only. The screen reader's protocol (announce.c) by Charles Vestal (fm1-base), MPL-2.0. The screen's text: Inter Tight by The Inter Project Authors and Terminus, SIL OFL 1.1. FMav-1 is an independent work, not made, endorsed or supported by M-VAVE or by any synthesizer maker."
             NO_GEN VERSION ${_fma_ver} DEFINES APP_VER="${_fma_ver}"
             INCLUDES ${_fma}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)")
dinghy_core(fmav1 core_fmav1.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, its LICENSING.md, the OFL of Inter Tight and of Terminus
# (the screen's text, rasterised into firmware/gen) and (0.6) the MPL-2.0 of fm1-base's announce.c (the screen reader,
# built in: the tree has no copy of the text, so licences/fmav1/MPL-2.0.txt, the standard text). Not the JieLi SDK's
# Apache-2.0 (device packages only).
fm1_add_rfm1(fmav1 AUTHORS "wootrop & gpix" "zvenson"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (Inter Tight, the screen's text in {name})=assets/fonts/InterTight-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "MPL-2.0 (fm1-base's announce.c in {name})=${CMAKE_CURRENT_LIST_DIR}/licences/fmav1/MPL-2.0.txt")
