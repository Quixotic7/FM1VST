/* SPDX-License-Identifier: GPL-3.0-only
 * Based on RemoteFM1's host/src/macos/coremidi.c (Copyright (C) 2026 RemoteFM1 contributors). */
/* FM1VST's CoreMIDI back end for libremotefm1, built in place of RemoteFM1's host/src/macos/coremidi.c (same
 * interface: mac_priv.h's cm_*). Identical except for discovery: cm_scan offers a real device entity as a candidate only
 * when fm1_remote_midi_candidate() says it can be an FM-1 (its USB IDs 1209:0001, or, without the USB property, an
 * "FM-1" name; RemoteCandidates.h). The session sends HELLO to candidates only, so the owner's other MIDI hardware
 * never gets a message; with no candidate nothing is sent at all.
 *
 * As there: SysEx out with MIDISend (one MIDIPacket per message), everything in through one input port, the bytes
 * queued as they come (the session's stream parser reassembles them). */
#include <CoreMIDI/CoreMIDI.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "RemoteCandidates.h"
#include "mac_priv.h"

struct cm {
    MIDIClientRef client;
    MIDIPortRef in, out;
    rxq_t *q;
    uint8_t *pl;
    size_t plcap;
};

static int contains_ci(const char *hay, const char *needle)
{
    size_t i, j, nh, nn;
    if (!hay || !needle)
        return 0;
    nh = strlen(hay);
    nn = strlen(needle);
    for (i = 0; i + nn <= nh; i++) {
        for (j = 0; j < nn && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j]); j++)
            ;
        if (j == nn)
            return 1;
    }
    return 0;
}

int fm1_remote_midi_candidate(int has_usb, int32_t usb_id, const char *device_name, const char *endpoint_name)
{
    if (contains_ci(endpoint_name, "update"))       /* an updater port (tools/m0_bench.py skips it too) */
        return 0;
    if (has_usb)
        return usb_id == FM1_REMOTE_USB_ID;
    return contains_ci(device_name, "FM-1") || contains_ci(endpoint_name, "FM-1");
}

static void cfstr(CFStringRef s, char *buf, size_t n)
{
    buf[0] = 0;
    if (s) {
        CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
        CFRelease(s);
    }
}

static void obj_name(MIDIObjectRef o, char *buf, size_t n)
{
    CFStringRef s = NULL;
    if (MIDIObjectGetStringProperty(o, kMIDIPropertyDisplayName, &s) != noErr || !s)
        MIDIObjectGetStringProperty(o, kMIDIPropertyName, &s);
    cfstr(s, buf, n);
}

static int offline(MIDIObjectRef o)
{
    SInt32 v = 0;
    return MIDIObjectGetIntegerProperty(o, kMIDIPropertyOffline, &v) == noErr && v;
}

/* kMIDIPropertyUSBVendorProduct: the key Apple's USB MIDI driver (com.apple.AppleMIDIUSBDriver) sets on its devices,
 * "USBVendorProduct" (no longer declared in the SDK's CoreMIDI headers, so named here) */
#define FM1_PROP_USB_VENDOR_PRODUCT CFSTR("USBVendorProduct")

/* kMIDIPropertyUSBVendorProduct on the entity (CoreMIDI looks it up on the device when the entity has none), else the
 * device; 1 if found */
static int usb_id_of(MIDIDeviceRef dev, MIDIEntityRef ent, int32_t *id)
{
    SInt32 v = 0;
    if (MIDIObjectGetIntegerProperty(ent, FM1_PROP_USB_VENDOR_PRODUCT, &v) == noErr ||
        MIDIObjectGetIntegerProperty(dev, FM1_PROP_USB_VENDOR_PRODUCT, &v) == noErr) {
        *id = (int32_t)v;
        return 1;
    }
    return 0;
}

cm_t *cm_open(rxq_t *q, int midi_restart, char *err, size_t errn)
{
    cm_t *c = (cm_t *)calloc(1, sizeof *c);
    OSStatus st;
    if (!c)
        return NULL;
    c->q = q;
    if (midi_restart)
        MIDIRestart();
    st = MIDIClientCreate(CFSTR("FM1VST Remote"), NULL, NULL, &c->client);
    if (st == noErr)
        st = MIDIInputPortCreateWithBlock(c->client, CFSTR("FM1VST Remote in"), &c->in,
                                          ^(const MIDIPacketList *pl, void *refcon) {
                                              const MIDIPacket *p = &pl->packet[0];
                                              UInt32 i;
                                              for (i = 0; i < pl->numPackets; i++) {
                                                  rxq_push(q, (uint16_t)(uintptr_t)refcon, p->data, p->length);
                                                  p = MIDIPacketNext(p);
                                              }
                                          });
    if (st == noErr)
        st = MIDIOutputPortCreate(c->client, CFSTR("FM1VST Remote out"), &c->out);
    if (st != noErr) {
        snprintf(err, errn, "CoreMIDI: cannot create the client or its ports (OSStatus %d)", (int)st);
        cm_close(c);
        return NULL;
    }
    c->plcap = 70000;
    c->pl = (uint8_t *)malloc(c->plcap);
    return c;
}

