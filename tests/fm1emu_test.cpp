// SPDX-License-Identifier: GPL-3.0-only
// The fm1emu core beyond the conformance test (core-glue/fm1emu/README.md):
//
//   fm1emu_test FM1EMU.fm1core FELUCCA.fwsc MVAVE.fwsc FELUCCA_NATIVE.fm1core [OUT_DIR] [--memory]
//
//   1. no firmware      nothing staged, no FM1EMU_FWSC: the LOAD .FWSC screen, silence, running (halted() 0)
//   2. the image        a package staged as bytes (case 1) boots; flash() is the 0x210000 fm1emu image (the NOR, the
//                       package in its slot); that image staged into a second load (case 2) boots the same package
//   3. two instances    Felucca 1.6.1 and the stock V15 in one process, side by side (their clocks interleaved a
//                       millisecond at a time) for 3 s and more: both draw, both sound, a key on one reaches only it
//   4. latency          the same key on fm1emu Felucca and on the native felucca core: the first audible frame of
//                       each, the difference (the emulator's added delay), and fm1core_latency_frames (dlsym)
//   5. memory           (--memory: only this, in a fresh process) phys_footprint after 1, 2 and 4 loaded and
//                       booted instances (Felucca, run 2 s), after unloading them, and after loading 4 again
//   6. JIT              ui_info's speed with the JIT on (two instances, above) and off (FM1EMU_JIT=0)
// Not a pass/fail on machine-dependent numbers (speed, memory): those are printed for the README.
#include <dlfcn.h>
#include <mach/mach.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "ppm.h"

namespace {

int fails = 0;
void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);
    fails += !c;
}

bool read_bytes(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool lcd_blank(const Device &d)
{
    const uint16_t *p = d.lcd();
    for (int i = 1; i < FM1P_LCD_W * FM1P_LCD_H; i++)
        if (p[i] != p[0])
            return false;
    return true;
}

int peak_of(Device &d)
{
    int16_t buf[FM1P_BLOCK * 2 * 4];
    int pk = 0;
    for (uint32_t n; (n = d.take_s16(buf, FM1P_BLOCK * 4)) > 0;)
        for (uint32_t i = 0; i < 2 * n; i++)
            pk = std::max(pk, std::abs((int)buf[i]));
    return pk;
}

std::string info(const fm1core_t *c)
{
    char b[512];
    c->ui_info(b, sizeof b);
    return b;
}

double footprint_mb()
{
    task_vm_info_data_t vi;
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &n) != KERN_SUCCESS)
        return -1;
    return (double)vi.phys_footprint / (1024.0 * 1024.0);
}

