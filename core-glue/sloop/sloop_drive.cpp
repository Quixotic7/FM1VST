// SPDX-License-Identifier: GPL-3.0-only
// sloop_drive: a core on the engine's Device driven by a script (tests/script_runner.h's grammar), its screen
// written as PPM at chosen device ms and at the end. A development tool for the SLOOP-family glue and the golden
// checks' "our side" (core-glue/sloop/core.cmake).
//
//   sloop_drive MODULE.fm1core SCRIPT|- OUT_PREFIX [--at MS]... [--flash-in FILE] [--flash-out FILE] [--dump]
//               [--compare MS REF.ppm]... [--ignore X0 Y0 X1 Y1]... [--max-diff N]
//
// OUT_PREFIX.ppm at the script's end, OUT_PREFIX_<ms>.ppm at each --at; prints ui_info there and the LEDs at the end.
// --compare: the screen at device ms MS against a reference PPM (a firmware's own host sim's), pixel for pixel,
// outside the --ignore rectangles (inclusive; explained differences); more than --max-diff (default 0) other differing
// pixels fail (exit 1).
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
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

static bool read_ppm(const std::string &path, std::vector<uint8_t> &rgb)
{
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int w = 0, h = 0, mx = 0;
    if (!(f >> magic >> w >> h >> mx) || magic != "P6" || w != FM1P_LCD_W || h != FM1P_LCD_H || mx != 255)
        return false;
    f.get();
    rgb.resize((size_t)w * h * 3);
    f.read((char *)rgb.data(), (std::streamsize)rgb.size());
    return (size_t)f.gcount() == rgb.size();
}

struct Rect {
    int x0, y0, x1, y1;
};

// the screen against a reference: the pixels that differ outside the ignored rectangles (their bounding box)
static int compare_ppm(const Device &d, const std::string &ref, const std::vector<Rect> &ignore, std::string &why)
{
    std::vector<uint8_t> rgb;
    if (!read_ppm(ref, rgb)) {
        why = "cannot read " + ref;
        return -1;
    }
    const uint16_t *p = d.lcd();
    int n = 0, ign = 0, x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for (int y = 0; y < FM1P_LCD_H; y++)
        for (int x = 0; x < FM1P_LCD_W; x++) {
            const int i = y * FM1P_LCD_W + x;
            const uint16_t c = (uint16_t)((p[i] >> 8) | (p[i] << 8));
            const uint8_t r = (uint8_t)(((c >> 11) & 31) * 255 / 31), g = (uint8_t)(((c >> 5) & 63) * 255 / 63),
                          b = (uint8_t)((c & 31) * 255 / 31);
            if (r == rgb[3 * i] && g == rgb[3 * i + 1] && b == rgb[3 * i + 2])
                continue;
            bool skip = false;
            for (const Rect &q : ignore)
                skip = skip || (x >= q.x0 && x <= q.x1 && y >= q.y0 && y <= q.y1);
            if (skip) {
                ign++;
                continue;
            }
            n++;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    why = std::to_string(n) + " pixels differ";
    if (n)
        why += " in (" + std::to_string(x0) + "," + std::to_string(y0) + ")..(" + std::to_string(x1) + "," +
               std::to_string(y1) + ")";
    if (ign)
        why += " (" + std::to_string(ign) + " more in the ignored areas)";
    return n;
}

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
    std::map<uint32_t, std::string> cmp;
    std::vector<Rect> ignore;
    int mismatches = 0, max_diff = 0;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--at") && i + 1 < argc)
            at.insert((uint32_t)std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "--flash-in") && i + 1 < argc)
            fin = argv[++i];
        else if (!std::strcmp(argv[i], "--flash-out") && i + 1 < argc)
            fout = argv[++i];
        else if (!std::strcmp(argv[i], "--dump"))
            dump = true;
        else if (!std::strcmp(argv[i], "--compare") && i + 2 < argc) {
            const uint32_t ms = (uint32_t)std::strtoul(argv[i + 1], nullptr, 10);
            cmp[ms] = argv[i + 2];
            i += 2;
        } else if (!std::strcmp(argv[i], "--max-diff") && i + 1 < argc) {
            max_diff = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--ignore") && i + 4 < argc) {
            ignore.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atoi(argv[i + 3]), std::atoi(argv[i + 4])});
            i += 4;
        }
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
        auto it = cmp.find(d.ms());
        if (it != cmp.end()) {
            std::string why;
            const int n = compare_ppm(d, it->second, ignore, why);
            std::printf("%6u ms: %s against %s: %s\n", (unsigned)d.ms(),
                        n > max_diff || n < 0 ? "DIFFERS" : n ? "same (within --max-diff)" : "same", it->second.c_str(),
                        why.c_str());
            if (n)
                write_ppm(out + "_" + std::to_string(d.ms()) + "_ours.ppm", d);
            if (n > max_diff || n < 0) {
                mismatches++;
            }
            cmp.erase(it);
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
    for (const auto &c : cmp) {
        std::printf("%6u ms: not reached (the script ended): %s\n", (unsigned)c.first, c.second.c_str());
        mismatches++;
    }
    return r.expect_fail() || mismatches ? 1 : 0;
}