void cm_close(cm_t *c)
{
    if (!c)
        return;
    if (c->in)
        MIDIPortDispose(c->in);
    if (c->out)
        MIDIPortDispose(c->out);
    if (c->client)
        MIDIClientDispose(c->client);
    free(c->pl);
    free(c);
}

static int skip_driver(MIDIDeviceRef dev)
{
    CFStringRef s = NULL;
    char drv[128];
    MIDIObjectGetStringProperty(dev, kMIDIPropertyDriverOwner, &s);
    cfstr(s, drv, sizeof drv);
    return contains_ci(drv, "IAC") || contains_ci(drv, "RTP") || contains_ci(drv, "Bluetooth");
}

static int is_virtual(MIDIEndpointRef e)
{
    MIDIEntityRef ent = 0;
    return MIDIEndpointGetEntity(e, &ent) != noErr || !ent;
}

int cm_scan(cm_t *c, cm_cand_t *out, int max, int allow_devices, const char *virtual_name, const char *filter)
{
    int n = 0;
    ItemCount i, k;
    (void)c;
    if (allow_devices) {
        ItemCount nd = MIDIGetNumberOfDevices();
        for (i = 0; i < nd && n < max; i++) {
            MIDIDeviceRef dev = MIDIGetDevice(i);
            char devname[64];
            if (offline(dev) || skip_driver(dev))
                continue;
            obj_name(dev, devname, sizeof devname);
            for (k = 0; k < MIDIDeviceGetNumberOfEntities(dev) && n < max; k++) {
                MIDIEntityRef ent = MIDIDeviceGetEntity(dev, k);
                MIDIEndpointRef src, dst;
                SInt32 uid = 0;
                int32_t usb = 0;
                const int has_usb = usb_id_of(dev, ent, &usb);
                if (MIDIEntityGetNumberOfSources(ent) < 1 || MIDIEntityGetNumberOfDestinations(ent) < 1)
                    continue;
                src = MIDIEntityGetSource(ent, 0);
                dst = MIDIEntityGetDestination(ent, 0);
                if (offline(ent) || offline(src) || offline(dst))
                    continue;
                obj_name(dst, out[n].name, sizeof out[n].name);
                if (!fm1_remote_midi_candidate(has_usb, usb, devname, out[n].name))
                    continue;                           /* not an FM-1: never touched */
                if (filter && *filter && !contains_ci(out[n].name, filter))
                    continue;
                MIDIObjectGetIntegerProperty(dst, kMIDIPropertyUniqueID, &uid);
                out[n].src = (uint32_t)src;
                out[n].dst = (uint32_t)dst;
                out[n].uid = uid;
                n++;
            }
        }
    }
    if (virtual_name && *virtual_name) {                /* (the tests' loopback: an exact name only) */
        ItemCount ns = MIDIGetNumberOfSources(), ndst = MIDIGetNumberOfDestinations();
        for (i = 0; i < ns && n < max; i++) {
            MIDIEndpointRef src = MIDIGetSource(i);
            char name[64];
            if (!is_virtual(src))
                continue;
            obj_name(src, name, sizeof name);
            if (strcmp(name, virtual_name))
                continue;
            for (k = 0; k < ndst; k++) {
                MIDIEndpointRef dst = MIDIGetDestination(k);
                char dn[64];
                SInt32 uid = 0;
                if (!is_virtual(dst))
                    continue;
                obj_name(dst, dn, sizeof dn);
                if (strcmp(dn, virtual_name))
                    continue;
                MIDIObjectGetIntegerProperty(dst, kMIDIPropertyUniqueID, &uid);
                out[n].src = (uint32_t)src;
                out[n].dst = (uint32_t)dst;
                out[n].uid = uid;
                snprintf(out[n].name, sizeof out[n].name, "%s", name);
                n++;
                break;
            }
        }
    }
    return n;
}

int cm_connect(cm_t *c, uint32_t src, uint16_t tag)
{
    return MIDIPortConnectSource(c->in, (MIDIEndpointRef)src, (void *)(uintptr_t)tag) == noErr ? 0 : -1;
}

void cm_disconnect(cm_t *c, uint32_t src) { MIDIPortDisconnectSource(c->in, (MIDIEndpointRef)src); }

int cm_send(cm_t *c, uint32_t dst, const uint8_t *p, uint32_t n)
{
    MIDIPacketList *pl;
    MIDIPacket *cur;
    if (n > 65535u || n + 256u > c->plcap)
        return -1;
    pl = (MIDIPacketList *)c->pl;
    cur = MIDIPacketListInit(pl);
    cur = MIDIPacketListAdd(pl, c->plcap, cur, 0, n, p);
    if (!cur)
        return -1;
    return MIDISend(c->out, (MIDIEndpointRef)dst, pl) == noErr ? 0 : -1;
}
