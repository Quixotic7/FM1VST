// SPDX-License-Identifier: GPL-3.0-only
// tonitor_drive: the Tonitor core on the engine's Device driven by a script (tests/script_runner.h's grammar), its
// screen compared with Tonitor's own host harness's (tests/host/host.c `host screens DIR`): the golden check's
// FM1VST side (core-glue/tonitor/core.cmake, golden.cmake), and a development tool.
//
//   tonitor_drive MODULE.fm1core SCRIPT OUT_PREFIX [--compare MS REF.ppm]... [--ignore X0 Y0 X1 Y1]... [--at MS]...
//
// host.c writes its framebuffer (native RGB565) as 8-bit RGB by shifting (r << 3, g << 2, b << 3), where FM1VST's
// ppm.h scales (c * 255 / 31): the comparison is in RGB565, the reference's pixel shifted back. A pixel that differs
// outside the --ignore rectangles (inclusive; explained differences) fails it (exit 1). OUT_PREFIX_<ms>.ppm: our
// screen at each --compare and --at.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "ppm.h"
#include "script_runner.h"

struct Rect {
    int x0, y0, x1, y1;
};

static int compare(const Device &d, const std::string &ref, const std::vector<Rect> &ignore, std::string &why)
{
    std::vector<uint8_t> rgb(FM1G_RGB_BYTES);
    if (!fm1g_read_ppm(ref.c_str(), rgb.data())) {
        why = "cannot read " + ref;
        return -1;
    }
    const uint16_t *p = d.lcd();
    int n = 0, ign = 0, x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for (int y = 0; y < FM1P_LCD_H; y++)
        for (int x = 0; x < FM1P_LCD_W; x++) {
            const int i = y * FM1P_LCD_W + x;
            const uint16_t ours = (uint16_t)((p[i] >> 8) | (p[i] << 8));   // (big-endian as sent -> native)
            const uint16_t theirs = (uint16_t)((rgb[3 * i] >> 3) << 11 | (rgb[3 * i + 1] >> 2) << 5 | rgb[3 * i + 2] >> 3);
            if (ours == theirs)
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

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: tonitor_drive MODULE SCRIPT OUT_PREFIX [--compare MS REF.ppm].. [--ignore X0 Y0 X1 Y1]..\n");
        return 2;
    }
    const std::string module = argv[1], script = argv[2], out = argv[3];
    std::map<uint32_t, std::vector<std::string>> cmp;
    std::set<uint32_t> at;
    std::vector<Rect> ignore;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--compare") && i + 2 < argc) {
            cmp[(uint32_t)std::strtoul(argv[i + 1], nullptr, 10)].push_back(argv[i + 2]);
            i += 2;
        } else if (!std::strcmp(argv[i], "--at") && i + 1 < argc) {
            at.insert((uint32_t)std::strtoul(argv[++i], nullptr, 10));
        } else if (!std::strcmp(argv[i], "--ignore") && i + 4 < argc) {
            ignore.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atoi(argv[i + 3]), std::atoi(argv[i + 4])});
            i += 4;
        } else {
            std::fprintf(stderr, "tonitor_drive: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc) {
        std::fprintf(stderr, "load: %s\n", err.c_str());
        return 1;
    }
    Device d(lc->core());
    d.boot_from(nullptr, 0);
    Script s;
    if (!load_script(script, s, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    ScriptRunner r(d, s);
    r.keep_pcm = false;
    char info[256];
    int bad = 0;
    while (!r.done()) {
        r.step();
        const uint32_t ms = d.ms();
        if (at.count(ms) || cmp.count(ms)) {
            lc->core()->ui_info(info, sizeof info);
            fm1g_write_lcd_ppm((out + "_" + std::to_string(ms) + ".ppm").c_str(), d.lcd());
            std::printf("%6u ms: %s\n", (unsigned)ms, info);
        }
        auto it = cmp.find(ms);
        if (it == cmp.end())
            continue;
        for (const std::string &ref : it->second) {
            std::string why;
            const int n = compare(d, ref, ignore, why);
            std::printf("%6u ms: %s %s: %s\n", (unsigned)ms, n ? "DIFFERS from" : "same as", ref.c_str(), why.c_str());
            bad += n != 0;
        }
        cmp.erase(it);
    }
    for (const auto &c : cmp) {
        std::printf("%6u ms: not reached (the script ended)\n", (unsigned)c.first);
        bad++;
    }
    return bad ? 1 : 0;
}
