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
 * (the update itself is never started: its flash hooks refuse). EVILGENIUS 0.1.2: every other frame in the update's
 * framing that ota_service decodes goes on to the glue's fm1core_qd_usb_cmd, as the device's main loop gives it to
 * app_usb_cmd (fm1-base main_fm1.c usb_cmd): the web manager's backup and restore (EVILGENIUS's core/glue/qd_bulk.h:
 * FZBI, FZBR, FZSR with the answers to its read requests, FZXD), the store in the Bluetooth package's layout
 * (-DQD_STORE_BLE=1, core_evilgenius.cmake). GPL-3.0-only: it compiles FM1VST's sysex_ota.h
 * (GPL-3.0-only) with EVILGENIUS's glue and felucca-bsp's ota.c (both MPL-2.0, unchanged). */
#include "sysex.h"

static void fm1core_eg_in_init(fm1sx_in_t *s, fm1sx_accept_fn accept);
static int fm1core_eg_frame(fm1sx_in_t *s, const uint8_t **p, uint32_t *n);
#define fm1sx_in_init(s, a) fm1core_eg_in_init((s), (a))
#define fm1sx_frame(s, p, n) fm1core_eg_frame((s), (p), (n))
#define fm1core_qd_frame fm1core_eg_qd_frame   /* (wrapped below: the live labels and readouts after it) */
#include "core_quadrille.c"
#undef fm1core_qd_frame
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
/* the waiting frame for EVILGENIUS's app, or 0: an M-UPGRADE frame is ota.c's (the handshake answered, the frame freed;
 * any other of its framing to the store's commands) */
static int fm1core_eg_frame(fm1sx_in_t *s, const uint8_t **p, uint32_t *n)
{
    uint32_t d;
    if (!fm1sx_frame(s, p, n))
        return 0;
    if (!fm1sx_is_mupg(*p, *n))
        return 1;
    d = ota_service();
    if (d)
        (void)fm1core_qd_usb_cmd(ota_dec, d);
    return 0;
}

/* THE LIVE LABELS AND READOUTS (docs/DYNAMIC-LABELS.md, docs/RFM1.md "Readouts (rd1)"): EVILGENIUS keeps FIZZ's own
 * models, so this is FIZZ's adapter (RemoteFM1 tools/rfm1/fizz/fizz_unit.c), the same calls into the unchanged tree.
 * The labels: qd_ctl_labels (app/view_labels.c, the 49 slots in FM1CORE_LBL_* order); the epoch moves when any changed.
 * The readouts, after each frame: a knob's takeover that moved (a new one or a new detent: tk_ms) as qd_view_turn
 * draws it (its name, its value, the bar); a knob over a track's default or the held steps (QD_TK_DEFAULT,
 * QD_TK_HELDKNOB) as the knob's live label and the hold screen's value (qd_view_edit); the browser's LOADED / BACK /
 * SAVED and an import as messages. */
static qd_labels_t fm1core_eg_lbl;
static uint32_t fm1core_eg_lbl_epoch;
FM1CORE_EXPORT uint32_t fm1core_labels_epoch(void)
{
    static qd_labels_t n;
    if (!fm1core_qd_up)
        return 0;
    qd_ctl_labels(&fm1core_qd_app, &n);
    if (!fm1core_eg_lbl_epoch || memcmp(&n, &fm1core_eg_lbl, sizeof n)) {
        fm1core_eg_lbl = n;
        if (++fm1core_eg_lbl_epoch == 0)
            fm1core_eg_lbl_epoch = 1;
    }
    return fm1core_eg_lbl_epoch;
}
FM1CORE_EXPORT const char *fm1core_label(int32_t which)
{
    return fm1core_qd_up && fm1core_eg_lbl_epoch && which >= 0 && which < QD_LBL_N ? fm1core_eg_lbl.s[which] : 0;
}
typedef char fm1core_eg_lbl_ok[QD_LBL_N == FM1CORE_LBL_N && QD_LBL_LEN == FM1CORE_LBL_LEN ? 1 : -1];

