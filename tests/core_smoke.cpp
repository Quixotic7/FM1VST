// SPDX-License-Identifier: GPL-3.0-only
// Smoke test of one core module: core_smoke MODULE.fm1core [SCREEN.ppm] [CONFORMANCE.txt]
// The module is dlopen'ed in place (its one symbol fm1core_get, no per-instance copy), boots headless (RAM-only
// flash, simulated time), must draw on its LCD, make sound when its note key is held, and not halt. When to press
// and what first is the core's conformance file (tests/conformance_conf.h): its `sound` prelude runs from power-on
// (X0X: steps entered and PLAY, its keys are steps), then settle_ms of device time (the splash: OberVave 2.4 s,
// JIANT ~2 s), then the key (D4 unless `key`) held `hold` ms; sound (a peak above 64) within 1 s of the press, as
// core_conformance's step 3 (a generative core such as AMB-1 may already be playing). The clock is the engine's
// Device (web/emu_web.c's run_ms). With SCREEN.ppm the LCD at the end is written there (binary PPM).
#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "conformance_conf.h"
#include "device.h"
#include "ppm.h"
#include "script_runner.h"

static int peak_of(Device &d)                      // the audio produced since the last take, its peak
{
    int16_t buf[FM1P_BLOCK * 2 * 4];
    int pk = 0;
    for (uint32_t n; (n = d.take_s16(buf, FM1P_BLOCK * 4)) > 0;)
        for (uint32_t i = 0; i < n * 2; i++)
            pk = std::max(pk, std::abs((int)buf[i]));
    return pk;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: core_smoke MODULE.fm1core [SCREEN.ppm] [CONFORMANCE.txt]\n");
        return 2;
    }
    const Conf conf = read_conf(argc > 3 ? argv[3] : "");
    for (const std::string &e : conf.errors) {
        std::printf("core_smoke: %s: %s: FAIL\n", conf.path.c_str(), e.c_str());
        return 1;
    }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const fm1core_get_fn get = h ? (fm1core_get_fn)dlsym(h, FM1CORE_SYMBOL) : nullptr;
    if (!get) {
        std::printf("core_smoke: %s: %s: FAIL\n", argv[1], dlerror());
        return 1;
    }
    const fm1core_t *core = get(FM1CORE_ABI);
    if (!core || get(FM1CORE_ABI + 1u)) {
        std::printf("core_smoke: fm1core_get: no descriptor, or one for the wrong ABI: FAIL\n");
        return 1;
    }
    std::printf("core_smoke: core %s \"%s\" %s (%s), flash %u bytes, %u parameters, buttons %s %s .. %s\n", core->id,
                core->name, core->version, core->source_url, (unsigned)core->flash_size, (unsigned)core->nparams,
                core->button_names[0], core->button_names[1], core->button_names[FM1P_NB - 1]);
    if (conf.found)
        std::printf("core_smoke: %s: settle %u ms, key %s held %u ms%s\n", conf.path.c_str(), (unsigned)conf.settle_ms,
                    note_name(conf.key).c_str(), (unsigned)conf.hold_ms, conf.sound.empty() ? "" : ", a sound prelude");

    Device d(core);
    d.boot(nullptr, true);                         // RAM-only flash, headless, ready = 2 before init
    uint64_t nonzero = 0;
    if (!conf.sound.empty()) {
        Script s;
        std::string err;
        if (!parse_script_text(conf.sound, conf.path + " sound", s, err)) {
            std::printf("core_smoke: %s: FAIL\n", err.c_str());
            return 1;
        }
        ScriptRunner r(d, s);
        r.keep_pcm = false;
        r.run();
        nonzero += r.nonzero();
    }
    while (d.ms() < conf.settle_ms)
        d.run_ms();
    peak_of(d);
    for (int i = 0; i < 300; i++)
        d.run_ms();
    const int before = peak_of(d);
    const uint32_t t0 = d.ms();
    d.key_vel(conf.key, 100);
    d.keys(1u << conf.key);
    int after = 0;
    uint32_t heard = 0;
    while (d.ms() - t0 < 1000) {
        if (d.ms() - t0 == conf.hold_ms)
            d.keys(0);
        d.run_ms();
        int16_t buf[FM1P_BLOCK * 2 * 4];
        for (uint32_t n; (n = d.take_s16(buf, FM1P_BLOCK * 4)) > 0;)
            for (uint32_t i = 0; i < n * 2; i++) {
                const int v = std::abs((int)buf[i]);
                nonzero += v != 0;
                if (v > 64 && !heard)
                    heard = d.ms() - t0;
                after = std::max(after, v);
            }
    }
    d.keys(0);
    if (argc > 2)
        fm1g_write_lcd_ppm(argv[2], core->panel->lcd);
    char info[512];
    info[0] = 0;
    if (core->ui_info)
        core->ui_info(info, sizeof info);
    std::printf("core_smoke: screen: %s\n", info);
    const bool ok = nonzero > 0 && after > 64 && core->panel->lcd_writes > 0 && !core->halted();
    std::printf("core_smoke: %s: %u ms, key %s at %u ms: peak %d from %u ms after the press (before it %d), "
                "lcd_writes %u%s: %s\n",
                core->id, (unsigned)d.ms(), note_name(conf.key).c_str(), (unsigned)t0, after, (unsigned)heard, before,
                (unsigned)core->panel->lcd_writes, core->halted() ? ", HALTED" : "", ok ? "PASS" : "FAIL");
    core->shutdown();
    return ok ? 0 : 1;
}
