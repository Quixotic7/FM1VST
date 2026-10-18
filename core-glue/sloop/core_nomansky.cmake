# SPDX-License-Identifier: GPL-3.0-only
# No Man Sky FM 0.4 (https://github.com/hericdk/nomanskyFM): no tags, the submodule is pinned at main 4304597 (the
# hub's catalog has no commit field: its nmsfm-0.4 package was published 2026-10-16T23:24:16Z, 37 s after 4304597,
# which is still main's head; 310960b before it is the v0.4 firmware commit, 4304597 only adds the site's recorder).
# The firmware is in firmware/ (SOURCE): its own tools/build.py, firmware/src. assets/abertura.png (the opening
# picture) is not in the repo: tools/gen_abertura.py writes NMS_ABERTURA 0 and the game's opening skips the picture
# (the hub's package has it). The SLOOP-family glue (core-glue/sloop).
# Credit (the author's request on top of the GPL): "Features from No Man Sky FM by Hericdk
# (github.com/hericdk/nomanskyFM)".
# GATED: a fan-made demake of Hello Games' No Man's Sky: the game's name, and sprite art compiled into the firmware
# (assets/decolagem.png is the game's key art redrawn as pixel art; assets/*.png carry no licence statement, the
# README's asset terms cover only the CC0 samples and the Hugelton assets). Un-gate: the author confirms the art's
# rights (or a build with the sprites left out: the generators write empty headers without the PNGs).
fm1_add_core(nomansky ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/nomanskyFM/firmware UNIT core_nomansky.c
             DESC core_nomansky_desc.c FAMILY sloop LICENSE GPL-3.0-only
             GATED "a fan-made demake of No Man's Sky (Hello Games): its sprite art (assets/*.png, one the game's key art redrawn) has no licence statement"
             GEN felucca_font.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h nms_abertura.h
                 nms_nave.h nms_decolagem.h nms_nave_cima.h nms_anao.h nms_nebulosa.h nms_nave_lado.h nms_cabine.h
             PYTHON_NEEDS numpy
             VERSION 0.4)
