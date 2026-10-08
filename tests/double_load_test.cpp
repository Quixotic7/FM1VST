// SPDX-License-Identifier: GPL-3.0-only
// Two instruments in one process (FM1-VST-PLAN.md 4.3): the ChoralRoot module loaded twice through CoreLoader must
// give two firmwares with separate globals. A plays cr_dmaj.txt, B the same script with E4 for D4, their clocks
// interleaved one millisecond at a time; each must equal its own single-load run bit for bit (and A the reference
// WAV when it exists), A and B must differ. Then A is unloaded while B keeps running: B still plays, unchanged.
#include <string>

#include "test_util.h"

static std::string replace_all(std::string s, const std::string &from, const std::string &to)
{
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size())
        s.replace(p, from.size(), to);
    return s;
}

int main()
{
    std::string err, text;
    Script sa, sb;
    int fails = 0;
    auto check = [&](bool c, const std::string &what) {
        std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
        fails += !c;
    };
    if (!load_script(script_path("cr_dmaj.txt"), sa, err) || !read_file(script_path("cr_dmaj.txt"), text) ||
        !parse_script_text(replace_all(text, "btn D4", "btn E4"), "cr_dmaj.txt (E4)", sb, err)) {
        std::printf("double_load_test: %s\n", err.c_str());
        return 1;
    }

    // the core browser: the bundled folder (here: the build's cores/) and the user folder
    {
        const std::string bundled = core_module().substr(0, core_module().rfind('/'));
        std::vector<std::string> errs;
        const std::vector<CoreInfo> found = CoreLoader::scan({bundled, CoreLoader::default_user_dir()}, &errs);
        bool have = false;
        for (const CoreInfo &ci : found) {
            std::printf("  core: %s  id %s, \"%s\" %s, %s\n", ci.path.c_str(), ci.id.c_str(), ci.name.c_str(),
                        ci.version.c_str(), ci.source_url.c_str());
            have |= ci.id == "choralroot" && ci.flash_size == 0x100000u;
        }
        for (const std::string &e : errs)
            std::printf("  (skipped %s)\n", e.c_str());
        check(have, "scan: the ChoralRoot module found in " + bundled);
    }

    // the single-load runs
    Run a0, b0;
    if (!run_script_fresh(sa, a0, err) || !run_script_fresh(sb, b0, err)) {
        std::printf("double_load_test: %s\n", err.c_str());
        return 1;
    }

    // two loads at once
    std::unique_ptr<LoadedCore> la = CoreLoader::load(core_module(), &err);
    std::unique_ptr<LoadedCore> lb = la ? CoreLoader::load(core_module(), &err) : nullptr;
    if (!la || !lb) {
        std::printf("double_load_test: %s\n", err.c_str());
        return 1;
    }
    std::printf("double_load_test: A %s\n                  B %s\n", la->copy_path().c_str(), lb->copy_path().c_str());
    check(la->core() != lb->core(), "two descriptors");
    check(la->core()->hal != lb->core()->hal, "two hal pointers (separate globals)");
    check(la->core()->tick != lb->core()->tick, "two copies of the code");

    Device da(la->core()), db(lb->core());
    da.boot(nullptr, true);
    db.boot(nullptr, true);
    ScriptRunner ra(da, sa), rb(db, sb);
    const uint32_t unload_at = 1000;       // ms: A goes away while B's E chord is held (560..1460 ms)
    uint64_t b_nz_after = 0;
    std::vector<int16_t> a_pcm;
    while (!rb.done()) {
        if (la && !ra.done())
            ra.step();
        rb.step();
        if (la && db.ms() == unload_at) {
            a_pcm = ra.pcm();               // (the part A played: compared below up to here)
            const std::string copy = la->copy_path();
            la.reset();                     // shutdown, dlclose, the copy deleted
            check(mtime(copy) < 0, "A unloaded at " + std::to_string(unload_at) + " ms: its copy deleted");
            b_nz_after = rb.nonzero();
        }
    }
    b_nz_after = rb.nonzero() - b_nz_after;

    // A ran to unload_at alongside B: equal to its single-load run there; then a full A run interleaved
    check(!a_pcm.empty() && first_diff(a_pcm, a0.pcm) < 0,
          "A (cr_dmaj) interleaved with B equals the single-load run over its " + std::to_string(a_pcm.size()) +
              " samples before the unload");
    check(rb.pcm() == b0.pcm, "B (E4) equals its single-load run, all " + std::to_string(rb.pcm().size()) +
                                  " samples, across A's unload");
    check(b_nz_after > 0, "B still sounds after A's unload (" + std::to_string(b_nz_after) + " non-zero samples)");
    check(a0.pcm != b0.pcm && first_diff(a_pcm, rb.pcm()) >= 0, "A and B differ (a different chord)");

    // a full interleaved run of both (no unload), A against the single load and the reference
    {
        std::unique_ptr<LoadedCore> l1 = CoreLoader::load(core_module(), &err), l2 = CoreLoader::load(core_module(), &err);
        Device d1(l1->core()), d2(l2->core());
        d1.boot(nullptr, true);
        d2.boot(nullptr, true);
        ScriptRunner r1(d1, sa), r2(d2, sb);
        while (!r1.done() || !r2.done()) {
            r1.step();
            r2.step();
        }
        check(r1.pcm() == a0.pcm, "full interleaved run: A equals the single-load cr_dmaj (" +
                                      std::to_string(r1.pcm().size()) + " samples)");
        check(r2.pcm() == b0.pcm, "full interleaved run: B equals its single-load run");
        check(r1.pcm() != r2.pcm(), "full interleaved run: A and B differ");
        std::string wav, log;
        std::vector<int16_t> ref;
        if (ensure_reference("cr_dmaj.txt", wav, log) >= 0 && read_wav_s16(wav, ref, err))
            check(r1.pcm() == ref, "full interleaved run: A equals the reference emulator's WAV");
        else
            std::printf("  (no reference WAV: skipped the comparison with build/host/emu)\n");
    }
    // two different firmwares at once: ChoralRoot and Felucca loaded side by side, their clocks interleaved; each plays
    // D4 (key 9, 600..900 ms) and sounds; neither one's globals reach the other (Felucca's descriptor is its own)
    {
        const std::string fe = std::string(FM1_CORES_BUILD_DIR) + "/felucca.fm1core";
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(core_module(), &err), lf = CoreLoader::load(fe, &err);
        check(lc && lf && !std::strcmp(lc->core()->id, "choralroot") && !std::strcmp(lf->core()->id, "felucca") &&
                  lc->core()->hal != lf->core()->hal,
              "choralroot and felucca loaded together (two descriptors, two hals)");
        if (lc && lf) {
            Device dc(lc->core()), df(lf->core());
            dc.boot(nullptr, true);
            df.boot(nullptr, true);
            uint64_t nzc = 0, nzf = 0, nzc0 = 0, nzf0 = 0;
            int16_t buf[1024];
            for (uint32_t ms = 0; ms < 1500u; ms++) {
                const uint32_t k = ms >= 600u && ms < 900u ? 1u << 9 : 0u;
                dc.keys(k);
                df.keys(k);
                dc.run_ms();
                df.run_ms();
                for (uint32_t got; (got = dc.take_s16(buf, 512)) > 0;)
                    for (uint32_t i = 0; i < 2 * got; i++)
                        (ms < 600u ? nzc0 : nzc) += buf[i] != 0;
                for (uint32_t got; (got = df.take_s16(buf, 512)) > 0;)
                    for (uint32_t i = 0; i < 2 * got; i++)
                        (ms < 600u ? nzf0 : nzf) += buf[i] != 0;
            }
            check(nzc > 0 && nzf > 0 && !dc.halted() && !df.halted(),
                  "both play D4: choralroot " + std::to_string(nzc) + " non-zero samples (before the key " +
                      std::to_string(nzc0) + "), felucca " + std::to_string(nzf) + " (before " + std::to_string(nzf0) + ")");
        }
    }
    std::printf("double_load_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
