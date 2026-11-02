# SPDX-License-Identifier: GPL-3.0-only
# The SikkenWorks family's shared cmake (included by core_etch.cmake and core_rupture.cmake; not a core of its own,
# so the top-level glob, core.cmake / core_*.cmake, leaves it alone).
#
# sikken_vfs(<out var> <id> <fm1-native dir> [BRINGUP <patched bringup.c>]): the core's clang VFS overlay (as
# RemoteFM1's and LOOM-8's glues), written to ${CMAKE_BINARY_DIR}/core_vfs/<id>/<id>_vfs.yaml and returned in <out
# var>. The device's HAL headers read as sikken_hal.h (a one-line stub each, so its #pragma once holds) and
# hal/fm1_flash.h (the glue's); ota_trusted_loader.h (the JieLi update loader's bytes, Apache-2.0) as an all-zero
# array of its size (no update runs in a core: sikken_glue.h); bringup.c as BRINGUP when the core patches it.
include_guard(GLOBAL)
set(SIKKEN_GLUE ${CMAKE_CURRENT_LIST_DIR})

function(sikken_vfs out id fw)
    cmake_parse_arguments(A "" "BRINGUP" "" ${ARGN})
    set(bin ${CMAKE_BINARY_DIR}/core_vfs/${id})
    set(vfs ${bin}/${id}_vfs.yaml)
    set(v "{ \"version\": 0, \"case-sensitive\": \"false\", \"use-external-names\": false, \"roots\": [\n")
    if(A_BRINGUP)
        string(APPEND v "  { \"type\": \"file\", \"name\": \"${fw}/bringup.c\", \"external-contents\": \"${A_BRINGUP}\" },\n")
    endif()
    string(APPEND v "  { \"type\": \"file\", \"name\": \"${fw}/ota_trusted_loader.h\", \"external-contents\": \"${SIKKEN_GLUE}/ota_trusted_loader.h\" },\n")
    string(APPEND v "  { \"type\": \"file\", \"name\": \"${fw}/hal/fm1_flash.h\", \"external-contents\": \"${SIKKEN_GLUE}/hal/fm1_flash.h\" }")
    foreach(h time sys irq guard input uart timer audio adc lcd_hw usb xip)
        set(s "/* (core-glue/sikken/sikken.cmake: the device's hal/fm1_${h}.h, read as the host HAL) */\n#include \"sikken_hal.h\"\n")
        set(old "")
        if(EXISTS ${bin}/hal/fm1_${h}.h)
            file(READ ${bin}/hal/fm1_${h}.h old)
        endif()
        if(NOT old STREQUAL s)
            file(WRITE ${bin}/hal/fm1_${h}.h "${s}")
        endif()
        string(APPEND v ",\n  { \"type\": \"file\", \"name\": \"${fw}/hal/fm1_${h}.h\", \"external-contents\": \"${bin}/hal/fm1_${h}.h\" }")
    endforeach()
    string(APPEND v " ] }\n")
    file(WRITE ${vfs}.in "${v}")
    configure_file(${vfs}.in ${vfs} COPYONLY)
    set(${out} ${vfs} PARENT_SCOPE)
endfunction()

# sikken_units(<id> <fm1-native dir> <vfs> <units...>): build.py's other units, the firmware's own code (warnings off),
# with the section attributes as the unit's; and the unit's dependencies on the overlay and the shared headers
function(sikken_units id fw vfs)
    if(NOT TARGET core_${id}_obj)
        return()
    endif()
    set_property(SOURCE ${SIKKEN_GLUE}/core_${id}.c APPEND PROPERTY OBJECT_DEPENDS ${vfs}
                 ${SIKKEN_GLUE}/sikken_hal.h ${SIKKEN_GLUE}/sikken_glue.h ${SIKKEN_GLUE}/hal/fm1_flash.h
                 ${SIKKEN_GLUE}/ota_trusted_loader.h)
    file(RELATIVE_PATH rel ${SIKKEN_GLUE} ${fw})
    foreach(u IN LISTS ARGN)
        set_source_files_properties(${SIKKEN_GLUE}/${rel}/${u}.c PROPERTIES
            COMPILE_OPTIONS "-w;-ffp-contract=off;-Dsection(x)=aligned(4)")
    endforeach()
endfunction()