typedef uint32_t (*latency_fn)(void);
latency_fn latency_of(const LoadedCore &lc)
{
    // the copy is loaded RTLD_LOCAL: its handle again (RTLD_NOLOAD), then the extra symbol
    void *h = dlopen(lc.copy_path().c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
    if (!h)
        return nullptr;
    latency_fn f = reinterpret_cast<latency_fn>(dlsym(h, "fm1core_latency_frames"));
    dlclose(h);   // (drops the reference NOLOAD added)
    return f;
}

// the first frame (counted from power-on) whose sample is above `level`, after the key goes down at ms `at`
struct Onset {
    uint64_t key_frame = 0, first = 0;
    bool heard = false;
};
Onset onset(Device &d, uint32_t at_ms, int key, uint32_t until_ms, int level = 256)
{
    Onset o;
    uint64_t taken = 0;
    int16_t buf[FM1P_BLOCK * 2 * 4];
    while (d.ms() < until_ms) {
        if (d.ms() == at_ms) {
            d.key_vel(key, 100);
            d.keys(1u << key);
            o.key_frame = (uint64_t)at_ms * 44100 / 1000;
        }
        d.run_ms();
        for (uint32_t n; (n = d.take_s16(buf, FM1P_BLOCK * 4)) > 0;) {
            for (uint32_t i = 0; i < n && !o.heard; i++)
                if (d.ms() > at_ms && (std::abs((int)buf[2 * i]) > level || std::abs((int)buf[2 * i + 1]) > level)) {
                    o.heard = true;
                    o.first = taken + i;
                }
            taken += n;
        }
    }
    d.keys(0);
    return o;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 5) {
        std::printf("usage: fm1emu_test FM1EMU.fm1core FELUCCA.fwsc MVAVE.fwsc FELUCCA_NATIVE.fm1core [OUT_DIR]\n");
        return 2;
    }
    const std::string module = argv[1], native = argv[4], out = argc > 5 ? argv[5] : ".";
    const bool memory_only = argc > 6 && std::string(argv[6]) == "--memory";
    std::vector<uint8_t> fel, stock;
    if (!read_bytes(argv[2], fel) || !read_bytes(argv[3], stock)) {
        std::printf("fm1emu_test: cannot read %s or %s\n", argv[2], argv[3]);
        return 1;
    }
    unsetenv("FM1EMU_FWSC");
    std::string err;
    std::printf("fm1emu_test: %s\n", module.c_str());
    const double mem0 = footprint_mb();

    // ---- 1. no firmware
    if (!memory_only) {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        check(lc != nullptr, "loads" + (lc ? "" : ": " + err));
        if (!lc)
            return 1;
        Device d(lc->core());
        d.boot_from(nullptr, 0);
        for (int i = 0; i < 500; i++)
            d.run_ms();
        fm1g_write_lcd_ppm((out + "/fm1emu_empty.ppm").c_str(), d.lcd());
        check(!lcd_blank(d) && peak_of(d) == 0 && !d.halted(),
              "no firmware: the LOAD .FWSC screen, silence, running (" + out + "/fm1emu_empty.ppm; " + info(lc->core()) + ")");
        check(lc->core()->flash_size == 0x210000u, "flash_size 0x210000 (the NOR, then the package slot)");
    }

    // ---- 2. the image: staged package bytes (case 1), then the image (case 2)
    std::vector<uint8_t> image;
    if (!memory_only) {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        Device d(lc->core());
        d.boot_from(fel.data(), (uint32_t)fel.size());
        for (int i = 0; i < 3000; i++)
            d.run_ms();
        check(!lcd_blank(d) && !d.halted(), "case 1: Felucca staged as bytes boots and draws (" + info(lc->core()) + ")");
        lc->core()->shutdown();
        image.assign(d.flash(), d.flash() + d.flash_size());
        uint32_t len = 0;
        std::memcpy(&len, &image[0x100008], 4);
        check(image.size() == 0x210000u && !std::memcmp(&image[0x100000], "FM1EMUPK", 8) && len == fel.size() &&
                  !std::memcmp(&image[0x100010], fel.data(), fel.size()),
              "flash(): the image carries the package in its slot (" + std::to_string(len) + " bytes)");
    }
    if (!memory_only) {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        Device d(lc->core());
        d.boot_from(image.data(), (uint32_t)image.size());
        for (int i = 0; i < 3000; i++)
            d.run_ms();
        const std::string inf = info(lc->core());
        check(!lcd_blank(d) && !d.halted() && inf.find("from image") != std::string::npos,
              "case 2: the image boots its own package (" + inf + ")");
        std::vector<uint8_t> nor(image.begin(), image.begin() + 0x100000);
        lc->core()->shutdown();
        size_t differ = 0;
        for (size_t s = 0; s < 0x100000; s += 4096)
            differ += std::memcmp(&nor[s], d.flash() + s, 4096) != 0;
        std::printf("        (the second run's NOR differs from the image it booted in %zu sectors)\n", differ);
    }

    // ---- 3. two instances, two packages
    if (!memory_only) {
        std::unique_ptr<LoadedCore> la = CoreLoader::load(module, &err), lb = CoreLoader::load(module, &err);
        check(la && lb && la->core() != lb->core() && la->core()->panel != lb->core()->panel,
              "two instances: two copies, two descriptors, two panels");
        Device a(la->core()), b(lb->core());
        a.boot_from(fel.data(), (uint32_t)fel.size());
        b.boot_from(stock.data(), (uint32_t)stock.size());
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 6000; i++) {
            a.run_ms();
            b.run_ms();
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        fm1g_write_lcd_ppm((out + "/fm1emu_two_felucca.ppm").c_str(), a.lcd());
        fm1g_write_lcd_ppm((out + "/fm1emu_two_mvave.ppm").c_str(), b.lcd());
        check(!lcd_blank(a) && !lcd_blank(b), "two instances: both draw after 6 s (fm1emu_two_*.ppm; 6 s of both in " +
                                                  std::to_string(wall).substr(0, 5) + " s wall)");
        peak_of(a);
        peak_of(b);
        // a key on A: A sounds, B's panel sees nothing and B stays silent
        a.key_vel(9, 100);
        a.keys(1u << 9);
        int pa = 0, pb = 0;
        for (int i = 0; i < 1000; i++) {
            if (i == 500)
                a.keys(0);
            a.run_ms();
            b.run_ms();
            pa = std::max(pa, peak_of(a));
            pb = std::max(pb, peak_of(b));
            if (b.panel()->keys)
                pb = 1 << 20;
        }
        check(pa > 64 && pb <= 64, "two instances: a key on Felucca sounds there (peak " + std::to_string(pa) +
                                       "), not on the stock one (peak " + std::to_string(pb) + ")");
        for (int i = 0; i < 2000; i++) {
            a.run_ms();
            b.run_ms();
        }
        peak_of(a);
        peak_of(b);
        b.key_vel(9, 100);
        b.keys(1u << 9);
        pa = pb = 0;
        for (int i = 0; i < 1000; i++) {
            if (i == 500)
                b.keys(0);
            a.run_ms();
            b.run_ms();
            pa = std::max(pa, peak_of(a));
            pb = std::max(pb, peak_of(b));
        }
        check(pb > 64 && pa <= 64, "two instances: a key on the stock one sounds there (peak " + std::to_string(pb) +
                                       "), not on Felucca (peak " + std::to_string(pa) + ")");
        check(!a.halted() && !b.halted(), "two instances: both running (A: " + info(la->core()) + " | B: " + info(lb->core()) + ")");
    }

    // ---- 4. latency against the native core
    if (!memory_only) {
        std::unique_ptr<LoadedCore> le = CoreLoader::load(module, &err), ln = CoreLoader::load(native, &err);
        check(le && ln, "latency: fm1emu and the native felucca loaded" + (ln ? "" : ": " + err));
        if (le && ln) {
            Device e(le->core()), n(ln->core());
            e.boot_from(fel.data(), (uint32_t)fel.size());
            n.boot_from(nullptr, 0);
            const Onset oe = onset(e, 3000, 9, 4000), on = onset(n, 3000, 9, 4000);
            latency_fn lf = latency_of(*le);
            const uint32_t L = lf ? lf() : 0;
            check(oe.heard && on.heard, "latency: D4 at 3000 ms is heard on both (fm1emu " +
                                            std::to_string((long long)(oe.first - oe.key_frame)) + " frames after the key, native " +
                                            std::to_string((long long)(on.first - on.key_frame)) + "; the difference " +
                                            std::to_string((long long)oe.first - (long long)on.first) + " frames)");
            check(lf && L >= 128 && L % 128 == 0, "fm1core_latency_frames (dlsym): " + std::to_string(L) + " (" + info(le->core()) + ")");
        }
    }

    // ---- 5. memory
    if (memory_only) {
        std::printf("  memory: phys_footprint %.1f MB before any instance%s\n", mem0,
                    getenv("FM1EMU_JIT") ? " (FM1EMU_JIT set)" : "");
        std::vector<std::unique_ptr<LoadedCore>> cores;
        std::vector<std::unique_ptr<Device>> devs;
        double last = mem0;
        auto grow = [&](int want) {
            while ((int)cores.size() < want) {
                cores.push_back(CoreLoader::load(module, &err));
                devs.emplace_back(new Device(cores.back()->core()));
                devs.back()->boot_from(fel.data(), (uint32_t)fel.size());
            }
            for (int i = 0; i < 2000; i++)
                for (auto &d : devs) {
                    d->run_ms();
                    peak_of(*d);
                }
            const double now = footprint_mb();
            std::printf("  memory: %d instance(s) booted and run 2 s: %.1f MB (+%.1f MB since the last count)\n", want, now,
                        now - last);
            last = now;
        };
        auto unload = [&]() {
            std::vector<std::string> copies;
            for (auto &c : cores)
                copies.push_back(c->copy_path());
            devs.clear();
            cores.clear();
            const double after = footprint_mb();
            int still = 0;
            for (const std::string &p : copies) {
                void *h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
                if (h) {
                    still++;
                    dlclose(h);
                }
            }
            std::printf("  memory: all unloaded: %.1f MB (%.1f MB returned); dyld still has %d of the %zu images mapped\n",
                        after, last - after, still, copies.size());
            last = after;
        };
        for (int want : {1, 2, 4})
            grow(want);
        unload();
        grow(4);
        unload();
        std::printf("fm1emu_test: memory printed\n");
        return 0;
    }

    // ---- 6. the JIT off
    {
        setenv("FM1EMU_JIT", "0", 1);
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        Device d(lc->core());
        d.boot_from(fel.data(), (uint32_t)fel.size());
        for (int i = 0; i < 2500; i++)
            d.run_ms();
        const std::string inf = info(lc->core());
        check(inf.find("JIT off") != std::string::npos && !lcd_blank(d) && !d.halted(),
              "JIT off (FM1EMU_JIT=0): runs on the interpreter (" + inf + ")");
        unsetenv("FM1EMU_JIT");
    }

    std::printf("fm1emu_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
