/* SPDX-License-Identifier: GPL-3.0-only */
/* Phase 0 smoke test: the ChoralRoot core boots headless (RAM-only flash, simulated time), draws on its
 * LCD and makes sound when note key 9 (D4) is held from 100 ms to 400 ms. The clock is web/emu_web.c's
 * run_ms(): the whole device advanced one millisecond at a time on one thread. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fm1core.h"

static uint32_t dev_ms, last_frame;
static uint64_t frames_done;
static uint64_t nonzero, samples;
static int peak;

static void run_ms(void)
{
    uint32_t ms = dev_ms++;
    emu_fw_tick(ms);
    if (ms == 0 || ms - last_frame >= 15u) {
        last_frame = ms;
        emu_fw_frame();
    } else {
        emu_fw_idle();
    }
    while (frames_done + EMU_BLOCK <= (uint64_t)ms * EMU_FS / 1000u) {
        int16_t blk[EMU_BLOCK * 2];
        uint32_t k;
        emu_fw_audio(blk, EMU_BLOCK);
        for (k = 0; k < EMU_BLOCK * 2; k++) {
            int v = abs((int)blk[k]);
            samples++;
            if (v) nonzero++;
            if (v > peak) peak = v;
        }
        frames_done += EMU_BLOCK;
    }
}

int main(void)
{
    const uint32_t key = 9;                       /* D4 */
    emu_fw_options(NULL, 1, 0, 1);                /* RAM-only flash, headless */
    emu_hal.ready = 2;                            /* simulated time, before the power-on (as web_boot) */
    emu_fw_init(0);
    while (dev_ms < 1000u) {
        if (dev_ms == 100u) {
            emu_hal.keys_tap |= 1u << key;
            emu_hal.keys |= 1u << key;
        } else if (dev_ms == 400u) {
            emu_hal.keys = 0;
        }
        run_ms();
    }
    int ok = nonzero > 0 && emu_hal.lcd_writes > 0;
    printf("core_smoke: %u ms, %llu frames, %llu/%llu non-zero samples, peak %d, lcd_writes %u: %s\n",
           (unsigned)dev_ms, (unsigned long long)frames_done, (unsigned long long)nonzero,
           (unsigned long long)samples, peak, (unsigned)emu_hal.lcd_writes, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
