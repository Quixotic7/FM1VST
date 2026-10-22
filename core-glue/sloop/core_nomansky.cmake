# SPDX-License-Identifier: GPL-3.0-only
# No Man Sky FM 0.5 (https://github.com/hericdk/nomanskyFM): no tags or releases, the submodule is pinned at main
# 32e7f4c, the v0.5 commit (2026-10-20; its README: "Current version v0.5", no beta mark; release/nmsfm-0.5.fwsc).
# v0.5 saves projects as format 7 (the FX lane and the arp; format 6 projects still load).
# The firmware is in firmware/ (SOURCE): its own tools/build.py, firmware/src. assets/abertura.png (the opening
# picture) is not in the repo: tools/gen_abertura.py writes NMS_ABERTURA 0 and the game's opening skips the picture
# (the hub's package has it). The SLOOP-family glue (core-glue/sloop).
# Credit (the author's request on top of the GPL): "Features from No Man Sky FM by Hericdk
# (github.com/hericdk/nomanskyFM)".
# GATED: a fan-made demake of Hello Games' No Man's Sky: the game's name, and sprite art compiled into the firmware
# (assets/decolagem.png is the game's key art redrawn as pixel art; 0.5 adds assets/decola/*.png, nave-batalha.png
# and nave-cima-desenho.png, the ship drawn from a 3D model; assets/*.png carry no licence statement, the README's
# asset terms cover only the CC0 samples and the Hugelton assets). Un-gate: the author confirms the art's
# rights (or a build with the sprites left out: the generators write empty headers without the PNGs).
fm1_add_core(nomansky ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/nomanskyFM/firmware UNIT core_nomansky.c
             DESC core_nomansky_desc.c FAMILY sloop LICENSE GPL-3.0-only
             CREDITS "No Man Sky FM by Hericdk (github.com/hericdk/nomanskyFM): its author asks for this credit.\nBuilt on SLOOP by isod89, itself built on Felucca by Leo Kuroshita (Hugelton Instruments).\nA fan-made demake inspired by No Man's Sky: not affiliated with Hello Games."
             GATED "a fan-made demake of No Man's Sky (Hello Games): its sprite art (assets/*.png, one the game's key art redrawn) has no licence statement"
             GEN felucca_font.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h nms_abertura.h
                 nms_nave.h nms_decolagem.h nms_nave_cima.h nms_nave_batalha.h
                 nms_nave_decola.h nms_anao.h nms_nebulosa.h nms_nave_lado.h nms_cabine.h
             PYTHON_NEEDS numpy
             VERSION 0.5)

# the golden check: No Man Sky's own simulator unit (firmware/wasm/nms_wasm.c, built natively by nms_golden.c with
# its UI frames on our 15 ms clock) and the core run the same input (EDIT tapped at 2000 ms): the EDIT page at 2600 ms
# is the same outside its live areas, the parameter bars (x 0..119, y 40..140) and the two scopes (y 85..102 right,
# y 178..196), which follow the sound and the simulator's sample-driven clock (fm1_ms from rendered frames; one core:
# FELUCCA_CPU1 0); up to 64 more pixels may differ (~30 background sparkles do, for the same reason)
if(TARGET nomansky_core)
    fm1_sloop_ref(nomansky_ref_sim ${CMAKE_CURRENT_LIST_DIR}/nms_golden.c ${CMAKE_SOURCE_DIR}/cores/nomanskyFM/firmware
                  gen_nomansky FLAGS -ffreestanding -fno-builtin
                  INCLUDES ${CMAKE_SOURCE_DIR}/cores/nomanskyFM/firmware/wasm)
    fm1_sloop_golden(nomansky REF nomansky_ref_sim REF_ARGS %REF%/nms SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_nomansky.txt
                     DRIVE_ARGS --compare 2600 %REF%/nms_edit.ppm --ignore 0 40 119 140 --ignore 120 85 239 102
                                --ignore 0 178 239 196 --max-diff 64)
endif()
