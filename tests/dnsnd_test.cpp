// SPDX-License-Identifier: GPL-3.0-only
// The .dnsnd reader (plugin/Dnsnd.h), and FIZZ's import of it through the plugin's processor (FIZZ P1.5s).
//
//   dnsnd_test DIR              every DIR/*.dnsnd: one line each, "file<TAB>name<TAB>drive<TAB>pan<TAB>reverb<TAB>delay
//                               <TAB>chorus<TAB>the blob in hex" (or "file<TAB>ERROR<TAB>why"), for tools/dnsnd_compare.mjs,
//                               which checks them against ChoralRoot's JS reader byte for byte
//   dnsnd_test --dn2 DIR...     P1.5x: every DIR/*.dn2pst the same way, with FIZZ's tags (a number) after the blob
//                               (E1: then the version-6 blob in hex and 1 if the sound needs it),
//                               for tools/dn2pst_compare.mjs (against FMToneSeq tools/dn2pst.mjs)
//   dnsnd_test --frame FILE     P1.5x: FILE's STORE frame in hex (its pan, sends, mixmask, tags byte)
//   dnsnd_test --import DIR     the processor on the FIZZ core (headless, as plugin_test): the folder's first presets
//                               imported (importSounds), the blocks run, every reply OK, the library holding them
//                               (the core's ui_info is not enough: the replies counted by the processor)
// The presets are the owner's, read where they are (never copied into the tree).
#include <cstdio>
#include <cstdlib>

#include "Dnsnd.h"
#include "Processor.h"

static int fails = 0;
static void check(bool c, const juce::String &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.toRawUTF8());
    fails += !c;
}

static juce::Array<juce::File> presets(const juce::File &dir)
{
    juce::Array<juce::File> fs = dir.findChildFiles(juce::File::findFiles, false, "*.dnsnd");
    fs.sort();
    return fs;
}

static int dump(const juce::File &dir)
{
    for (const juce::File &f : presets(dir)) {
        fm1dnsnd::Sound s;
        juce::String err;
        if (!fm1dnsnd::read(f, s, err)) {
            std::printf("%s\tERROR\t%s\n", f.getFileName().toRawUTF8(), err.toRawUTF8());
            continue;
        }
        juce::String hex;
        for (uint8_t b : s.blob)
            hex += juce::String::toHexString((int)b).paddedLeft('0', 2);
        std::printf("%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%d\n", f.getFileName().toRawUTF8(), s.name.toRawUTF8(), s.drive,
                    s.pan, s.reverb, s.delay, s.chorus, hex.toRawUTF8(), (int)s.six, (int)s.blob6[88 + 79 - 74]);
                    // (P1.5z: STORE6, and DRPRE in the version-6 blob: both 1 when DRIVE is above 0)
    }
    return 0;
}

static int dumpDn2(int argc, char **argv)
{
    for (int a = 2; a < argc; a++) {
        juce::Array<juce::File> fs = juce::File(argv[a]).findChildFiles(juce::File::findFiles, false, "*.dn2pst");
        fs.sort();
        for (const juce::File &f : fs) {
            fm1dnsnd::Sound s;
            juce::String err;
            if (!fm1dnsnd::read(f, s, err)) {
                std::printf("%s\tERROR\t%s\n", f.getFileName().toRawUTF8(), err.toRawUTF8());
                continue;
            }
            juce::String hex;
            for (uint8_t b : s.blob)
                hex += juce::String::toHexString((int)b).paddedLeft('0', 2);
            juce::String hex6;                  // (E1: the version-6 blob, and whether it is needed)
            for (uint8_t b : s.blob6)
                hex6 += juce::String::toHexString((int)b).paddedLeft('0', 2);
            std::printf("%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%s\t%d\n", f.getFileName().toRawUTF8(), s.name.toRawUTF8(),
                        s.drive, s.pan, s.reverb, s.delay, s.chorus, hex.toRawUTF8(), s.tags, hex6.toRawUTF8(), (int)s.six);
        }
    }
    return 0;
}

static int frame(const char *path)
{
    fm1dnsnd::Sound s;
    juce::String err;
    if (!fm1dnsnd::read(juce::File(path), s, err)) {
        std::printf("ERROR\t%s\n", err.toRawUTF8());
        return 1;
    }
    juce::String hex;
    for (uint8_t b : fm1dnsnd::storeFrame(s, false))
        hex += juce::String::toHexString((int)b).paddedLeft('0', 2);
    std::printf("%s\n", hex.toRawUTF8());
    return 0;
}

static int import(const juce::File &dir)
{
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    FM1Processor p;
    check(p.switchCore("fizz"), "the FIZZ core loaded");
    const double sr = 44100;
    const int bs = 256;
    p.setRateAndBufferSizeDetails(sr, bs);
    p.prepareToPlay(sr, bs);
    juce::AudioBuffer<float> buf(2, bs);
    auto run = [&](double ms) {
        for (double t = 0; t < ms; t += 1000.0 * bs / sr) {
            juce::MidiBuffer m;
            buf.clear();
            p.processBlock(buf, m);
        }
    };
    run(500);
    check(p.canImportSounds(), "FIZZ: .dnsnd import offered");
    juce::Array<juce::File> fs = presets(dir);
    fs.removeRange(5, fs.size());
    juce::StringArray errors;
    const int queued = p.importSounds(fs, errors);
    check(queued == fs.size() && errors.isEmpty(), "five presets queued (" + juce::String(queued) + ")");
    run(2000);
    const FM1Processor::ImportStatus st = p.importStatus();
    check(st.ok == queued && st.pending == 0 && st.full == 0 && st.failed == 0,
          "all stored (OK " + juce::String(st.ok) + ", FULL " + juce::String(st.full) + ", failed " + juce::String(st.failed) +
              ", pending " + juce::String(st.pending) + ")");
    char info[512];
    p.deviceForTest()->core()->ui_info(info, sizeof info);
    std::printf("  (%s)\n", info);
    juce::String other;                         // another core, if this build has one: no import there
    for (const CoreInfo &c : p.listCores())
        if (c.id != "fizz" && c.id != "fm1emu" && other.isEmpty())
            other = juce::String(c.id);
    if (other.isNotEmpty()) {
        check(p.switchCore(other), "another core (" + other + ")");
        check(!p.canImportSounds(), "not FIZZ: no import");
    } else {
        std::printf("  (no other core in this build: the gate checked by canImportSounds' id test only)\n");
    }
    std::printf("dnsnd_test --import: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2)
        return dump(juce::File(juce::String(argv[1])));
    if (argc == 3 && juce::String(argv[1]) == "--import")
        return import(juce::File(juce::String(argv[2])));
    if (argc >= 3 && juce::String(argv[1]) == "--dn2")
        return dumpDn2(argc, argv);
    if (argc == 3 && juce::String(argv[1]) == "--frame")
        return frame(argv[2]);
    std::fprintf(stderr, "usage: dnsnd_test DIR | --dn2 DIR... | --frame FILE | --import DIR\n");
    return 2;
}
