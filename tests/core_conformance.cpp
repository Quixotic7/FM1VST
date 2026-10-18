// SPDX-License-Identifier: GPL-3.0-only
// The conformance test every core passes (CORES-PLAN.md 3; one ctest per core CMake builds: core_conformance_<id>).
//
//   core_conformance MODULE.fm1core ID FAMILY LICENSE [CONFORMANCE.txt] [OUT_DIR]
//
// On the loaded module, through the engine's Device as the plugin drives it:
//   1. descriptor  ABI 6, id / name / version / source_url / family / license filled (family and licence as CMake's
//                  fm1_add_core gave them), gated NULL or a reason, the 14 button names, the keymap (every key id
//                  0..40 once on rows 1..4), the required entry points, a 1 MiB flash, the credits NULL or text;
//                  after init the panel table (btn_id a permutation of 0..13, enc_id of 0..6, enc_dir +-1)
//   2. screen      boots from a fresh flash (the staged boot, as the plugin does); the LCD is not blank (two or more
//                  colours) within boot_ms (3 s) of device time; written to OUT_DIR/<id>.ppm
//   3. sound       after the `sound` script (if any) and settle_ms of device time (1 s), the note key (D4) is held
//                  hold_ms (500), and within 1 s of the press the audio is not silent (a peak above 64)
//   4. flash       the round trip: a fresh unit changes something the firmware saves (the `save` script, else the
//                  first FM1P_PERSIST parameter of the core's map), flash_sync, shutdown; a second load boots from
//                  that image and the change is still there (`saved` expects / `saved-param`, the parameter, or at
//                  least an image that still differs from a fresh unit's). `flash none <reason>` skips it.
//   5. double load two copies in one process: two panels, two flash images, both boot and draw; a key on one does
//                  not reach the other; the first unloaded, the second runs on
//   6. reboot      if declared: `reboot-guard N [reason]` (the boot guard's UBOOT: halted right after init) and / or
//                  a `reboot` script (halted by its end); then the clock gives silence. Not declared: skipped.
//
// CONFORMANCE.txt (core-glue/<family>/conformance.txt, or conformance_<id>.txt; '#' comments):
//   boot_ms N | settle_ms N | key NOTE (F3..G5) | hold N
//   sound <script line>           (repeatable) the emulator's script grammar (tests/script_runner.h), run from ms 0
//   save <script line>            (repeatable) the change to save, run from ms 0 of the first unit (end a trailing
//                                 wait with `quit`, as for every script here)
//   saved <script line>           (repeatable) run from ms 0 of the second unit; its expects must pass
//   saved-param <name> <value>    the Tier 2 parameter <name> reads <value> on the second unit
//   saved-lcd-differs             after the `saved` script (or 1 s) the second unit's screen differs from a fresh
//                                 unit's after the same (a saved palette, theme, page ..)
//   flash none <reason>           the firmware saves nothing: the round trip is skipped
//   reboot <script line>          (repeatable) a scripted reboot from ms 0: halted() by the script's end (a script
//                                 runs to its last command's ms + 15: end a trailing wait with `quit`)
//   reboot-guard <fail> [reason]  boot_options(fail, reason, -1) before init: halted() after init
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "ppm.h"
#include "conformance_conf.h"
#include "script_runner.h"

namespace {

int fails = 0, skips = 0;
void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}
void skip(const std::string &what)
{
    std::printf("  SKIP  %s\n", what.c_str());
    skips++;
}

bool lcd_blank(const Device &d)
{
    const uint16_t *p = d.lcd();
    for (int i = 1; i < FM1P_LCD_W * FM1P_LCD_H; i++)
        if (p[i] != p[0])
            return false;
    return true;
}

void write_ppm(const std::string &path, const Device &d) { fm1g_write_lcd_ppm(path.c_str(), d.lcd()); }   // (common/ppm.h)

int peak_of(Device &d)                             // the audio produced since the last take, its peak
{
    int16_t buf[FM1P_BLOCK * 2 * 4];
    int pk = 0;
    for (uint32_t n; (n = d.take_s16(buf, FM1P_BLOCK * 4)) > 0;)
        for (uint32_t i = 0; i < 2 * n; i++)
            pk = std::max(pk, std::abs((int)buf[i]));
    return pk;
}

