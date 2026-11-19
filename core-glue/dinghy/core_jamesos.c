/* SPDX-License-Identifier: GPL-3.0-only */
/* The JamesOS core: JamesOS by Aaron Grando (https://github.com/aarongrando/JamesOS, the pinned, unmodified submodule
 * cores/JamesOS), a groovebox (three Plaits synth tracks, four drum lanes, the sequencer and the pattern arranger) on
 * Dinghy's platform.c over Felucca's, built as its host mode (DINGHY_HOST) with the device's SAVE screen and project
 * slots (DINGHY_DEVICE_SAVE_TEST: instrument_project_device.c / _ui.c on a host NOR, as its own device-UI test builds
 * them). platform.c then includes "../../tests/host.c" and dinghy.c "../../tests/project_device_host.c":
 * core_jamesos.cmake maps those paths to jamesos_host.c and jamesos_project_host.c (clang's -ivfsoverlay). The family
 * glue (dinghy_pre.h, dinghy_glue.h) runs the main loop:
 *
 *   the main loop  dinghy.c's own app_main (br_app_init, then platform_poll and br_app_tick for ever)
 *   the input      into the shim's fm1_in, edges (host_pressed / host_released), the note-key edges with their
 *                  24 MHz stamps, encoder steps (host_encoder); the keys are switches
 *   the flash      the session record SAVE writes: platform.c's settings sector (0xFC000, "DNGY"); the 12 named
 *                  project slots and the working copy: project_store.c's partition 0x97000 .. 0xB1000, the device's
 *                  legacy store (the 1.0b device build stores projects through its pattern streams instead: a unit's
 *                  dump's projects are not read here, its session record is)
 *   SysEx          in: every frame goes to ota.c's ota_service, as the device's platform_poll hands them: the
 *                  M-UPGRADE identity FM-1_9673083 (build 3083); out: its replies
 *   the LEDs       host_led_level_state per id (0..13 buttons, 14 + k keys): off, dim, mid, bright
 * MASTER is the glue's (the device's curve, after app_audio). Not here: USB audio, the DIN MIDI port, the second core
 * (the host build renders on one), the Editor's project transfer and the screen reader's SysEx (the device's). */
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#include "dinghy_pre.h"

#include "dinghy.c"

#include "exit_end.h"

/* the ISR: the block zeroed first (platform.c's DMA half reaches app_audio cleared), then MASTER and the clip */
static void fm1core_jo_render(int32_t *o, uint32_t frames, int32_t q12)
{
    uint32_t i;
    memset(o, 0, (size_t)frames * 2u * sizeof *o);
    (void)app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#define FM1CORE_DG_RENDER(o, frames, q12) fm1core_jo_render((o), (frames), (q12))

/* ---- the hooks (dinghy_glue.h) */
static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc)
{
    uint32_t i, k;
    host_ticks = fm1_ms * 24000u;
    for (k = 0; k < 27u; k++)
        if (((notes ^ fm1_in.notes) >> k) & 1u)
            host_note_tick[k] = host_ticks;
    host_note_pressed |= notes & ~fm1_in.notes;
    fm1_in.notes = host_note_stamped = notes;
    fm1_in.buttons = held;
    host_pressed |= down;
    host_released |= up;
    for (i = 0; i < 7u; i++)
        host_encoder[i] += enc[i];
}
static uint8_t fm1core_dg_led_level(uint32_t id)
{
    static const uint8_t L[4] = {FM1P_LED_OFF, FM1P_LED_DIM, FM1P_LED_MID, FM1P_LED_LIT};
    return id < FM1_LED_COUNT ? L[host_led_level_state[id] & 3u] : FM1P_LED_OFF;
}
static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (mq_w - mq_r >= 64u)
        return 0;
    mq[mq_w % 64u][0] = st;
    mq[mq_w % 64u][1] = d1;
    mq[mq_w % 64u][2] = d2;
    mq_time[mq_w % 64u] = host_ticks;
    mq_stamped[mq_w % 64u] = 1u;
    mq_w++;
    return 1;
}
/* the stores are the NOR image itself (jamesos_host.c, jamesos_project_host.c): nothing to copy; the packed Plaits
 * waves unpacked before the boot (tests/host.c's main does it before app_main) */
static void fm1core_dg_stores_load(void)
{
    fm1core_jo_store_seen();
    if (!host_init_resources())
        printf("fm1core %s: the Plaits waves did not unpack\n", FM1CORE_ID);
}
static void fm1core_dg_stores_poll(void) {}
static int fm1core_dg_sync(void)
{
    /* SAVE's session record waits for quiet (an erase stalls the device's sound): when it is still pending, now if
     * the app allows it; the project slots are written by the SAVE screen as it goes */
    if (br_ready && br_save_pending)
        br_save_poll();
    return 1;
}
static void fm1core_dg_info(char *buf, uint32_t n)
{
    char slot[16] = "";
    if (es.current >= 0)
        snprintf(slot, sizeof slot, ", project %d", es.current + 1);
    snprintf(buf, n, "track %u, page %u, preset %u%s%s%s%s", (unsigned)br_ui.part + 1u, (unsigned)br_ui.page,
             (unsigned)br_part[br_ui.part].sound.preset, slot, br_playing ? ", playing" : "",
             br_recording ? ", recording" : "", es.mode ? ", save screen" : "");
}

/* ---- SysEx: ota.c's handshake (its hooks here: ota_commit is int in JamesOS's ota.c, so not dinghy_glue.h's
 * FM1CORE_DG_OTA_C); the update's flash steps refused (never reached: nothing starts an update here) */
static const uint8_t *fm1core_jo_ota_p;
static uint32_t fm1core_jo_ota_n;
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    fm1core_dg_sysex_out(p, n);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!fm1core_jo_ota_p)
        return 0;
    *p = fm1core_jo_ota_p;
    *n = fm1core_jo_ota_n;
    return 1;
}
static void ota_frame_done(void) { fm1core_jo_ota_p = NULL; }
static uint32_t ota_now_ms(void) { return fm1t_ms(); }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static int ota_commit(const uint8_t *parm) { (void)parm; return -1; }
#include "ota.c"
static void fm1core_jo_ota_frame(const uint8_t *p, uint32_t n)   /* platform_poll: ota_service() on this frame */
{
    fm1core_jo_ota_p = p;
    fm1core_jo_ota_n = n;
    ota_service();
    fm1core_jo_ota_p = NULL;
}
#define FM1CORE_DG_SYSEX_IN(p, n) fm1core_jo_ota_frame((p), (n))

#include "dinghy_glue.h"
