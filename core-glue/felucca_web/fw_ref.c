/* SPDX-License-Identifier: GPL-3.0-only */
/* The golden check's reference (fw_common.cmake fw_golden): the firmware's own browser unit, FW_REF_WEB_C
 * (web/emu/felucca_web.c), built natively as its author's web/emu/native_check.c builds it, driven through its own
 * exports (web_boot, web_buttons, web_keys, web_enc, web_master, web_screen) one device millisecond at a time
 * (web_ms, as web_render does). Two things are set as the core sets them, so the two runs see the same device:
 * usb.up / usb.config = 1 after the boot (the plugin is a USB host: the header's USB mark, MIDI out), and the audio
 * read back as the core does (abuf >> OUT_SHIFT, from the unit's own ring). Nothing else of the glue is here. */
#include FW_REF_WEB_C

void fw_ref_boot(void)
{
    web_nor_erase();
    web_boot();
    usb.up = 1;
    usb.config = 1;
}
void fw_ref_boot_from(const uint8_t *bytes, uint32_t n)   /* (fw_golden's FW_GOLDEN_SYSEX: a staged image) */
{
    web_nor_erase();
    memcpy(nor, bytes, n < sizeof nor ? n : sizeof nor);
    web_boot();
    usb.up = 1;
    usb.config = 1;
}
void fw_ref_ms(void) { web_ms(); }
void fw_ref_buttons(uint32_t labels) { web_buttons(labels); }
void fw_ref_keys(uint32_t mask) { web_keys(mask); }
void fw_ref_enc(uint32_t role, int32_t n) { web_enc(role, n); }
void fw_ref_master(uint32_t v) { web_master(v); }
const uint16_t *fw_ref_screen(void) { return web_screen(); }
uint32_t fw_ref_now(void) { return fm1_ms; }
/* the frames rendered since the last take, as the core's audio() gives them (s16 stereo, Q15 = 0 dBFS) */
uint32_t fw_ref_audio_take(int16_t *out, uint32_t max_frames)
{
    uint32_t n = 0;
    while (n < max_frames && ring_r != ring_w) {
        const uint32_t i = (ring_r++ % RING) * 2u;
        int32_t l = (int32_t)(ring[i] * 8388608.0f) >> OUT_SHIFT, r = (int32_t)(ring[i + 1] * 8388608.0f) >> OUT_SHIFT;
        out[2u * n] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2u * n + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        n++;
    }
    return n;
}
