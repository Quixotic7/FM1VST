# SPDX-License-Identifier: GPL-3.0-only
# MONO2K 1.9 (https://github.com/InigoHernandez/mono2k, tag v1.9 = b443b00): Sequencist's six mono machines after
# the Monomachine. Not Felucca's web/emu/felucca_web.c (MONO2K has none, so not fw_core.h either): its core
# (firmware/src/m2k_core.c, the unity build behind m2k.h) driven by core_mono2k.c as the FM-1 build's
# firmware/fm1/m2k_fm1.c drives it, with that build's own store and Patch Editor link (firmware/fm1/m2k_link.c) and
# update entry (firmware/fm1/src/ota.c), all unmodified (cmake/FM1Cores.cmake fm1_add_core). The family's descriptor
# (fw_desc.h), as Hortator's.
# Built as tools/fm1/build.py builds the FM-1's core (CORE): M2K_FW (the store a stream into the flash),
# M2K_PATTERNS 64, M2K_PAT_SLOTS 2, M2K_PROJECTS 1, M2K_IMG_DATA 47104 (one project of 64 patterns, two unpacked:
# the store the FM-1 and its Patch Editor exchange), M2K_VER / APP_VER the release's (build.py --release); POOL empty
# (the FM-1's .pool section: plain statics here, as m2k.h has them off the device). Not M2K_MUL_OUT (the 64-bit
# products out of line for the FM-1's CPU fault: the same results inline). The headers in firmware/gen are committed
# (NO_GEN). The version: web/firmware/mono2k.json, the FM-1 package the site and the installer offer.
fm1_add_core(mono2k ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/mono2k UNIT core_mono2k.c
             DESC core_mono2k_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "MONO2K by Iñigo Hernandez (Sequencist), GPL-3.0-only. Its FM-1 platform parts compiled in here (the update entry ota.c) by Leo Kuroshita (Hügelton Instruments, Felucca), as gathered in CS-1 (twochannels) on Dinghy (zvenson) and SLOOP (isod89). Grids' map and rules ported from Mutable Instruments' Grids by Emilie Gillet (GPL-3.0). The DRM- drums after the Erica Synths LXR drum voices (GPL-3.0). The screen's type: the Elektron Pixel Font, a FontStruction by savingaurora (CC BY-SA 3.0), adapted for MONO2K by Iñigo Hernandez (CC BY-SA 4.0). The wordmark: Orbitron (The Orbitron Project Authors, SIL OFL 1.1). The 64-wave bank is the author's own. Monomachine is a trademark of Elektron, and MONO2K is not made or endorsed by Elektron or M-VAVE."
             CREDITS_FILES assets/fonts/mono2k-pixel/ADAPTATION.md assets/fonts/mono2k-pixel/license.txt
             NO_GEN
             VERSION_FILE web/firmware/mono2k.json VERSION_REGEX "\"version\": *\"([^\"]*)\""
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/mono2k/firmware/gen
             DEFINES M2K_FW=1 M2K_PATTERNS=64 M2K_PAT_SLOTS=2 M2K_PROJECTS=1 M2K_IMG_DATA=47104 POOL=
                     M2K_VER="1.9" APP_VER="1.9")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in, MONO2K's GPL,
# the platform's licensing notes (CS-1's, kept in firmware/fm1/LICENSES: ota.c's), the pixel font's CC BY-SA
# (its adaptation note and the original's licence), Orbitron's OFL (the wordmark). Not the Terminus font
# (firmware/fm1/gen/felucca_font.h: the platform's crash and update text, not in the core), Inter Tight (the site),
# the JieLi SDK's Apache-2.0 (device packages only). RUN_ARGS: PLAY pressed once the splash and its wipe are over
# (2.5 s; a press during the splash only skips it): bank A's A01 plays.
fm1_add_rfm1(mono2k AUTHORS "Iñigo Hernandez (Sequencist)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "The FM-1 platform's licensing notes ({name})=firmware/fm1/LICENSES/CS-1-LICENSING.md"
                           "CC-BY-SA-4.0 (the pixel font in {name}, adapted)=assets/fonts/mono2k-pixel/ADAPTATION.md"
                           "CC-BY-SA-3.0 (the Elektron Pixel Font by savingaurora)=assets/fonts/mono2k-pixel/license.txt"
                           "OFL-1.1 (the Orbitron wordmark in {name})=assets/fonts/orbitron/OFL.txt"
             RUN_ARGS --press PLAY@3000)
