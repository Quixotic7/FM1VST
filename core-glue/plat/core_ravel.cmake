# SPDX-License-Identifier: GPL-3.0-only
# Ravel (https://github.com/De-Pattern/Ravel_FM1, tag v1.34.1 = 227339b; GPL-3.0-only, its sources' SPDX headers), an
# 8-track groovebox on its own portable platform (firmware/app/plat.h) over X0X's Felucca layer, adapted
# (firmware/fm1): the plat glue (core-glue/plat/README.md) over the unmodified submodule. The device's platform unit
# (core_ravel.c: storage.c, ota.c, panel.c, plat_fm1.c with projstore.c) on the HAL half; the instrument
# (firmware/app/ravel_app.c) its own unit, as tools/build_fm1.py builds it (-O2, -ffp-contract=off). Generated: the
# screen font (tools/gen_rv_font.py, Pillow); the wavetables, organ spectra and shapes are committed headers.
# Scaffolded by RFM1Manager from fimba's glue, ported by hand.
set(_rv ${CMAKE_SOURCE_DIR}/cores/Ravel_FM1)
fm1_add_core(ravel ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_rv} UNIT core_ravel.c DESC core_ravel_desc.c
             EXTRA_SOURCES ravel_app_unit.c
             FAMILY plat LICENSE GPL-3.0-only
             CREDITS "Ravel by De-Pattern.\nFelucca's FM-1 hardware layer by Leo Kuroshita (Hügelton Instruments), by way of X0X by Charles Vestal.\nThe CZ engine follows MAME's NEC uPD933 emulation by Devin Acker (BSD-3-Clause, its notice in THIRD_PARTY.md)."
             GEN rv_font.h PYTHON_NEEDS PIL
             GEN_PYTHON "__import__('os').makedirs('build/gen', exist_ok=True) or __import__('gen_rv_font').main('build/gen/rv_font.h')"
             VERSION 1.34.1
             FLAGS -ffp-contract=off
             INCLUDES ${_rv}/firmware/app ${_rv}/firmware)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL and THIRD_PARTY.md (the
# Felucca / X0X parts, and the MAME uPD933 BSD-3-Clause notice of the CZ engine)
fm1_add_rfm1(ravel AUTHORS "De-Pattern"
             LICENSE_FILES "GPL-3.0 ({name})=LICENSE" "THIRD_PARTY.md ({name})=THIRD_PARTY.md")
