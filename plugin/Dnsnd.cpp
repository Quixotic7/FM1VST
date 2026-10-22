// SPDX-License-Identifier: GPL-3.0-only
// Digitone .dnsnd -> FM TONE (Dnsnd.h): ChoralRootFM1 web/fm1dnsnd.js, line for line where it decides a value. The JS
// works in doubles (a parameter is the decoded value rounded to 2 decimals, then each field's rule); this port takes
// the same doubles (a value in hundredths K is K / 100.0, the nearest double to the JS's parsed toFixed(2)) and the
// same arithmetic, with Math.round's rule (jsRound), so the blobs are the JS's byte for byte.
#include "Dnsnd.h"

#include <cmath>
#include <map>

namespace fm1dnsnd {
namespace {

// ---- FM TONE's patch (eng_quad.c QP_*: [min, max, def], version 5)
const int RANGES[kNp][3] = {{1,8,1},{0,18,3},{0,63,3},{0,18,3},{-2600,2600,0},{0,12700,0},{0,127,0},{-64,63,-64},
    {-100,100,0},{-100,100,0},{-100,100,0},{-100,100,0},{0,127,0},{0,127,32},{0,127,127},{0,127,0},{0,127,0},{0,127,32},
    {0,127,127},{0,127,0},{0,127,0},{0,1,1},{0,1,1},{0,4,1},{0,127,0},{0,1,1},{0,1,1},{0,127,64},{0,127,0},{0,127,0},
    {0,127,0},{0,127,64},{0,127,0},{0,127,64},{0,12700,12700},{0,12700,0},{0,3,1},{-6400,6399,0},{0,127,0},{0,127,0},
    {0,12700,0},{0,127,127},{0,127,8},{0,127,32},{0,127,96},{0,127,24},{0,127,110},{-6400,6399,3200},{0,23,5},
    {-64,63,0},{0,57,0},{0,6,0},{0,127,0},{0,4,0},{-6400,6399,0},{-6400,6399,3200},{0,23,5},{-64,63,0},{0,57,0},
    {0,6,0},{0,127,0},{0,4,0},{-6400,6399,0},{-6400,6399,3200},{0,23,5},{-64,63,0},{0,57,0},{0,6,0},{0,127,0},{0,4,0},
    {-6400,6399,0},{0,18,3},{0,127,0}};
enum { ALGO = 0, RC = 1, RA = 2, RB1 = 3, HARM = 4, DTUN = 5, FDBK = 6, MIX = 7, OFSC = 8, OFSA = 9, OFSB1 = 10,
       OFSB2 = 11, AATK = 12, ADEC = 13, AEND = 14, ALEV = 15, BATK = 16, BDEC = 17, BEND = 18, BLEV = 19, ADLY = 20,
       ATRIG = 21, ARST = 22, PHRT = 23, BDLY = 24, BTRIG = 25, BRST = 26, FATK = 30, FDEC = 31, FSUS = 32, FREL = 33,
       FREQ = 34, RESO = 35, FTYPE = 36, FDEPTH = 37, FDLY = 38, BASE = 40, WIDTH = 41, EATK = 42, EDEC = 43, ESUS = 44,
       EREL = 45, LEVEL = 46, RB2 = 71 };
enum { L_SPEED = 0, L_MULT = 1, L_FADE = 2, L_DEST = 3, L_WAVE = 4, L_PHASE = 5, L_TRIG = 6, L_DEPTH = 7 };
int qlfo(int k, int f) { return 47 + k * 8 + f; }
const int HIRES[] = {4, 5, 34, 35, 37, 40, 47, 54, 55, 62, 63, 70};
bool hires(int i)
{
    for (int h : HIRES)
        if (h == i)
            return true;
    return false;
}

// ---- the Digitone's tables
const double RATIO_CB[19] = {0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
double jsRound(double x)                        // Math.round: half up (toward +infinity)
{
    const double f = std::floor(x);
    return x - f >= 0.5 ? f + 1.0 : f;
}
double ratioA(int i)                            // DN_RATIO_A[i]
{
    const double x = i <= 15 ? (double)(i + 1) / 4 : i <= 25 ? 4 + (double)(i - 15) * 0.4 : 8 + (double)(i - 25) * 0.8;
    return jsRound(x * 100) / 100;
}
const int RATIO_A_ANCHORS[] = {0, 3, 9, 15, 25, 35};
const int WAVE_TO_QUAD[7] = {0, 1, 2, 3, 5, 4, 6};
const int FTYPE_V1_TO_QUAD[5] = {0, 1, 2, 3, 3};
const std::map<int, int> &lfoDest()
{
    static const std::map<int, int> m = {
        {0, 0}, {1, 40}, {3, 41}, {5, 42}, {9, 43}, {11, 44}, {13, 45}, {15, 46}, {17, 3}, {18, 4}, {19, 5}, {20, 6},
        {21, 11}, {22, 12}, {23, 13}, {24, 14}, {27, 17}, {28, 18}, {29, 19}, {30, 15}, {31, 20}, {32, 21}, {33, 22},
        {34, 16}, {35, 23}, {38, 24}, {42, 1}, {43, 2}, {44, 47}, {45, 48}, {46, 49}, {47, 50}, {48, 51}, {49, 52},
        {51, 25}, {52, 26}, {53, 27}, {54, 30}, {55, 31}, {56, 32}, {57, 33}, {58, 28}, {59, 29}, {60, 34}, {61, 35},
        {62, 36}, {63, 37}, {64, 53}, {65, 39}, {66, 38}, {67, 54}, {68, 55}, {69, 56}, {73, 7}, {74, 8}, {75, 9},
        {76, 10}, {77, 57}};
    return m;
}
constexpr double kOctTol = 0.003;

struct Out {
    std::array<int, kNp> p{};
    int pan = 0, drive = 0, version = 1;
    juce::StringArray notes;
    Out()
    {
        for (int i = 0; i < kNp; i++)
            p[(size_t)i] = RANGES[i][2];
    }
    void set(int i, double v)
    {
        const double c = std::max((double)RANGES[i][0], std::min((double)RANGES[i][1], std::isfinite(v) ? v : RANGES[i][2]));
        p[(size_t)i] = (int)c;
    }
};

// the record's values as the JS's params: lo a byte; hi a coarse and a fine byte in hundredths
struct Rec {
    const uint8_t *d;
    int lo(int o) const { return d[o]; }
    int hiK(int o) const { return d[o] * 100 + (int)jsRound(d[o + 1] * 100 / 256.0); }   // hundredths
    double hi(int o, int minus = 0) const { return (double)(hiK(o) - minus * 100) / 100.0; }
};
double dnT(double v) { return std::max(0.0, std::min(127.0, jsRound(v))); }
double timeQ(double v) { return std::max(0.0, std::min(127.0, v)); }

void apply(const Rec &r, int version, Out &o, int &reverb, int &delay, int &chorus, double &octave)
{
    o.version = version;
    for (int k = 0; k < 2; k++) {               // the LFOs
        o.set(qlfo(k, L_SPEED), jsRound(r.hi(30 + 2 * k, 64) * 100));
        o.set(qlfo(k, L_MULT), jsRound(r.lo(34 + 2 * k)));
        o.set(qlfo(k, L_FADE), jsRound(r.lo(38 + 2 * k) - 64));
        {
            const int v = r.lo(42 + 2 * k);
            const auto it = lfoDest().find(v);
            o.set(qlfo(k, L_DEST), it == lfoDest().end() ? 0 : it->second);
            if (it == lfoDest().end())
                o.notes.add("LFO " + juce::String(k + 1) + " destination #" + juce::String(v) + ": FM TONE has none, off");
        }
        {
            const int v = r.lo(46 + 2 * k);
            o.set(qlfo(k, L_WAVE), v >= 0 && v < 7 ? WAVE_TO_QUAD[v] : 0);
        }
        o.set(qlfo(k, L_PHASE), jsRound(r.lo(50 + 2 * k)));
        o.set(qlfo(k, L_TRIG), jsRound(r.lo(54 + 2 * k)));
        o.set(qlfo(k, L_DEPTH), jsRound(r.hi(58 + 2 * k, 64) * 100));
    }
    o.set(ALGO, jsRound(r.lo(62) + 1));
    o.set(RC, std::min(18, r.lo(64)));          // (DN_RATIO_CB's index: its values are distinct)
    {
        const int i = std::min(35, r.lo(66));
        const double v = ratioA(i);
        o.set(RA, jsRound(v * 4) - 1);
        bool anchor = false;
        for (int a : RATIO_A_ANCHORS)
            anchor = anchor || a == i;
        if (i > 15 && !anchor)
            o.notes.add("RATIO A step " + juce::String(i) + " read as " + juce::String(v, 2) + " (between the proved steps)");
    }
    {
        const int v = std::min(360, (r.d[68] << 8 | r.d[69]) >> 1);
        o.set(RB1, std::floor(v / 19.0));
        o.set(RB2, v % 19);
    }
    o.set(HARM, jsRound(r.hi(70, 63) * 100));
    o.set(DTUN, jsRound(r.hi(72) * 100));
    o.set(FDBK, jsRound(r.lo(74)));
    o.set(MIX, jsRound(r.lo(76) - 64));
    o.set(PHRT, jsRound(r.lo(78)));
    const int envA[4] = {AATK, ADEC, AEND, ALEV}, envB[4] = {BATK, BDEC, BEND, BLEV};
    for (int i = 0; i < 4; i++) {
        o.set(envA[i], i < 2 ? dnT(r.lo(82 + 2 * i)) : jsRound(r.lo(82 + 2 * i)));
        o.set(envB[i], i < 2 ? dnT(r.lo(90 + 2 * i)) : jsRound(r.lo(90 + 2 * i)));
    }
    o.set(ADLY, jsRound(r.lo(98)));
    o.set(ATRIG, jsRound(r.lo(100)));
    o.set(ARST, jsRound(r.lo(102)));
    o.set(BDLY, jsRound(r.lo(104)));
    o.set(BTRIG, jsRound(r.lo(106)));
    o.set(BRST, jsRound(r.lo(108)));
    {
        const int v = r.lo(128);
        if (version >= 5) {
            o.set(FTYPE, v);
        } else {
            o.set(FTYPE, v >= 0 && v < 5 ? FTYPE_V1_TO_QUAD[v] : 1);
            if (v >= 5)
                o.notes.add("filter type #" + juce::String(v) + " (a version-1 sound: not a known value)");
        }
    }
    o.set(FREQ, jsRound(r.hi(130) * 100));
    o.set(RESO, jsRound(r.hi(132) * 100));
    o.set(FDEPTH, jsRound(r.hi(134, 64) * 100));
    o.set(FATK, dnT(r.lo(136)));
    o.set(FDEC, dnT(r.lo(138)));
    o.set(FSUS, jsRound(r.lo(140)));
    {
        const double v = r.lo(142);
        o.set(FREL, v >= 127 ? 127 : jsRound(timeQ(v)));
    }
    o.set(BASE, jsRound(r.hi(144) * 100));
    o.set(WIDTH, jsRound(r.lo(146)));
    o.set(EATK, dnT(r.lo(148)));
    {
        const double v = r.lo(150);
        o.set(EDEC, v >= 127 ? 127 : dnT(v));
    }
    o.set(ESUS, jsRound(r.lo(152)));
    {
        const double v = r.hi(154);
        o.set(EREL, v >= 127 ? 127 : std::min(126.0, jsRound(timeQ(v))));
    }
    o.drive = (int)std::max(0.0, std::min(127.0, jsRound(r.hi(156))));
    o.pan = std::max(-64, std::min(63, r.lo(158) - 64));
    o.set(LEVEL, jsRound(r.hi(160)));
    auto send = [&](int off, const char *what, int &to) {
        const double v = r.hi(off);
        to = (int)std::max(0.0, std::min(127.0, jsRound(v)));
        if (v > 0)
            o.notes.add(juce::String(what) + " " + juce::String(v, 2) + " (FIZZ: the track's send, when loaded on one)");
    };
    send(162, "reverb send", reverb);
    send(164, "delay send", delay);
    send(166, "chorus send", chorus);
    if (version >= 5 && r.lo(168) == 0)
        o.notes.add("AMP ENV RESET off not imported (FM TONE's amp envelope always resets)");
    if (version >= 5) {
        const int ofs[4][2] = {{174, OFSC}, {176, OFSA}, {178, OFSB1}, {180, OFSB2}};
        for (const auto &f : ofs) {
            const double v = r.hi(f[0], 64);
            o.set(f[1], jsRound(v * 100 / 64));
        }
        o.set(FDLY, jsRound(r.lo(182)));
    }
    octave = (double)(int8_t)r.d[version >= 5 ? 280 : 263];
}

// the sound's OCTAVE into the ratios (fm1dnsnd.js dnOctave): all four or none
bool octaveInto(std::array<int, kNp> &p, int oct)
{
    double RA_Q[64];
    for (int i = 0; i < 64; i++)
        RA_Q[i] = (double)(i + 1) / 4;
    const double f = std::pow(2.0, oct / 12.0);
    struct Op {
        const double *tab;
        int n, qs, qo, step;
    } ops[4] = {{RATIO_CB, 19, RC, OFSC, p[RC]}, {RA_Q, 64, RA, OFSA, p[RA]}, {RATIO_CB, 19, RB1, OFSB1, p[RB1]},
                {RATIO_CB, 19, RB2, OFSB2, p[RB2]}};
    int seti[4], seto[4];
    for (int k = 0; k < 4; k++) {
        const Op &op = ops[k];
        const double want = (op.tab[op.step] + p[(size_t)op.qo] / 100.0) * f;
        bool have = false;
        int bi = 0, bo = 0;
        double be = 0;
        for (int i = 0; i < op.n; i++) {
            const double x = op.tab[i];
            const int o = (int)std::max(-100.0, std::min(100.0, jsRound((want - x) * 100)));
            const double got = x + o / 100.0, e = std::fabs(got - want) / want;
            if (got > 0 && e <= kOctTol && (!have || std::abs(o) < std::abs(bo) || (std::abs(o) == std::abs(bo) && e < be)))
                have = true, bi = i, bo = o, be = e;
        }
        if (!have)
            return false;
        seti[k] = bi, seto[k] = bo;
    }
    for (int k = 0; k < 4; k++)
        p[(size_t)ops[k].qs] = seti[k], p[(size_t)ops[k].qo] = seto[k];
    return true;
}

void pack(const std::array<int, kNp> &p, std::array<uint8_t, kBlob> &blob)
{
    blob.fill(0);
    size_t o = 2;
    blob[0] = 0x51, blob[1] = 5;
    for (int i = 0; i < kNp; i++) {
        const int u = p[(size_t)i] - RANGES[i][0];
        blob[o++] = (uint8_t)(u & 255);
        if (hires(i))
            blob[o++] = (uint8_t)(u >> 8);
    }
}

uint32_t u16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
uint32_t u32(const uint8_t *b) { return u16(b) | u16(b + 2) << 16; }
const char *const kNot = "not a Digitone sound file";

bool lz4Block(const uint8_t *src, size_t n, size_t p, size_t want, std::vector<uint8_t> &out, juce::String &err)
{
    out.assign(want, 0);
    size_t k = 0;
    auto byte = [&](int &b) {
        if (p >= n)
            return false;
        b = src[p++];
        return true;
    };
    while (k < want) {
        int t, b;
        if (!byte(t))
            return err = juce::String(kNot) + " (the sound data is cut short)", false;
        size_t l = (size_t)(t >> 4);
        if (l == 15)
            do {
                if (!byte(b))
                    return err = juce::String(kNot) + " (the sound data is cut short)", false;
                l += (size_t)b;
            } while (b == 255);
        if (k + l > want)
            return err = juce::String(kNot) + " (the sound data does not decompress)", false;
        for (size_t i = 0; i < l; i++) {
            if (!byte(b))
                return err = juce::String(kNot) + " (the sound data is cut short)", false;
            out[k++] = (uint8_t)b;
        }
        if (k >= want)
            break;
        int lo, hi;
        if (!byte(lo) || !byte(hi))
            return err = juce::String(kNot) + " (the sound data is cut short)", false;
        const size_t off = (size_t)(lo | hi << 8);
        size_t m = (size_t)(t & 15) + 4;
        if ((t & 15) == 15)
            do {
                if (!byte(b))
                    return err = juce::String(kNot) + " (the sound data is cut short)", false;
                m += (size_t)b;
            } while (b == 255);
        if (!off || off > k || k + m > want)
            return err = juce::String(kNot) + " (the sound data does not decompress)", false;
        for (size_t i = 0; i < m; i++, k++)
            out[k] = out[k - off];
    }
    return true;
}

}   // namespace

bool parse(const void *data, size_t n, Sound &s, juce::String &err)
{
    const uint8_t *b = (const uint8_t *)data;
    s = Sound();
    if (n < 4 || u32(b) != 0x04034b50u)
        return err = juce::String(kNot) + " (not a ZIP archive)", false;
    juce::MemoryInputStream in(data, n, false);
    juce::ZipFile zip(in);
    auto entry = [&](const juce::String &name, juce::MemoryBlock &mb) {
        const int i = zip.getIndexOfFileName(name);
        if (i < 0)
            return false;
        std::unique_ptr<juce::InputStream> st(zip.createStreamForEntry(i));
        if (!st)
            return false;
        st->readIntoMemoryBlock(mb);
        return true;
    };
    juce::MemoryBlock mt, pl;
    if (!entry("manifest.json", mt))
        return err = juce::String(kNot) + " (no manifest.json)", false;
    const juce::var man = juce::JSON::parse(mt.toString());
    if (!man.isObject())
        return err = juce::String(kNot) + " (manifest.json is not JSON)", false;
    if (man["FileType"].toString() != "Sound" || !man["FileType"].isString())
        return err = juce::String(kNot) + " (FileType is not \"Sound\")", false;
    if (!man["Payload"].isString() || !entry(man["Payload"].toString(), pl))
        return err = juce::String(kNot) + " (the payload is missing)", false;
    const uint8_t *P = (const uint8_t *)pl.getData();
    const size_t pn = pl.getSize();
    if (pn < 0x23 + 14 || u32(P) != 0x03d311acu)
        return err = juce::String(kNot) + " (the payload's magic)", false;
    const uint32_t size = u16(P + 0x1c);
    if (size != 302)
        return err = "a newer Digitone format (a " + juce::String(size) + "-byte sound, not 302): not supported", false;
    std::vector<uint8_t> d;
    if (!lz4Block(P, pn, 0x23, size, d, err))
        return false;
    if (!(d[0] == 0xbe && d[1] == 0xef && d[2] == 0xba && d[3] == 0xce))
        return err = juce::String(kNot) + " (the sound's magic)", false;
    const uint32_t version = (uint32_t)d[4] << 24 | (uint32_t)d[5] << 16 | (uint32_t)d[6] << 8 | d[7];
    if (version != 1 && version != 5)
        return err = "a newer Digitone format (version " + juce::String(version) + "): not supported", false;
    {   // the name (parseDnsnd, then soundFromDnsnd's syxName: printable, trimmed, 12 letters; none: DIGITONE)
        juce::String nm;
        for (int i = 12; i < 28 && d[(size_t)i]; i++)
            nm += (juce::juce_wchar)(d[(size_t)i] < 32 || d[(size_t)i] > 126 ? ' ' : d[(size_t)i]);
        nm = nm.trim().substring(0, 12).trimEnd();
        s.name = nm.isEmpty() ? juce::String("DIGITONE") : nm;
    }
    s.version = (int)version;
    Out o;
    double octave = 0;
    apply(Rec{d.data()}, (int)version, o, s.reverb, s.delay, s.chorus, octave);
    if (d[138] == 127) {                        // the filter's DEC INF: its SUS 127
        o.p[FSUS] = 127;
        o.notes.add("filter DEC INF: FM TONE's filter SUS set to 127");
    }
    const int oct = (int)octave;
    if (oct) {
        const bool r = octaveInto(o.p, oct);
        o.notes.add(r ? "the sound's OCTAVE " + juce::String(oct / 12) + ": every ratio scaled"
                      : "the sound's OCTAVE " + juce::String(oct / 12) + " not imported (a ratio has no such value)");
    }
    s.values = o.p;
    pack(o.p, s.blob);
    s.drive = o.drive;
    s.pan = o.pan;
    s.notes = o.notes;
    return true;
}

bool read(const juce::File &f, Sound &s, juce::String &err)
{
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb))
        return err = "cannot read " + f.getFileName(), false;
    return parse(mb.getData(), mb.getSize(), s, err);
}

