/* SPDX-License-Identifier: GPL-3.0-only */
/* Read by the SikkenWorks cores (ETCH, RUPTURE) in the place of fm1-native/ota_trusted_loader.h (the core's VFS
 * overlay, sikken.cmake). The device's file holds the 6898-byte JieLi update loader (Apache-2.0, from the JieLi SDK via Felucca's OTA) that
 * ota_session stages into flash for an update. A core is never updated that way: sikken_glue.h drops the update
 * request (F0 22 24 35 7F F7) as it arrives, so ota_session never runs, and the loader's bytes stay out of the core
 * (the device packages carry them). The same name and size, all zero. */
#pragma once
static const uint8_t etch_trusted_app_loader[6898u];