#ifdef FM1_HOST_EXT
#include "rfm1_ext.h"
static void fm1core_eg_rd_note(const ui_held_t *h, int knob, char *b, size_t n)   /* a NOTE slot's note, else the chord */
{
    int i;
    if (knob >= QD_K_SELECT && knob <= QD_K_4 && h->slot_note[knob == QD_K_SELECT ? 0 : knob - QD_K_1 + 1])
        for (i = 0; i < h->nnote && i < 6; i++)
            if (h->note[i] == h->slot_note[knob == QD_K_SELECT ? 0 : knob - QD_K_1 + 1]) {
                snprintf(b, n, "%s", h->note_name[i]);
                return;
            }
    if (h->root[0])
        snprintf(b, n, "%s%s", h->root, h->qual);
    else if (h->names[0])
        snprintf(b, n, "%s", h->names);
    else
        snprintf(b, n, "%s", h->nnote ? h->note_name[0] : "-");
}
static void fm1core_eg_rd_held(int knob)
{
    static ui_held_t h;
    static ui_lane_t l;
    char v[24];
    const char *t;
    v[0] = 0;
    memset(&h, 0, sizeof h);
    qd_view_edit(&fm1core_qd_app, &h, &l);
    switch (fm1core_qd_app.ui.tk_job) {                 /* the band's job first: in the CHNC mode KNOB 2 sets the
                                                         * condition while the screen keeps the chance big */
    case QD_KJ_COND: snprintf(v, sizeof v, "%s", h.cond_name); goto said;
    case QD_KJ_CHANCE: snprintf(v, sizeof v, "%d%%", h.chance); goto said;
    case QD_KJ_PTIME: snprintf(v, sizeof v, "%d", h.ptime); goto said;
    default: break;
    }
    switch (h.field) {
    case UI_F_NOTE: fm1core_eg_rd_note(&h, knob, v, sizeof v); break;
    case UI_F_VEL: snprintf(v, sizeof v, "%d", h.vel); break;
    case UI_F_LEN: snprintf(v, sizeof v, "%s", h.len_name ? h.len_name : ""); break;
    case UI_F_MICRO: snprintf(v, sizeof v, "%d", h.micro); break;
    case UI_F_RATCH: snprintf(v, sizeof v, "%d", h.count); break;
    case UI_F_CHNC: snprintf(v, sizeof v, "%d%%", h.chance); break;
    case UI_F_COND: snprintf(v, sizeof v, "%s", h.cond_name); break;
    case UI_F_FUNC: snprintf(v, sizeof v, "%s", h.func_name); break;
    case UI_F_SLIDE: snprintf(v, sizeof v, "%d", h.ptime); break;
    case UI_F_SOUND: snprintf(v, sizeof v, "%s", h.sound_cur ? h.sound_cur : ""); break;
    default: break;
    }
said:
    (void)fm1core_labels_epoch();                       /* (the labels as of now: the knob's name) */
    t = knob >= 0 && knob < QD_NKNOB ? fm1core_eg_lbl.s[QD_LBL_KNOB0 + knob] : "";
    if (t[0] || v[0])
        RFM1_READOUT(-1, t, v);
}
static void fm1core_eg_rd_poll(void)
{
    static int last_tk = QD_TK_NONE;
    static uint32_t last_ms;
    const qd_ui_t *u = &fm1core_qd_app.ui;
    if (!fm1core_qd_up)
        return;
    if (u->tk == last_tk && (u->tk == QD_TK_NONE || u->tk_ms == last_ms))
        return;
    last_tk = u->tk, last_ms = u->tk_ms;
    if (u->tk >= 0 && u->tk < UI_NTK) {
        ui_turn_t v;
        memset(&v, 0, sizeof v);
        qd_view_turn(&fm1core_qd_app, &v);
        if (v.name && v.name[0])
            RFM1_READOUT_BAR(-1, v.name, v.val, (int32_t)(v.v * 1000.0f), 0, 1000);
    } else if (u->tk == QD_TK_DEFAULT || u->tk == QD_TK_HELDKNOB)
        fm1core_eg_rd_held(u->tk_src);
    else if (u->tk == QD_TK_LOADED)
        RFM1_MESSAGE("LOADED");
    else if (u->tk == QD_TK_BACK)
        RFM1_MESSAGE("BACK");
    else if (u->tk == QD_TK_SAVED)
        RFM1_MESSAGE("SAVED");
    else if (u->tk == QD_TK_IMPORT)
        RFM1_MESSAGE("IMPORTED");
}
#endif
void fm1core_qd_frame(void)
{
    fm1core_eg_qd_frame();
#ifdef FM1_HOST_EXT
    fm1core_eg_rd_poll();
#endif
}
