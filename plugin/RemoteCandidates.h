/* SPDX-License-Identifier: GPL-3.0-only */
/* Remote FM-1: which CoreMIDI endpoints may be an FM-1, and so get RemoteFM1's HELLO while the plugin searches.
 * plugin/RemoteMidi.c (FM1VST's CoreMIDI back end for libremotefm1, in place of RemoteFM1's host/src/macos/coremidi.c)
 * asks this for every real device entity; nothing else ever gets a message from the plugin.
 *
 *   - the device (or entity) has kMIDIPropertyUSBVendorProduct: a candidate exactly when it is 0x1209 << 16 | 0x0001
 *     (RemoteFM1's USB IDs, shared with ChoralRoot / TransitFM1 on the same unit);
 *   - only when the property is missing: a candidate when the device's or the endpoint's name contains "FM-1" (any
 *     case; also the stale "ChoralRoot FM-1" CoreMIDI keeps);
 *   - never an updater port (an endpoint name containing "update"). */
#ifndef FM1_REMOTE_CANDIDATES_H
#define FM1_REMOTE_CANDIDATES_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM1_REMOTE_USB_VID 0x1209u
#define FM1_REMOTE_USB_PID 0x0001u
#define FM1_REMOTE_USB_ID ((int32_t)((FM1_REMOTE_USB_VID << 16) | FM1_REMOTE_USB_PID))

/* has_usb: the property exists; usb_id: its value; names may be NULL. 1: send HELLO, 0: never touch it */
int fm1_remote_midi_candidate(int has_usb, int32_t usb_id, const char *device_name, const char *endpoint_name);

#ifdef __cplusplus
}
#endif
#endif
