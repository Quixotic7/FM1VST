/* SPDX-License-Identifier: GPL-3.0-only */
/* The AURORA core: AURORA (https://github.com/twochannels/AURORA, the pinned, unmodified submodule cores/AURORA), a
 * Dinghy app (Dinghy's platform.c over Felucca's, with FM-106's second-core audio worker), on its own fake FM-1,
 * tests/fake_fm1.h, built as its host mode (DINGHY_HOST). platform.c then includes "../../tests/host.c", a program
 * (script, WAV, main()): core_aurora.cmake maps that path to aurora_host.c (clang's -ivfsoverlay), whose
 * platform_poll is the coroutine's yield. The family glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the main loop  AURORA's app has no app_main: platform.c's contract is app_init once, then platform_poll and
 *                  app_loop for ever (host.c's main, the device's); that is app_main below
 *   the second core  fake_fm1.h's audio_worker_submit runs the job at once on the caller (the device's submit /
 *                  join path, serial), so the CPU1 half of the voices renders inside the ISR's block
 *   the input      into fake_fm1.h's fm1_in, edges (fake_pressed / fake_released / fake_notes_pressed) and encoder
 *                  steps; the keys are switches (no analog rows on the fake: velocity 100, no pressure)
 *   the flash      the fake's RAM stores, copied into the NOR image at the device's offsets in the device's formats
 *                  (platform.c): the settings (a DNGY record at 0xFC000), the user bank (4 KiB raw at 0x98000);
 *                  read back before the boot
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them (the
 *                  M-UPGRADE identity FM-1_4747, tools/build.py PRODUCT); out: the sound dump (SAVE then GLO,
 *                  midi_sysex) as USB-MIDI SysEx packets
 *   the LEDs       fake_leds per id (0..13 buttons, 14 + k keys), lit or off
 * MASTER is the glue's (the device's curve, after app_audio); the fake leaves it at unity. Not here: USB audio, the
 * rescue, the boot guard (the device branch's). */
#define DINGHY_HOST 1
#include "dinghy_pre.h"

#include "aurora.c"

#include "exit_end.h"

/* platform.c's device layout (its device branch: not compiled here) */
#define FM1CORE_AUR_SETTINGS_AT 0x000FC000u
#define FM1CORE_AUR_BANK_AT 0x00098000u
_Static_assert(BANK_LEN == 0x1000u, "AURORA's user bank: platform.c's one 4 KiB sector");

/* platform.c's contract (its header): app_init once, then a poll and a pass of app_loop, for ever */
static void app_main(void)
{
    app_init();
    for (;;) {
        platform_poll();
        app_loop();
    }
}

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i;
    fake_notes_pressed |= notes & ~fm1_in.notes;
    fm1_in.notes = notes;
    fm1_in.buttons = held;
    fake_pressed |= down;
    fake_released |= up;
    for (i = 0; i < 7u; i++)
        fake_enc[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    return id <= 40u && (fake_leds >> id & 1u) ? FM1P_LED_LIT : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2) { return fake_midi_put(st, d1, d2); }
static uint32_t fm1core_aur_saves, fm1core_aur_bank_saves;
static void fm1core_dg_stores_load(void)
{
    uint32_t n = 0;
    memset(fake_store, 0, sizeof fake_store);
    if (!fm1core_dg_sector_read(FM1CORE_AUR_SETTINGS_AT, fake_store + 4, sizeof fake_store - 4u, &n)) {
        fake_store[0] = 1;                /* (fake_fm1.h settings_load: the length must match the app's) */
        fake_store[1] = (uint8_t)n;
        fake_store[2] = (uint8_t)(n >> 8);
    }
    memcpy(fake_bank, fm1nor_image + FM1CORE_AUR_BANK_AT, BANK_LEN);   /* (platform.c bank_load: the raw sector) */
    fm1core_aur_saves = fake_saves;
    fm1core_aur_bank_saves = fake_bank_saves;
}
static void fm1core_dg_stores_poll(void)
{
    if (fake_saves != fm1core_aur_saves) {
        fm1core_aur_saves = fake_saves;
        fm1core_dg_sector_save(FM1CORE_AUR_SETTINGS_AT, fake_store + 4,
                               (uint32_t)fake_store[1] | (uint32_t)fake_store[2] << 8);
    }
    if (fake_bank_saves != fm1core_aur_bank_saves) {
        fm1core_aur_bank_saves = fake_bank_saves;
        fm1core_dg_raw_sync(FM1CORE_AUR_BANK_AT, fake_bank);
    }
}
static int fm1core_dg_sync(void)
{
    /* the main loop's deferred save, now: the settings (2 s after a change, when quiet; app_loop); the bank is written
     * when SAVE is pressed the second time (stores_poll copies it) */
    if (dirty) {
        settings_save(&set, sizeof set);
        dirty = 0;
    }
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u, preset %u, scene %s%s%s%s", (unsigned)page, (unsigned)set.preset,
             morph >= 16384 ? "B" : "A", set.latch ? ", latch" : "", autopilot ? ", auto" : "",
             voices_sounding() ? ", voices" : "");
}

/* the M-UPGRADE identity: platform.c's platform_poll gives every SysEx frame to ota_service */
#define FELUCCA_ID "FM-1_4747"            /* tools/build.py PRODUCT (fixed across versions) */
#define FM1CORE_DG_OTA_C "../../cores/AURORA/firmware/src/ota.c"
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_dg_ota_frame((p), (n))

#include "dinghy_glue.h"
