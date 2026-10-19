// SPDX-License-Identifier: GPL-3.0-only
// How heavy each core is: core_bench [SECONDS] MODULE.fm1core[=CONFORMANCE.txt] ..
// Each module is loaded as the plugin loads it (CoreLoader: a private copy), boots from a fresh flash, runs its
// conformance file's `sound` prelude and settle time (tests/conformance_conf.h), then holds a three-note chord
// (D4 F#4 A4, velocity 100; X0X: its steps) while the plugin's path renders SECONDS (10) of audio: Device::render at
// 48 kHz in 512-frame host blocks (the device clock, the firmware, the resampler), as the Standalone's audio
// callback drives it. Prints the wall time and the real-time factor (rendered seconds per wall second: 50x = one
// core takes 2 % of one CPU core). Not a ctest: the numbers depend on the machine and its load. FM1_BENCH_INFO=1:
// each core's ui_info after its run too (tests/fm1emu_bench.sh). FM1_BENCH_BLOCK=n: host blocks of n frames (512).
// Also printed: the slowest host block's wall time against the block's own length ("worst") and how many blocks took
// longer than they last ("late": each one a dropout in a real-time host): the work must be even, never a burst (an
// emulated core running a whole DMA half inside one small block). FM1_BENCH_MHZ=n: the fm1emu core's clock (its boot
// option "mhz"; default 360).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "conformance_conf.h"
#include "cores.h"
#include "device.h"
#include "script_runner.h"

int main(int argc, char **argv)
{
    int a = 1;
    double secs = 10.0;
    if (argc > 1 && std::atof(argv[1]) > 0 && std::string(argv[1]).find(".fm1core") == std::string::npos)
        secs = std::atof(argv[a++]);
    if (a >= argc) {
        std::printf("usage: core_bench [SECONDS] MODULE.fm1core[=CONFORMANCE.txt] ..\n");
        return 2;
    }
    std::printf("%-14s %-22s %10s %10s %8s\n", "core", "version", "render s", "wall s", "x real");
    int bad = 0;
    for (; a < argc; a++) {
        std::string arg = argv[a], conf_path;
        const size_t eq = arg.find('=');
        if (eq != std::string::npos) {
            conf_path = arg.substr(eq + 1);
            arg = arg.substr(0, eq);
        }
        const Conf conf = read_conf(conf_path);
        std::string err;
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(arg, &err);
        if (!lc) {
            std::printf("%s: %s\n", arg.c_str(), err.c_str());
            bad++;
            continue;
        }
        if (const char *mhz = std::getenv("FM1_BENCH_MHZ"))
            if (auto opt = reinterpret_cast<fm1core_boot_option_fn>(lc->symbol(FM1CORE_BOOT_OPTION_SYMBOL)))
                opt("mhz", mhz);
        Device d(lc->core());
        if (std::string(lc->core()->id) == "fm1emu")   // (the plugin paces the emulated core by time: device.h PACING)
            d.set_pace_ms(std::getenv("FM1_BENCH_PACE") ? (uint32_t)std::atoi(std::getenv("FM1_BENCH_PACE")) : 4u);
        d.boot_from(nullptr, 0);
        if (!conf.sound.empty()) {
            Script s;
            if (parse_script_text(conf.sound, conf_path, s, err)) {
                ScriptRunner r(d, s);
                r.keep_pcm = false;
                r.run();
            }
        }
        while (d.ms() < conf.settle_ms)
            d.run_ms();
        int16_t drain[1024];
        while (d.take_s16(drain, 512))
            ;
        const int chord[3] = {9, 13, 16};          // D4 F#4 A4
        uint32_t mask = 0;
        for (int k : chord) {
            d.key_vel(k, 100);
            mask |= 1u << k;
        }
        d.keys(mask);
        const double rate = 48000.0;
        const uint32_t block = std::getenv("FM1_BENCH_BLOCK") ? (uint32_t)std::max(16, std::atoi(std::getenv("FM1_BENCH_BLOCK"))) : 512;
        const uint32_t blocks = (uint32_t)(secs * rate / block);
        const double blockMs = 1000.0 * block / rate;
        double worst = 0;
        uint32_t late = 0;
        std::vector<float> L(block), R(block);
        double peak = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (uint32_t b = 0; b < blocks; b++) {
            const auto b0 = std::chrono::steady_clock::now();
            d.render(L.data(), R.data(), block, rate);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count();
            worst = std::max(worst, ms);
            late += ms > blockMs;
            for (uint32_t i = 0; i < block; i++)
                peak = std::max(peak, (double)std::abs(L[i]));
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double rendered = (double)blocks * block / rate;
        d.keys(0);
        std::printf("%-14s %-22s %10.2f %10.3f %8.1f  worst %.2f of %.2f ms, late %u of %u%s\n", lc->core()->id,
                    lc->core()->version, rendered, wall, rendered / wall, worst, blockMs, late, blocks, peak > 0 ? "" : "  (silent)");
        if (std::getenv("FM1_BENCH_INFO") && lc->core()->ui_info) {   // (the core's own view: fm1emu's speed, H, L)
            char info[512];
            lc->core()->ui_info(info, sizeof info);
            std::printf("    ui_info: %s\n", info);
        }
        std::fflush(stdout);
    }
    return bad ? 1 : 0;
}
