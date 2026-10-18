// SPDX-License-Identifier: GPL-3.0-only
// sloop_drive: a core on the engine's Device driven by a script (tests/script_runner.h's grammar), its screen
// written as PPM at chosen device ms and at the end. A development tool for the SLOOP-family glue and the golden
// checks' "our side" (core-glue/sloop/core.cmake).
//
//   sloop_drive MODULE.fm1core SCRIPT|- OUT_PREFIX [--at MS]... [--flash-in FILE] [--flash-out FILE] [--dump]
//
// OUT_PREFIX.ppm at the script's end, OUT_PREFIX_<ms>.ppm at each --at; prints ui_info there and the LEDs at the end.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "script_runner.h"

static void write_ppm(const std::string &path, const Device &d)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return;
    std::fprintf(f, "P6\n%d %d\n255\n", FM1P_LCD_W, FM1P_LCD_H);
    const uint16_t *p = d.lcd();
    for (int i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++) {
        const uint16_t c = (uint16_t)((p[i] >> 8) | (p[i] << 8));
        const uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                                (uint8_t)((c & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: sloop_drive MODULE SCRIPT|- OUT_PREFIX [--at MS].. [--flash-in F] [--flash-out F]\n");
        return 2;
    }
    const std::string module = argv[1], script = argv[2], out = argv[3];
    std::set<uint32_t> at;
    std::string fin, fout;
    bool dump = false;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--at") && i + 1 < argc)
            at.insert((uint32_t)std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "--flash-in") && i + 1 < argc)
            fin = argv[++i];
        else if (!std::strcmp(argv[i], "--flash-out") && i + 1 < argc)
            fout = argv[++i];
        else if (!std::strcmp(argv[i], "--dump"))
            dump = true;
    }
    std::string text;
    if (script == "-") {
        std::stringstream ss;
        ss << std::cin.rdbuf();
        text = ss.str();
    } else {
        std::ifstream in(script);
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc) {
        std::fprintf(stderr, "load: %s\n", err.c_str());
        return 1;
    }
    Device d(lc->core());
    std::vector<uint8_t> image;
    if (!fin.empty()) {
        std::ifstream f(fin, std::ios::binary);
        image.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    d.boot_from(image.empty() ? nullptr : image.data(), (uint32_t)image.size());
    Script s;
    if (!parse_script_text(text, script, s, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    ScriptRunner r(d, s);
    r.keep_pcm = false;
    char info[256];
    while (!r.done()) {
        r.step();
        if (at.count(d.ms())) {
            lc->core()->ui_info(info, sizeof info);
            std::printf("%6u ms: %s\n", (unsigned)d.ms(), info);
            write_ppm(out + "_" + std::to_string(d.ms()) + ".ppm", d);
        }
    }
    for (const std::string &l : r.log())
        std::printf("  %s\n", l.c_str());
    lc->core()->ui_info(info, sizeof info);
    std::printf("%6u ms (end): %s%s  expects %d ok %d failed\n", (unsigned)d.ms(), info, d.halted() ? " HALTED" : "",
                r.expect_ok(), r.expect_fail());
    write_ppm(out + ".ppm", d);
    std::printf("  leds:");
    for (int id = 0; id < 41; id++)
        std::printf(" %d", d.led_level(id));
    std::printf("\n");
    if (dump && lc->core()->dump)
        lc->core()->dump();
    if (!fout.empty()) {
        d.flash_sync();
        lc->core()->shutdown();
        std::ofstream f(fout, std::ios::binary);
        f.write((const char *)d.flash(), d.flash_size());
    }
    return r.expect_fail() ? 1 : 0;
}