void run_for(Device &d, uint32_t ms) { for (uint32_t i = 0; i < ms; i++) d.run_ms(); }

// a script from the device's power-on (ms 0); false and a message on a parse error. ok / fail: its expects
bool run_script(Device &d, const std::string &text, const std::string &name, int *ok = nullptr, int *fail = nullptr,
                std::vector<std::string> *log = nullptr, const std::function<void()> &each = nullptr)
{
    Script s;
    std::string err;
    if (!parse_script_text(text, name, s, err)) {
        check(false, name + ": " + err);
        return false;
    }
    ScriptRunner r(d, s);
    r.keep_pcm = false;
    while (!r.done()) {
        r.step();
        if (each)
            each();
    }
    if (ok)
        *ok = r.expect_ok();
    if (fail)
        *fail = r.expect_fail();
    if (log)
        *log = r.log();
    return true;
}

const fm1param_t *persist_param(const fm1core_t *c, uint32_t &idx)
{
    for (uint32_t i = 0; i < c->nparams; i++) {
        const fm1param_t &p = c->params[i];
        if ((p.flags & FM1P_PERSIST) && !(p.flags & (FM1P_META | FM1P_HIDDEN)) && p.max > p.min && p.get && p.set) {
            idx = i;
            return &p;
        }
    }
    return nullptr;
}
const fm1param_t *param_named(const fm1core_t *c, const std::string &name)
{
    for (uint32_t i = 0; i < c->nparams; i++)
        if (c->params[i].name && name == c->params[i].name)
            return &c->params[i];
    return nullptr;
}

