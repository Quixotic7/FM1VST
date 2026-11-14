# SPDX-License-Identifier: GPL-3.0-only
# Tessera (https://github.com/Rastapopolix/tessera, Theo Dainis): generative firmware grown from Melodee (Tess), a
# fork of Melodee (keremimo), itself built on Felucca. Its sources keep Melodee's shape (firmware/src/melodee.c, the
# same HAL, storage, editor.c and ota.c), so it is the melodee family's second core: tessera_fw.c / tessera_firmware.h
# over the family's melodee_hal.h, melodee_glue.h and melodee_desc.h (README.md).
# The version is the tag's (v3.5.3): the source's MELODEE_VERSION default is stale (build.py --release stamps it).
# The release's options (tools/build.py build_app, .github/workflows/release.yml): FM6, PHYS and TRIO in; SID, DRUM's
# SID KIT and MACRO (Plaits) out; the Mono look; the help texts packed. So reSID's tables and Plaits' sources are not
# compiled in. No hardware synth's factory sounds: ORACLE's presets are Tessera's own (oracle_sounds.h), the FM6
# patches Felucca's, Melodee's and ZEN-1's / FuMi-1's (GPL); Melodee's CZ-1 and Prophet banks are gone.
fm1_add_core(tessera ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/tessera UNIT core_tessera.c
             DESC core_tessera_desc.c FAMILY melodee LICENSE GPL-3.0-only
             CREDITS "Tessera by Theo Dainis (Rastapopolix), grown from Melodee (Tess), a fork of Melodee by Kerem Kilic (Ellic Studio), itself built on Felucca by Leo Kuroshita (Hügelton Instruments). GPL-3.0-only. The FM6 engine restates MSFA (Apache-2.0, Google) and Dexed (Pascal Gauthier, GPL-3.0-or-later). FM6 patches from ZEN-1 by aceyprime and FuMi-1 by cartesive. PHYS ports DaisySP (Electrosmith, Emilie Gillet) and Rings (Emilie Gillet), MIT. The ATLAS generator restates Grids by Emilie Gillet (GPL-3.0-or-later). The NOTE LENGTH page after FM-1 Studio Tess by Inputsignwave. Boot and update parts from ChoralRoot FM-1. The UI font is Chakra Petch (SIL OFL 1.1). Generator ideas after EuroPi, initenere, Fall, Constellation, norgard, less concepts, Triple Sloth and ochd (no code copied). The full notices: LICENSING.md."
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h melodee_tables.h pid_help.h melodee_fm6.h
                 x0x_drum_tables.h
             EXTRA_GEN "tools/gen_909_tables.py build/gen/x0x_drum_tables.h"
             VERSION 3.5.3
             DEFINES MELODEE_FM6=1 MELODEE_SID=0 MELODEE_SID_KIT=0 MELODEE_PHYS=1 MELODEE_PLAITS=0 MELODEE_TRIO=1
                     MELODEE_LOOK=1 MELODEE_HELP_PACKED=1)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/core_tessera.c PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Werror=array-bounds;-Wno-unused-function;-Wno-unused-parameter;-Wno-missing-field-initializers;-Wno-sign-compare;-Wno-int-to-pointer-cast;-Wno-macro-redefined;-Wno-unused-but-set-variable;-Wno-unused-but-set-parameter;-Wno-bitwise-op-parentheses;-Wno-shift-negative-value;-Wno-unused-variable")

# the .rfm1 (cmake/FM1Rfm1.cmake): Tessera 3.5.3 as its dual-core release builds it, the hardware's parts left out
# (USB audio, the USB host, the serial console, the update). Its licence texts are the default set: LICENSE
# (GPL-3.0-only), each file of LICENSES/ (Apache-2.0 msfa and JieLi, MIT DaisySP / libDaisy / Rings / Fukiai, OFL Chakra Petch /
# Inter Tight / Rubik) and LICENSING.md
fm1_add_rfm1(tessera AUTHORS "Theo Dainis (Rastapopolix)" MEMORY_MIB 16)
