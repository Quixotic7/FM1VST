// SPDX-License-Identifier: GPL-3.0-only
// The firmware's exit() inside a host (core_choralroot.c): the two places the emulator ends its process must halt
// the core instead, the host running on.
//   1. the boot guard's UBOOT (--boot-fail 3 --reset-reason wdt: the emulator exits 3 during the power-on)
//   2. CR_REBOOT after SAFE MODE's Options > Flash Data (scripts/cr_safe_erase.txt with --boot-fail 1
//      --reset-reason wdt: the emulator prints "reboot:" and exits 0)
// Each: halted() reports the code, the clock entry points do nothing, the audio is silence, shutdown is safe.
#include "test_util.h"

int main()
{
    int fails = 0;
    std::string err;
    auto check = [&](bool c, const std::string &what) {
        std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
        fails += !c;
    };

    {   // 1. UBOOT during init
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(core_module(), &err);
        if (!lc) {
            std::printf("halt_test: %s\n", err.c_str());
            return 1;
        }
        const fm1core_t *c = lc->core();
        check(c->boot_options(3, "wdt", -1) == 1, "UBOOT: boot_options(3, wdt)");
        Device dev(c);
        dev.boot(nullptr, true);
        check(c->halted() == (0x100 | 3), "UBOOT: halted() = 0x103 after init (exit(3) returned to the host)");
        uint64_t nz = 0;
        int16_t buf[1024];
        for (int i = 0; i < 500; i++) {
            dev.run_ms();
            for (uint32_t n; (n = dev.take_s16(buf, 512)) > 0;)
                for (uint32_t k = 0; k < 2 * n; k++)
                    nz += buf[k] != 0;
        }
        check(nz == 0 && dev.ms() == 500, "UBOOT: 500 ms of the clock, silence");
    }   // (shutdown, unload)

    {   // 2. CR_REBOOT inside a frame
        Script s;
        if (!load_script(script_path("cr_safe_erase.txt"), s, err)) {
            std::printf("%s", err.c_str());
            return 1;
        }
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(core_module(), &err);
        const fm1core_t *c = lc->core();
        c->boot_options(1, "wdt", -1);
        Device dev(c);
        dev.boot(nullptr, true);
        check(c->halted() == 0, "SAFE MODE boot: running");
        ScriptRunner r(dev, s);
        uint32_t halt_ms = 0;
        size_t halt_at = 0;
        while (!r.done()) {
            r.step();
            if (!halt_ms && c->halted()) {
                halt_ms = dev.ms() - 1;
                halt_at = r.pcm().size();
            }
        }
        check(c->halted() == 0x100, "Flash Data erase: halted() = 0x100 (CR_REBOOT's exit(0)) at " +
                                        std::to_string(halt_ms) + " ms");
        size_t nz_after = 0;
        for (size_t i = halt_at; i < r.pcm().size(); i++)
            nz_after += r.pcm()[i] != 0;
        check(halt_ms > 0 && nz_after == 0, "after the halt: silence (" + std::to_string(r.pcm().size() - halt_at) +
                                                " samples), the clock ran on to " + std::to_string(dev.ms()) + " ms");
        c->shutdown();
        c->shutdown();
        check(true, "shutdown twice after the halt");
    }
    std::printf("halt_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
