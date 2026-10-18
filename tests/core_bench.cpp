// SPDX-License-Identifier: GPL-3.0-only
// How heavy each core is: core_bench [SECONDS] MODULE.fm1core[=CONFORMANCE.txt] ..
// Each module is loaded as the plugin loads it (CoreLoader: a private copy), boots from a fresh flash, runs its
// conformance file's `sound` prelude and settle time (tests/conformance_conf.h), then holds a three-note chord
// (D4 F#4 A4, velocity 100; X0X: its steps) while the plugin's path renders SECONDS (10) of audio: Device::render at
// 48 kHz in 512-frame host blocks (the device clock, the firmware, the resampler), as the Standalone's audio
// callback drives it. Prints the wall time and the real-time factor (rendered seconds per wall second: 50x = one
// core takes 2 % of one CPU core). Not a ctest: the numbers depend on the machine and its load.
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
        Device d(lc->core());
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
        const uint32_t block = 512, blocks = (uint32_t)(secs * rate / block);
        std::vector<float> L(block), R(block);
        double peak = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (uint32_t b = 0; b < blocks; b++) {
            d.render(L.data(), R.data(), block, rate);
            for (uint32_t i = 0; i < block; i++)
                peak = std::max(peak, (double)std::abs(L[i]));
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double rendered = (double)blocks * block / rate;
        d.keys(0);
        std::printf("%-14s %-22s %10.2f %10.3f %8.1f%s\n", lc->core()->id, lc->core()->version, rendered, wall,
                    rendered / wall, peak > 0 ? "" : "  (silent)");
        std::fflush(stdout);
    }
    return bad ? 1 : 0;
}