std::vector<uint8_t> storeFrame(const Sound &s, bool batch)
{
    std::vector<uint8_t> f = {0xF0, 0x7D, 0x46, 0x5A, 0x01, 0x10};
    const size_t from = 5;
    f.push_back((uint8_t)((kImportIntoPool ? 0x01 : 0x00) | (batch ? 0x02 : 0x00)));   // flags: POOL / LIBRARY, BATCH
    f.push_back(0), f.push_back(0);              // slot 0: the first free
    for (int i = 0; i < 12; i++) {
        const juce::juce_wchar c = i < s.name.length() ? s.name[i] : ' ';
        f.push_back((uint8_t)(c >= 0x20 && c <= 0x7E ? c : '?'));
    }
    f.push_back((uint8_t)std::max(0, std::min(127, s.drive)));
    f.push_back(0), f.push_back(64), f.push_back(0), f.push_back(0), f.push_back(0), f.push_back(0);   // (no LOAD)
    for (int i = 0; i < kBlob; i += 7) {         // 7-bit: groups of 7 as 8, the first their top bits
        const int k = std::min(7, kBlob - i);
        uint8_t hi = 0;
        for (int j = 0; j < k; j++)
            hi |= (uint8_t)(((s.blob[(size_t)(i + j)] >> 7) & 1) << j);
        f.push_back(hi);
        for (int j = 0; j < k; j++)
            f.push_back(s.blob[(size_t)(i + j)] & 0x7F);
    }
    uint32_t sum = 0;
    for (size_t i = from; i < f.size(); i++)
        sum += f[i];
    f.push_back((uint8_t)((0x80u - (sum & 0x7Fu)) & 0x7Fu));
    f.push_back(0xF7);
    return f;
}

}   // namespace fm1dnsnd
