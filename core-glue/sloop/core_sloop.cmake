# SPDX-License-Identifier: GPL-3.0-only
# SLOOP 2.7.1 (https://github.com/isod89/sloop-fm1, tag v2.7.1): the SLOOP-family glue (core-glue/sloop: sloop_hal.h,
# sloop_firmware.h, sloop_fw.c, sloop_core.c) over the unmodified submodule cores/sloop-fm1 (cmake/FM1Cores.cmake)
# GATED: the firmware compiles assets/icons.png (tools/gen_icons.py -> felucca_icons.h), the Hugelton icon atlas,
# all rights reserved; LICENSING.md's GPL section 7 permission to convey the combination asks to follow terms for
# the assets that are not published yet. Un-gate: those terms allow a plugin, or a build with replacement icons.
fm1_add_core(sloop ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/sloop-fm1 UNIT core_sloop.c
             DESC core_sloop_desc.c FAMILY sloop LICENSE GPL-3.0-only
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h
                 sloop_logo.h
             PYTHON_NEEDS numpy
             VERSION 2.7.1)
