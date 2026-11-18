/* SPDX-License-Identifier: GPL-3.0-only */
/* The OrGigs core: OrGigs (https://github.com/orgelbobo/orgigs, the pinned, unmodified submodule cores/orgigs), a
 * three-manual organ (84 free-running generators, 8 models, rotary, delay, reverb, 3 MIDI zones) on Dinghy's
 * platform.c over Felucca's, built as its host mode (DINGHY_HOST, the FM-1 board). platform.c then includes
 * "../../tests/host.c", a program (script, WAV, main()): core_orgigs.cmake maps that path to orgigs_host.c (clang's
 * -ivfsoverlay), which takes the firmware's own fake FM-1 (sim/sim_platform.h) and makes platform_poll the
 * coroutine's yield. The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the input      into sim_platform.h's fm1_in, host_edges / host_released (presses, releases), host_note_edges
 *                  (keys gone down) and host_enc[]
 *   the LEDs       host_led[] per id (0 off, 1 dim, 2 lit) and the backlight layer host_led_bg[], as the browser
 *                  simulator shows them (sim_leds)
 *   the flash      the two settings sectors (store.h: SETTINGS_AT 0xFC000 and STORE1_AT 0xFD000) in platform.c's
 *                  format ({"DNGY", length, crc32} then the bytes) at the device's offsets, read into host_store
 *                  before the boot and written back when the app saves (2 s after the last change and silence,
 *                  ctl_save_tick); flash_sync saves what is pending at once. The blobs are the SMK-37's only
 *   SysEx          in: OrGigs' own (F0 7D 4F 47 .., the settings backup) and Yamaha's (F0 43 .., a SEQTRAK) go to
 *                  app_sysex, the rest to ota.c's ota_service, as the device's sysex_service does (the M-UPGRADE
 *                  identity FM-1_674102: tools/build.py --release 1.0.2); out through the MIDI out queue
 * MASTER is the glue's (the device's curve after app_audio, the block zeroed first as fm1_alnk0_irq does). Not here:
 * USB audio, the TRS MIDI in, the rescue, the boot guard (the device branch's). */
#include "dinghy_pre.h"

#include "orgigs.c"

#include "exit_end.h"

/* host_store: STORE_NSECT sectors of [1][length lo][length hi] .. the bytes from 12 (sim_platform.h store_save) */
/* (store.h's FM-1 SETTINGS_AT and STORE1_AT: store.h is the device branch's, not compiled here) */
#define FM1CORE_ORG_AT(s) ((s) ? 0x000FD000u : 0x000FC000u)
_Static_assert(STORE_NSECT == 2u && STORE_SECT == 4096u, "OrGigs' host store: two sectors");

/* the ISR: the block zeroed first (platform.c's fm1_alnk0_irq zeroes the half), then MASTER and the clip */
static void fm1core_org_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_org_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    host_note_edges |= notes & ~fm1_in.notes;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    host_edges |= down;
    host_released |= up;
    for (i = 0; i < FM1_NENC; i++)
        host_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    if (id >= 64u)
        return FM1P_LED_OFF;
    if (host_led[id])
        return host_led[id] >= 2u ? FM1P_LED_LIT : FM1P_LED_DIM;
    return host_led_bg[id] ? (host_bg_full ? FM1P_LED_LIT : FM1P_LED_DIM) : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (mq_w - mq_r >= 256u)
        return 0;
    host_midi_in(st, d1, d2);
    return 1;
}
static void fm1core_dg_stores_load(void)
{
    uint32_t s, n = 0;
    memset(host_store, 0, sizeof host_store);
    for (s = 0; s < STORE_NSECT; s++) {
        uint8_t *h = host_store + s * STORE_SECT;
        if (!fm1core_dg_sector_read(FM1CORE_ORG_AT(s), h + 12, STORE_SECT - 12u, &n)) {
            h[0] = 1;
            h[1] = (uint8_t)n;
            h[2] = (uint8_t)(n >> 8);
        }
    }
}
static void fm1core_dg_stores_poll(void)
{
    static uint8_t seen[STORE_NSECT * STORE_SECT];
    static uint8_t primed;
    uint32_t s;
    if (!primed) {                        /* (what the boot read: not written again) */
        memcpy(seen, host_store, sizeof seen);
        primed = 1;
    }
    if (!fm1core_org_saved)
        return;
    fm1core_org_saved = 0;
    for (s = 0; s < STORE_NSECT; s++) {
        uint8_t *h = host_store + s * STORE_SECT;
        if (h[0] != 1u || !memcmp(seen + s * STORE_SECT, h, STORE_SECT))
            continue;
        memcpy(seen + s * STORE_SECT, h, STORE_SECT);
        fm1core_dg_sector_save(FM1CORE_ORG_AT(s), h + 12, (uint32_t)(h[1] | h[2] << 8));
    }
}
static int fm1core_dg_sync(void)
{
    if (st_dirty)
        store_save_due();                 /* (ctl_save_tick's save, now) */
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "mode %u, model %s", (unsigned)ui.mode, ORG_MODELS[org_par.model].name);
}

/* SysEx in: platform.c's sysex_service (OrGigs): its own and Yamaha's to app_sysex, the rest to ota_service */
static void fm1core_org_sysex(const uint8_t *p, uint32_t n);
#define FELUCCA_ID "FM-1_674102"          /* tools/build.py --release 1.0.2: FM-1_674 + X Y Z */
#define FM1CORE_DG_OTA_C "../../cores/orgigs/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_org_sysex((p), (n))

#include "dinghy_glue.h"

static void fm1core_org_sysex(const uint8_t *p, uint32_t n)
{
    if ((n >= 4u && p[0] == 0x7Du && p[1] == 0x4Fu && p[2] == 0x47u) || (n >= 2u && p[0] == 0x43u))
        app_sysex(p, n);
    else
        fm1core_dg_ota_frame(p, n);
}
