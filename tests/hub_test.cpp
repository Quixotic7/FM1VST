// SPDX-License-Identifier: GPL-3.0-only
// The Hub's firmwares on the fm1emu core (plugin/Hub.h, plugin/EmuClock.h, Processor.h EMULATED FIRMWARES), headless:
//
//   hub_test FELUCCA.fwsc GATED_IDS
//
//   (c) the catalog: tests/fixtures/hub/catalog.json parses (the entries with a package, the first of a repeated id,
//       the URLs resolved, categories and tags); the bundled fallback list parses and names no gated firmware
//       (GATED_IDS: CMake's gated cores, comma separated); the version order
//   (d) downloads from a file:// Hub (FM1VST_HUB / FM1VST_HUB_CATALOG at a scratch copy of the fixture): a SHA-256 or
//       size mismatch is refused and nothing is cached; the good package is cached and found again by SHA-256; the
//       Service fetches the catalog off the message thread (the run loop pumped for its callbacks)
//   (p) the processor: a Hub firmware picked boots on the fm1emu core and draws; firmwareCores() leaves the engine out;
//       the working flash per package version
//   (k) the clock: the choice reaches the core (ui_info's clock, and the cycles per device ms over 2 s at 240 and at
//       480 MHz); Auto's governor on fake timing (EmuClock.h) and through the processor (a step down reboots at 320;
//       while the host's transport plays it waits, says so, and steps when the transport stops)
//   (s) the state: the package by id and SHA-256 and the NOR only (its size), the clock; a second instance restores
//       it from the cache with the Hub unreachable; with the package not cached, the core waits (empty), the state
//       keeps the NOR unchanged, the package is fetched and the firmware comes up with that NOR; with the Hub gone too,
//       an error, and the NOR still kept
// FM1EMU_HOME points at a scratch folder under the build.
//
//   hub_test --live [ID]      (by hand, not a ctest: the network) the real Hub over https with the plugin's own code:
//                             the catalog, then ID's package (default melodee) downloaded, checked and cached
#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "built_cores.h"
#include "EmuClock.h"
#include "Hub.h"
#include "Processor.h"

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);
    fails += !c;
}
static std::string s(const juce::String &x) { return x.toStdString(); }

struct Host {
    FM1Processor &p;
    double sr;
    int bs;
    juce::AudioBuffer<float> buf;
    Host(FM1Processor &proc, double rate = 48000, int block = 512) : p(proc), sr(rate), bs(block), buf(2, block)
    {
        p.setRateAndBufferSizeDetails(rate, block);
        p.prepareToPlay(rate, block);
    }
    void run_ms(double ms)
    {
        for (int n = (int)(ms * sr / 1000.0 / bs) + 1; n > 0; n--) {
            juce::MidiBuffer m;
            buf.clear();
            p.processBlock(buf, m);
        }
    }
};

// a host's transport, playing or not
struct FakePlayHead : juce::AudioPlayHead {
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo pi;
        pi.setIsPlaying(playing);
        return pi;
    }
};

static std::string info(FM1Processor &p)
{
    Device *d = p.deviceForTest();
    if (!d || !d->core()->ui_info)
        return "";
    char b[600];
    d->core()->ui_info(b, sizeof b);
    return b;
}

static long long field(const std::string &inf, const std::string &key)   // "cycles 123" -> 123; "ms": "3000 ms;" -> 3000
{
    if (key == "ms") {
        const size_t e = inf.find(" ms;");
        const size_t b = e == std::string::npos ? e : inf.rfind(' ', e - 1);
        return b == std::string::npos ? -1 : std::atoll(inf.c_str() + b + 1);
    }
    const size_t i = inf.find(key + " ");
    return i == std::string::npos ? -1 : std::atoll(inf.c_str() + i + key.size() + 1);
}

static bool lcdDrawn(FM1Processor &p)
{
    PanelView v;
    v.seq = 0xFFFFFFFF;
    v.lcdSeq = 0xFFFFFFFF;
    p.getPanelView(v);
    for (size_t i = 1; i < v.lcd.size(); i++)
        if (v.lcd[i] != v.lcd[0])
            return true;
    return false;
}