bool sync_flash(Device &d)                         // flash_sync until current (the clock runs between tries)
{
    for (int t = 0; t < 400; t++) {
        if (d.flash_sync())
            return true;
        run_for(d, 15);
    }
    return false;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 5) {
        std::printf("usage: core_conformance MODULE.fm1core ID FAMILY LICENSE [CONFORMANCE.txt] [OUT_DIR]\n");
        return 2;
    }
    const std::string module = argv[1], want_id = argv[2], want_family = argv[3], want_license = argv[4];
    const Conf conf = read_conf(argc > 5 ? argv[5] : "");
    const std::string out_dir = argc > 6 ? argv[6] : ".";
    std::string err;
    std::printf("core_conformance %s: %s\n", want_id.c_str(), module.c_str());
    if (conf.found)
        std::printf("  (script: %s)\n", conf.path.c_str());
    for (const std::string &e : conf.errors)
        check(false, conf.path + ": " + e);

    // ---- 1. the descriptor
    {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        check(lc != nullptr, "loads (ABI " + std::to_string(FM1CORE_ABI) + ")" + (lc ? "" : ": " + err));
        if (!lc)
            return 1;
        const fm1core_t *c = lc->core();
        auto filled = [](const char *s) { return s && *s; };
        check(c->abi_version == FM1CORE_ABI && c->id && want_id == c->id, std::string("id \"") + (c->id ? c->id : "") +
                                                                              "\" (the module is " + want_id + ")");
        check(filled(c->name) && filled(c->version) && filled(c->source_url),
              std::string("name \"") + (c->name ? c->name : "") + "\", version \"" + (c->version ? c->version : "") +
                  "\", source " + (c->source_url ? c->source_url : "(none)"));
        check(filled(c->family) && want_family == c->family,
              std::string("family \"") + (c->family ? c->family : "") + "\" (CMake: " + want_family + ")");
        check(filled(c->license) && want_license == c->license,
              std::string("license \"") + (c->license ? c->license : "") + "\" (CMake: " + want_license + ")");
        check(!c->gated || *c->gated, std::string("gated: ") + (c->gated ? c->gated : "no"));
        {
            const char *cr = fm1core_credits(c);
            const std::string first = cr ? std::string(cr).substr(0, std::string(cr).find('\n')) : "";
            check(!cr || *cr, std::string("credits (ABI 6): ") +
                                  (cr ? std::to_string(std::strlen(cr)) + " bytes, \"" + first.substr(0, 90) + "\"" : "none"));
        }
        bool names = true;
        std::string all;
        for (int i = 0; i < FM1P_NB; i++) {
            names = names && filled(c->button_names[i]);
            all += std::string(i ? " " : "") + (c->button_names[i] ? c->button_names[i] : "?");
        }
        check(names, "the 14 button names: " + all);
        bool km = c->keymap != nullptr;
        if (km) {
            std::multiset<int> ids;
            for (int r = 1; r <= FM1P_NLINE; r++)
                for (int col = 0; col < FM1P_NCOL; col++)
                    if (c->keymap[r * FM1P_NCOL + col] >= 0)
                        ids.insert(c->keymap[r * FM1P_NCOL + col]);
            for (int id = 0; id < FM1P_NB + FM1P_NKEY; id++)
                km = km && ids.count(id) == 1;
            km = km && ids.size() == (size_t)(FM1P_NB + FM1P_NKEY);
        }
        check(km, "the keymap: every key id 0..40 once on rows 1..4");
        check(c->panel && c->options && c->init && c->tick && c->frame && c->idle && c->audio && c->midi_in &&
                  c->midi_out_take && c->shutdown && c->halted && c->flash_stage && c->flash && c->flash_dirty,
              "the panel and the required entry points");
        check(c->flash_size == 0x100000u, "a 1 MiB flash (" + std::to_string(c->flash_size) + ")");
        std::printf("        (%u parameters%s%s%s)\n", (unsigned)c->nparams, c->knob_target ? ", knob targets" : "",
                    c->flash_sync ? ", flash_sync" : "", c->boot_options ? ", a boot guard" : "");
        Device d(c);
        d.boot_from(nullptr, 0);
        uint32_t b = 0, e = 0;
        bool dir = true;
        for (int i = 0; i < FM1P_NB; i++)
            b |= c->panel->btn_id[i] < FM1P_NBTN ? 1u << c->panel->btn_id[i] : 0x80000000u;
        for (int i = 0; i < FM1P_NE - 1; i++) {
            e |= c->panel->enc_id[i] < FM1P_NENC ? 1u << c->panel->enc_id[i] : 0x80000000u;
            dir = dir && (c->panel->enc_dir[i] == 1 || c->panel->enc_dir[i] == -1);
        }
        check(b == 0x3FFFu && e == 0x7Fu && dir, "after init: the panel table (btn_id 0..13, enc_id 0..6, enc_dir +-1)");
        check(!d.halted(), "init: running");
    }

    // ---- 2. the screen, 3. the sound
    {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        Device d(lc->core());
        d.boot_from(nullptr, 0);
        uint32_t drawn = 0;
        bool shown = !lcd_blank(d);
        while (!shown && d.ms() < conf.boot_ms) {
            d.run_ms();
            shown = !lcd_blank(d);
        }
        drawn = d.ms();
        write_ppm(out_dir + "/" + want_id + ".ppm", d);
        check(shown, "the screen is not blank within " + std::to_string(conf.boot_ms) + " ms (at " +
                         std::to_string(drawn) + " ms; " + out_dir + "/" + want_id + ".ppm)");
        lc.reset();
        // the sound, on a fresh unit: its `sound` script from power-on, the settle time, the key
        lc = CoreLoader::load(module, &err);
        Device s(lc->core());
        s.boot_from(nullptr, 0);
        if (!conf.sound.empty())
            run_script(s, conf.sound, want_id + " sound");
        while (s.ms() < conf.settle_ms)
            s.run_ms();
        run_for(s, 300);
        const int before = peak_of(s);
        const uint32_t t0 = s.ms();
        s.key_vel(conf.key, 100);
        s.keys(1u << conf.key);
        int after = 0;
        uint32_t heard = 0;
        while (s.ms() - t0 < 1000) {
            if (s.ms() - t0 == conf.hold_ms)
                s.keys(0);
            s.run_ms();
            const int pk = peak_of(s);
            if (pk > 64 && !heard)
                heard = s.ms() - t0;
            after = std::max(after, pk);
        }
        s.keys(0);
        check(after > 64 && !s.halted(), "key " + note_name(conf.key) + " held " + std::to_string(conf.hold_ms) +
                                             " ms: sound within 1 s (peak " + std::to_string(after) + " from " +
                                             std::to_string(heard) + " ms; before the key " + std::to_string(before) +
                                             ")");
    }

    // ---- 4. the flash round trip
    if (conf.flash_none) {
        skip("flash round trip: " + conf.flash_none_why);
    } else {
        std::vector<uint8_t> image, fresh;
        // a fresh unit's image after the same run (the reference for "still differs")
        {
            std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
            Device d(lc->core());
            d.boot_from(nullptr, 0);
            run_for(d, 1500);
            sync_flash(d);
            lc->core()->shutdown();
            fresh.assign(d.flash(), d.flash() + d.flash_size());
        }
        bool changed = false;
        std::string how, pname;
        int32_t pvalue = 0;
        {
            std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
            const fm1core_t *c = lc->core();
            Device d(c);
            d.boot_from(nullptr, 0);
            if (!conf.save.empty()) {
                changed = run_script(d, conf.save, want_id + " save");
                how = "the save script";
            } else {
                run_for(d, 1000);
                uint32_t idx = 0;
                if (const fm1param_t *p = persist_param(c, idx)) {
                    const int32_t v = p->get();
                    pvalue = v < p->max ? v + 1 : v - 1;
                    p->set(pvalue);
                    run_for(d, 200);
                    changed = p->get() == pvalue;
                    pname = p->name ? p->name : "?";
                    how = "the parameter \"" + pname + "\" " + std::to_string(v) + " -> " + std::to_string(pvalue);
                }
            }
            if (how.empty()) {
                check(false, "flash round trip: nothing to change: add `save` lines (and `saved` expects) or "
                             "`flash none <reason>` to " + (conf.found ? conf.path : "core-glue/<family>/conformance.txt"));
            } else {
                check(changed, "flash round trip: changed " + how);
                run_for(d, 500);
                check(sync_flash(d), "flash round trip: flash_sync made the image current");
                c->shutdown();
                image.assign(d.flash(), d.flash() + d.flash_size());
                check(image != fresh, "flash round trip: the image differs from a fresh unit's");
            }
        }
        if (!how.empty()) {
            std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
            const fm1core_t *c = lc->core();
            Device d(c);
            d.boot_from(image.data(), (uint32_t)image.size());
            bool explicit_check = false;
            if (!conf.saved.empty()) {
                int ok = 0, fail = 0;
                std::vector<std::string> log;
                run_script(d, conf.saved, want_id + " saved", &ok, &fail, &log);
                for (const std::string &l : log)
                    std::printf("        %s\n", l.c_str());
                if (ok + fail > 0)
                    check(fail == 0, "flash round trip: after the reboot the saved expects pass (" + std::to_string(ok) +
                                         " ok, " + std::to_string(fail) + " failed)");
                explicit_check = ok + fail > 0;
            } else {
                run_for(d, 1000);
            }
            if (conf.has_saved_param || !pname.empty()) {
                const std::string n = conf.has_saved_param ? conf.saved_param : pname;
                const int32_t want = conf.has_saved_param ? conf.saved_value : pvalue;
                const fm1param_t *p = param_named(c, n);
                check(p && p->get() == want, "flash round trip: after the reboot \"" + n + "\" reads " +
                                                 (p ? std::to_string(p->get()) : std::string("(no such parameter)")) +
                                                 " (saved " + std::to_string(want) + ")");
                explicit_check = true;
            }
            if (conf.saved_lcd_differs) {
                std::unique_ptr<LoadedCore> lf = CoreLoader::load(module, &err);
                Device f(lf->core());
                f.boot_from(nullptr, 0);
                if (!conf.saved.empty())
                    run_script(f, conf.saved, want_id + " saved (fresh)");
                else
                    run_for(f, 1000);
                size_t diff = 0;
                for (int i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++)
                    diff += d.lcd()[i] != f.lcd()[i];
                write_ppm(out_dir + "/" + want_id + "_saved.ppm", d);
                check(diff > 0, "flash round trip: after the reboot the screen differs from a fresh unit's (" +
                                    std::to_string(diff) + " pixels; " + want_id + "_saved.ppm)");
                explicit_check = true;
            }
            sync_flash(d);
            c->shutdown();
            const std::vector<uint8_t> again(d.flash(), d.flash() + d.flash_size());
            if (!explicit_check)
                check(again != fresh, "flash round trip: after the reboot the image still differs from a fresh unit's");
            check(!d.halted(), "flash round trip: the second unit booted from the image and runs");
        }
    }

    // ---- 5. double load
    {
        std::unique_ptr<LoadedCore> a = CoreLoader::load(module, &err), b = CoreLoader::load(module, &err);
        check(a && b, "double load: two copies loaded");
        if (a && b) {
            const fm1core_t *ca = a->core(), *cb = b->core();
            check(ca != cb && ca->panel != cb->panel && ca->flash() != cb->flash(),
                  "double load: two descriptors, two panels, two flash images");
            std::unique_ptr<Device> da(new Device(ca)), db(new Device(cb));
            da->boot_from(nullptr, 0);
            db->boot_from(nullptr, 0);
            for (uint32_t i = 0; i < std::max<uint32_t>(conf.settle_ms, 500); i++) {
                da->run_ms();
                db->run_ms();
            }
            peak_of(*da);
            peak_of(*db);
            da->keys(1u << conf.key);
            for (int i = 0; i < 300; i++) {
                da->run_ms();
                db->run_ms();
            }
            check(((ca->panel->keys >> conf.key) & 1u) && !cb->panel->keys, "double load: a key on one reaches only it");
            da->keys(0);
            da.reset();
            a.reset();
            for (int i = 0; i < 300; i++)
                db->run_ms();
            check(!db->halted() && !lcd_blank(*db), "double load: the first unloaded, the second runs on and draws");
        }
    }

    // ---- 6. the reboot
    if (conf.reboot_none)
        skip("reboot: " + conf.reboot_none_why);
    if (conf.guard_fail >= 0) {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        const fm1core_t *c = lc->core();
        if (!c->boot_options) {
            check(false, "reboot-guard: the core has no boot_options");
        } else {
            c->flash_stage(nullptr, 0);
            c->options(FM1CORE_FLASH_STAGED, 0, 0, 1);
            const int ok = c->boot_options(conf.guard_fail, conf.guard_reason.empty() ? nullptr : conf.guard_reason.c_str(), -1);
            c->panel->ready = 2;
            c->init(0);
            check(ok == 1 && c->halted() != 0, "reboot-guard " + std::to_string(conf.guard_fail) + (conf.guard_reason.empty() ? "" : " " + conf.guard_reason) +
                                                   ": halted() = 0x" + [&] { char h[16]; std::snprintf(h, sizeof h, "%X", (unsigned)c->halted()); return std::string(h); }() +
                                                   " after init");
            int16_t blk[FM1P_BLOCK * 2];
            int pk = 0;
            for (int i = 0; i < 40; i++) {
                c->tick((uint32_t)i);
                c->frame();
                c->audio(blk, FM1P_BLOCK);
                for (int k = 0; k < FM1P_BLOCK * 2; k++)
                    pk = std::max(pk, std::abs((int)blk[k]));
            }
            check(pk == 0, "reboot-guard: halted: the clock gives silence");
        }
    }
    if (!conf.reboot.empty()) {
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
        Device d(lc->core());
        d.boot_from(nullptr, 0);
        uint32_t at = 0;
        run_script(d, conf.reboot, want_id + " reboot", nullptr, nullptr, nullptr, [&] {
            if (!at && d.halted())
                at = d.ms();
        });
        peak_of(d);
        run_for(d, 200);
        const int pk = peak_of(d);
        check(d.halted(), "reboot script: halted() at " + std::to_string(at) + " ms");
        check(pk == 0, "reboot script: then silence");
    } else if (conf.guard_fail < 0 && !conf.reboot_none) {
        skip("reboot: none declared in " + (conf.found ? conf.path : std::string("(no conformance.txt)")));
    }

    std::printf("core_conformance %s: %s (%d skipped)\n", want_id.c_str(), fails ? "FAIL" : "PASS", skips);
    return fails ? 1 : 0;
}
