/* SPDX-License-Identifier: GPL-3.0-only */
/* EVILGENIUS's firmware unit (fm1_add_core UNIT; core_evilgenius.cmake). EVILGENIUS is a derivative of FIZZ and keeps
 * FIZZ's FM1VST glue in its own tree: core/glue/core_quadrille.c, compiled here unchanged. This file adds the one thing
 * a unit answers that the glue leaves to the device, as RemoteFM1's tools/rfm1/fizz/fizz_unit.c does for FIZZ: the
 * M-UPGRADE identity. EVILGENIUS's web manager finds the unit by it first (site/manager/egwire.js: the handshake
 * F0 00 32 45 00 00 00 40 7F F7, an answer that starts FM-1_45) and only then talks EVILGENIUS's own SysEx (7D 45 47).
 * On the device fm1-base's main loop runs felucca-bsp's chip/ota.c ota_service(), which answers with the package
 * identity (FELUCCA_ID: fm1.toml's id_prefix FM-1_45 + XXYYZZ, FM-1_45000100 for 0.1.0; the glue's DEFINES, checked
 * against the repository's site/firmware/releases.json at configure time). Here the same ota.c, at the commit
 * EVILGENIUS's fm1-base pins (firmware/fm1-base/bsp), through FM1VST's common/sysex_ota.h on the glue's own SysEx
 * transport: the handshake frame is accepted beside EVILGENIUS's 7D 45 47 ones and answered; nothing else of ota.c runs
 * (the update itself is never started: its flash hooks refuse). GPL-3.0-only: it compiles FM1VST's sysex_ota.h
 * (GPL-3.0-only) with EVILGENIUS's glue and felucca-bsp's ota.c (both MPL-2.0, unchanged). */
#include "sysex.h"

static void fm1core_eg_in_init(fm1sx_in_t *s, fm1sx_accept_fn accept);
static int fm1core_eg_frame(fm1sx_in_t *s, const uint8_t **p, uint32_t *n);
#define fm1sx_in_init(s, a) fm1core_eg_in_init((s), (a))
#define fm1sx_frame(s, p, n) fm1core_eg_frame((s), (p), (n))
#include "core_quadrille.c"
#undef fm1sx_in_init
#undef fm1sx_frame

#define FM1SX_OTA_C "ota.c"                       /* felucca-bsp chip/ota.c */
#define FM1SX_OTA_IN fm1core_qd_sx_in
#define FM1SX_OTA_OUT fm1core_qd_sx_out
#define FM1SX_OTA_MS() (fm1core_qd_app.ms)
#include "sysex_ota.h"

static fm1sx_accept_fn fm1core_eg_accept_eg;
static int fm1core_eg_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n) || fm1core_eg_accept_eg(f, n); }
static void fm1core_eg_in_init(fm1sx_in_t *s, fm1sx_accept_fn accept)
{
    fm1core_eg_accept_eg = accept;
    fm1sx_in_init(s, fm1core_eg_accept);
}
/* the waiting frame for EVILGENIUS's app, or 0: an M-UPGRADE frame is ota.c's (the handshake answered, the frame freed) */
static int fm1core_eg_frame(fm1sx_in_t *s, const uint8_t **p, uint32_t *n)
{
    if (!fm1sx_frame(s, p, n))
        return 0;
    if (!fm1sx_is_mupg(*p, *n))
        return 1;
    (void)ota_service();
    return 0;
}