// the message thread's queue (MessageManager::callAsync: the Hub's results) until done() or the timeout
static bool pump(const std::function<bool()> &done, double seconds)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (!done()) {
        if (juce::Time::getMillisecondCounterHiRes() - t0 > seconds * 1000)
            return false;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);
    }
    return true;
}

static void useHub(const juce::File &root)
{
    setenv("FM1VST_HUB", juce::URL(root).toString(false).toRawUTF8(), 1);
    setenv("FM1VST_HUB_CATALOG", juce::URL(root.getChildFile("catalog.json")).toString(false).toRawUTF8(), 1);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: hub_test FELUCCA.fwsc [GATED_IDS] | hub_test --live [ID]\n");
        return 2;
    }
    if (std::string(argv[1]) == "--live") {
        const juce::File scratch(FM1_SCRATCH_DIR "_live");
        scratch.deleteRecursively();
        scratch.createDirectory();
        setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
        unsetenv("FM1VST_HUB");
        unsetenv("FM1VST_HUB_CATALOG");
        juce::ScopedJuceInitialiser_GUI gui;
        hub::Catalog c;
        juce::String err;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const bool ok = hub::Service::fetchCatalogNow(c, &err);
        check(ok && c.firmwares.size() > 40, "the live catalog (" + s(hub::catalogUrl()) + "): " + std::to_string(c.firmwares.size()) +
                                                 " firmwares, updated " + s(c.updated) + " (" +
                                                 std::to_string((int)(juce::Time::getMillisecondCounterHiRes() - t0)) + " ms)" + s(err));
        const hub::Entry *e = c.find(argc > 2 ? argv[2] : "melodee");
        if (e) {
            juce::String derr;
            const juce::File f = hub::Service::downloadNow(*e, derr);
            check(f.existsAsFile() && f.getSize() == e->size, s(e->name) + " " + s(e->version) + " downloaded, checked (SHA-256 " +
                                                                  s(e->sha256.substring(0, 12)) + "), cached: " +
                                                                  s(f.getFullPathName()) + s(derr));
        }
        std::printf("hub_test --live: %s\n", fails ? "FAIL" : "PASS");
        return fails ? 1 : 0;
    }
    require_cores("hub_test", {"fm1emu", "felucca"});
    const juce::File felPkg(juce::String::fromUTF8(argv[1]));
    if (!felPkg.existsAsFile()) {
        std::printf("hub_test: SKIP: no package at %s\n", argv[1]);
        return 77;
    }
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    // the fixture Hub, served over file:// from a scratch copy
    const juce::File hubRoot = scratch.getChildFile("hubfix");
    hubRoot.getChildFile("fw").createDirectory();
    juce::File(FM1_HUB_FIXTURE).getChildFile("catalog.json").copyFileTo(hubRoot.getChildFile("catalog.json"));
    const juce::File served = hubRoot.getChildFile("fw/felucca-1.6.1.fwsc");
    felPkg.copyFileTo(served);
    useHub(hubRoot);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("hub_test: FM1EMU_HOME=%s, the Hub at %s\n", scratch.getFullPathName().toRawUTF8(), hub::catalogUrl().toRawUTF8());
    check(hub::dir() == scratch.getChildFile("FM1VST/hub"), "the Hub's folder follows FM1EMU_HOME: " + s(hub::dir().getFullPathName()));

    // ---- (c) the catalog
    hub::Catalog cat;
    juce::String err;
    check(hub::parse(hubRoot.getChildFile("catalog.json").loadFileAsString(), hub::baseUrl(), cat, &err), "the fixture parses" + s(err));
    check(cat.firmwares.size() == 3 && cat.find("felucca") && cat.find("badsha") && cat.find("badsize") && !cat.find("nopkg"),
          "three entries with a package (nopkg left out, the repeated felucca too): " + std::to_string(cat.firmwares.size()));
    const hub::Entry fel = cat.find("felucca") ? *cat.find("felucca") : hub::Entry{};
    check(fel.version == "1.6.1" && fel.size == 610090 && fel.sha256.startsWith("dff3a38f") &&
              fel.pkg == juce::URL(served).toString(false) && fel.source == "https://github.com/hugelton/Felucca",
          "felucca's entry: version, size, SHA-256, the package URL resolved (" + s(fel.pkg) + ")");
    check(fel.category() == "Groovebox" && fel.tag() == juce::String::fromUTF8("Multi-engine synth and\xe2\x80\xa6") && fel.shortAuthor() == "Leo Kuroshita",
          "category / tag / author as the site makes them: " + s(fel.category()) + " / " + s(fel.tag()) + " / " + s(fel.shortAuthor()));
    check(cat.stockSha256.startsWith("db1642b2") && cat.stockProduct == "FM-1_015", "the stock firmware's SHA-256 (recognised, never fetched)");
    {
        const hub::Catalog b = hub::bundled();
        juce::StringArray gated;
        gated.addTokens(argc > 2 ? juce::String(argv[2]) : juce::String(), ",", "");
        gated.removeEmptyStrings();
        // the gated firmwares by name too (a fork carrying the name: "SLOOP Floyd FM", "X0X-RMX")
        static const char *const names[] = {"choralroot", "melodee", "x0x", "fm-106", "duell", "rainbow", "fart machine", "orbit", "sloop",
                                            "no man sky"};
        std::string bad;
        for (const hub::Entry &e : b.firmwares) {
            if (gated.contains(hub::coreIdForHub(e.id)))
                bad += " " + s(e.id);
            for (const char *n : names)
                if (e.name.containsIgnoreCase(n))
                    bad += " " + s(e.name);
        }
        check(b.origin == "bundled" && b.firmwares.size() >= 20 && b.find("felucca") && gated.size() >= 10 && bad.empty(),
              "the bundled fallback list: " + std::to_string(b.firmwares.size()) + " firmwares, none of the " +
                  std::to_string(gated.size()) + " gated ones" + (bad.empty() ? "" : ": FOUND" + bad));
        const juce::String raw = juce::File(FM1_FALLBACK_CATALOG).loadFileAsString().toLowerCase();
        std::string inRaw;
        for (const juce::String &g : gated)
            if (raw.contains("\"id\": \"" + hub::hubIdForCore(g) + "\""))
                inRaw += " " + s(g);
        check(inRaw.empty(), "plugin/hub/fallback-catalog.json has no gated firmware's id" + (inRaw.empty() ? "" : ":" + inRaw));
    }
    {
        auto cmp = [](const char *a, const char *b) { const auto c = hub::compareVersions(a, b); return c ? *c : 99; };
        check(cmp("1.6.1", "1.6.0") == 1 && cmp("1.4.3", "1.4.4") == -1 && cmp("v2.4", "2.4") == 0 && cmp("0.9.9-0912abb", "0.9.9") == 99 &&
                  cmp("0.2.20-beta", "0.2.20") == -1 && cmp("1.0", "1.0.0") == 0 && hub::hubNewer("1.4.4", "1.4.3") &&
                  !hub::hubNewer("1.6.1", "1.6.1") && hub::hubNewer("Beta5", "Beta4"),
              "the version order (live.js compareVersions): numbers, pre-releases, unsure counts as the Hub's newer");
    }

    // ---- (d) downloads
    {
        juce::String e1, e2;
        const juce::File f1 = hub::Service::downloadNow(*cat.find("badsha"), e1);
        const juce::File f2 = hub::Service::downloadNow(*cat.find("badsize"), e2);
        check(f1 == juce::File() && e1.contains("refused") && e1.contains("SHA-256"), "a SHA-256 mismatch is refused: " + s(e1));
        check(f2 == juce::File() && e2.contains("refused") && e2.contains("bytes"), "a size mismatch is refused: " + s(e2));
        check(hub::packagesDir().findChildFiles(juce::File::findFiles, false, "*").isEmpty(), "  ... and nothing is cached");
        juce::String e3;
        double lastP = -2;
        const juce::File f3 = hub::Service::downloadNow(fel, e3, [&](double p) { lastP = p; });
        juce::MemoryBlock mb;
        check(f3.existsAsFile() && f3 == hub::packageFile("felucca", "1.6.1", fel.sha256) && hub::readCached(fel.sha256, mb) &&
                  mb.getSize() == 610090 && juce::approximatelyEqual(lastP, 1.0),
              "the good package is downloaded, checked, cached (" + s(f3.getFileName()) + ") and found by SHA-256");
        juce::String bad = hub::verify(mb.getData(), mb.getSize() - 1, fel.sha256, 0);
        check(bad.contains("SHA-256"), "verify: one byte short is another SHA-256 (" + s(bad) + ")");
    }
    {
        juce::SharedResourcePointer<hub::Service> svc;
        svc->refresh(true);
        const bool got = pump([&] { return !svc->refreshing() && svc->catalog().origin == "live"; }, 20);
        check(got && svc->catalog().firmwares.size() == 3 && svc->lastError().isEmpty() && hub::cachedCatalogFile().existsAsFile(),
              "the Service fetches the catalog on its worker (live, cached as hub/catalog.json)");
        hub::Service::Job last;
        bool called = false;
        juce::String cbErr;
        svc->fetch(*cat.find("badsha"), [&](juce::File f, juce::String e) { called = f == juce::File(), cbErr = e; });
        check(pump([&] { return called; }, 20), "Service::fetch reports a refused package on the message thread: " + s(cbErr));
        const auto j = svc->job(cat.find("badsha")->sha256);
        check(j && j->failed && !j->running, "  ... and its job says failed");
    }

    // ---- (p) the processor
    juce::MemoryBlock state;
    std::vector<uint8_t> nor1;
    juce::String workFile;
    {
        FM1Processor p;
        Host h(p);
        bool engineListed = false;
        for (const CoreInfo &c : p.firmwareCores())
            engineListed = engineListed || c.id == "fm1emu";
        bool inAll = false;
        for (const CoreInfo &c : p.listCores())
            inAll = inAll || c.id == "fm1emu";
        check(inAll && !engineListed, "the fm1emu module is bundled (listCores) but not offered as a firmware (firmwareCores)");
        juce::String why;
        check(p.selectEmuFirmwareNow(fel, &why) && p.isEmulated() && p.getEmuFirmware().id == "felucca",
              "a Hub firmware picked: Felucca 1.6.1 on the fm1emu core" + s(why));
        h.run_ms(3000);
        std::string inf = info(p);
        check(lcdDrawn(p) && !p.getEmuStatus().waiting && inf.find("running") != std::string::npos &&
                  inf.find("clock 360 MHz") != std::string::npos,
              "it boots, draws, at the default clock (" + inf.substr(0, 120) + ")");
        check(p.getLatencySamples() > (int)Device::latency_frames(48000), "the latency includes the core's FIFO: " +
                                                                              std::to_string(p.getLatencySamples()) + " samples");
        // ---- (k) the clock: the cycles per device ms
        auto rate = [&](int choice) {
            p.setEmuClockChoice(choice);
            h.run_ms(2000);
            const std::string a = info(p);
            h.run_ms(2000);
            const std::string b = info(p);
            const double per = (double)(field(b, "cycles") - field(a, "cycles")) / (double)(field(b, "ms") - field(a, "ms"));
            check(b.find("clock " + std::to_string(choice) + " MHz") != std::string::npos && std::abs(per / (choice * 1000.0) - 1) < 0.02,
                  "clock " + std::to_string(choice) + " MHz: " + std::to_string((long long)per) + " cycles per device ms (" +
                      std::to_string(field(b, "ms") - field(a, "ms")) + " ms measured)");
        };
        rate(240);
        rate(480);
        check(p.isEmulated() && lcdDrawn(p), "  ... the firmware came back after each clock change (a reboot, its flash kept)");
        workFile = p.workingFlashFile().getFullPathName();
        check(workFile.endsWith("fm1emu/felucca/1.6.1-dff3a38f/flash.bin"), "the working flash per package version: " + s(workFile));
        nor1 = p.currentFlash();
        check(nor1.size() == FM1Processor::kNor, "currentFlash() is the NOR only: " + std::to_string(nor1.size()) + " bytes");
        // ---- (s) the state
        p.getStateInformation(state);
        const juce::ValueTree t = juce::ValueTree::readFromData(state.getData(), state.getSize());
        const juce::ValueTree e = t.getChildWithName("emu");
        check(t.getProperty("core").toString() == "fm1emu" && e.isValid() && e.getProperty("id").toString() == "felucca" &&
                  e.getProperty("sha256").toString() == fel.sha256 && e.getProperty("pkg").toString() == fel.pkg &&
                  t.getProperty("emuClock").toString() == "480",
              "the state: core fm1emu, the firmware by id and SHA-256 and URL, the clock 480");
        check(state.getSize() < 700000, "the state holds the NOR, not the package: " + std::to_string(state.getSize()) + " bytes");
        // the switch back to a native core keeps the emulated firmware's flash in its own file
        p.switchCore("felucca");
        check(!p.isEmulated() && p.getCoreInfo().info.id == "felucca" && juce::File(workFile).existsAsFile(),
              "back to the native felucca: the emulated one's flash saved to its working file");
    }
    {   // the governor on fake timing (cpu.js's rules)
        using namespace emuclock;
        Governor g(kAuto, 360, 0);
        Governor::Verdict v;
        for (double t = 0; t <= 3500; t += 500)
            v = g.feed(0.95, 0, t);
        check(!v.step && !v.warn, "governor: the first 4 s don't count");
        for (double t = 4000; t <= 5500; t += 500)
            v = g.feed(0.95, 0, t);
        check(!v.step, "  ... high load under 2 s: nothing yet");
        v = g.feed(0.95, 0, 6000);
        check(v.step == 320 && g.done(), "  ... above 75 % for 2 s: Auto steps 360 -> 320");
        Governor l(kAuto, 240, 0);
        l.feed(0.2, 100, 4500);
        v = l.feed(0.2, 160, 5000);
        check(v.step == 180, "  ... 260 ms late within 5 s: Auto steps 240 -> 180");
        Governor f(kAuto, 120, 0), m(480, 480, 0);
        f.feed(0.9, 0, 4000);
        m.feed(0.9, 0, 4000);
        check(f.feed(0.9, 0, 6000).warn && m.feed(0.9, 0, 6000).warn, "  ... Auto at its floor and a manual pick only warn");
        check(mhzFor(kAuto, 0) == 360 && mhzFor(kAuto, 240) == 240 && mhzFor(640, 120) == 640 && nextStep(120) == 0 && validChoice(320) &&
                  !validChoice(300),
              "  ... the choices: Auto (360, its steps), 240 320 360 480 640");
    }

    // ---- (s) the state restored: the cache, the Hub unreachable
    useHub(scratch.getChildFile("nowhere"));
    {
        FM1Processor p;
        Host h(p);
        p.setStateInformation(state.getData(), (int)state.getSize());
        const std::vector<uint8_t> nor2 = p.currentFlash();
        h.run_ms(1500);
        check(p.isEmulated() && !p.getEmuStatus().waiting && p.getEmuClockChoice() == 480 && info(p).find("clock 480 MHz") != std::string::npos &&
                  lcdDrawn(p),
              "a second instance restores it offline from the cache: Felucca at 480 MHz, drawing");
        check(nor2 == nor1, "  ... with the state's NOR exactly");
    }
    // not cached: the core waits, the NOR is kept, the package comes from the state's URL
    useHub(hubRoot);
    hub::packagesDir().deleteRecursively();
    {
        FM1Processor p;
        Host h(p);
        p.setStateInformation(state.getData(), (int)state.getSize());
        const bool waiting = p.getEmuStatus().waiting;
        check(waiting && p.isEmulated() && p.currentFlash() == nor1, "the package not cached: the core waits empty, the state's NOR held");
        check(pump([&] { return !p.getEmuStatus().waiting; }, 30), "  ... the package is fetched from the state's URL and the firmware boots");
        h.run_ms(1500);
        check(p.currentFlash().size() == FM1Processor::kNor && lcdDrawn(p) && hub::packageFile("felucca", "1.6.1", fel.sha256).existsAsFile(),
              "  ... drawing, and cached again");
    }
    // not cached and the Hub gone: an error, the NOR still kept by the state
    hub::packagesDir().deleteRecursively();
    served.moveFileTo(hubRoot.getChildFile("fw/moved.fwsc"));
    {
        FM1Processor p;
        Host h(p);
        p.setStateInformation(state.getData(), (int)state.getSize());
        check(pump([&] { return p.getEmuStatus().error; }, 30), "offline and not cached: the download fails: " + s(p.getEmuStatus().text));
        h.run_ms(500);
        juce::MemoryBlock st2;
        p.getStateInformation(st2);
        const juce::ValueTree t2 = juce::ValueTree::readFromData(st2.getData(), st2.getSize());
        std::vector<uint8_t> nor3;
        if (const juce::MemoryBlock *z = t2.getProperty("flash").getBinaryData()) {
            juce::MemoryInputStream mi(*z, false);
            juce::GZIPDecompressorInputStream gz(&mi, false, juce::GZIPDecompressorInputStream::gzipFormat);
            juce::MemoryBlock mb;
            gz.readIntoMemoryBlock(mb);
            nor3.assign((const uint8_t *)mb.getData(), (const uint8_t *)mb.getData() + mb.getSize());
        }
        check(p.getEmuStatus().waiting && nor3 == nor1 && t2.getChildWithName("emu").getProperty("sha256").toString() == fel.sha256,
              "  ... the core still waits, and the state it saves keeps the firmware and its NOR unchanged");
    }
    hubRoot.getChildFile("fw/moved.fwsc").moveFileTo(served);

    // ---- (k) Auto through the processor: a step down reboots at 320
    {
        FM1Processor p;
        Host h(p);
        juce::String why;
        p.selectEmuFirmwareNow(fel, &why);
        p.setEmuClockChoice(emuclock::kAuto);
        h.run_ms(500);
        for (double t = 0; t <= 6000; t += 500)
            p.emuGovernorFeed(0.95, 0, t);
        h.run_ms(1000);
        check(p.getEmuMhz() == 320 && info(p).find("clock 320 MHz") != std::string::npos && p.getEmuClockNote().contains("320"),
              "Auto through the processor: too slow at 360 for 2 s, rebooted at 320 (" + s(p.getEmuClockNote()) + ")");
        juce::MemoryBlock st;
        p.getStateInformation(st);
        const juce::ValueTree t = juce::ValueTree::readFromData(st.getData(), st.getSize());
        check(t.getProperty("emuClock").toString() == "auto" && (int)t.getProperty("emuAutoMhz") == 320, "  ... the state keeps Auto at 320");
        p.setEmuClockChoice(emuclock::kAuto);
        check(p.getEmuMhz() == 360, "  ... picking Auto again starts at 360");

        // the transport plays: the step waits, the clock row says so, and comes when the transport stops
        FakePlayHead ph;
        ph.playing = true;
        p.setPlayHead(&ph);
        h.run_ms(500);
        const std::string before = info(p);
        for (double t = 0; t <= 6000; t += 500)
            p.emuGovernorFeed(0.95, 0, t);
        h.run_ms(1000);
        p.emuTimerForTest();
        const std::string during = info(p);
        check(p.getEmuDeferredStep() == 320 && p.getEmuMhz() == 360 && during.find("clock 360 MHz") != std::string::npos &&
                  field(during, "ms") > field(before, "ms") && p.getEmuClockNote() == "Auto: will step down to 320 MHz when stopped",
              "the transport playing: no reboot (" + std::to_string(field(during, "ms")) + " device ms, still 360 MHz), \"" +
                  s(p.getEmuClockNote()) + "\"");
        ph.playing = false;
        h.run_ms(100);
        p.emuTimerForTest();
        h.run_ms(500);
        check(p.getEmuDeferredStep() == 0 && p.getEmuMhz() == 320 && info(p).find("clock 320 MHz") != std::string::npos &&
                  field(info(p), "ms") < field(during, "ms"),
              "  ... the transport stopped: rebooted at 320 (" + s(p.getEmuClockNote()) + ")");
        p.setPlayHead(nullptr);
    }

    std::printf("hub_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
