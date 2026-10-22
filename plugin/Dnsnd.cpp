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
    {0,127,0},{0,127,64},{0,127,0},{0,127,64},{0,12700,12700},{0,12700,0},{0,7,1},{-6400,6399,0},{0,127,0},{0,127,0},
    {0,12700,0},{0,127,127},{0,127,8},{0,127,32},{0,127,96},{0,127,24},{0,127,110},{-6400,6399,3200},{0,23,5},
    {-64,63,0},{0,63,0},{0,6,0},{0,127,0},{0,4,0},{-6400,6399,0},{-6400,6399,3200},{0,23,5},{-64,63,0},{0,63,0},
    {0,6,0},{0,127,0},{0,4,0},{-6400,6399,0},{-6400,6399,3200},{0,23,5},{-64,63,0},{0,63,0},{0,6,0},{0,127,0},{0,4,0},
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
    auto send = [&](int off, int &to) {          // (P1.5x: kept with the preset: no note)
        to = (int)std::max(0.0, std::min(127.0, jsRound(r.hi(off))));
    };
    send(162, reverb);
    send(164, delay);
    send(166, chorus);
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

// E1: FIZZ's values 74..109 (FMToneSeq core/voice/quad/quad_voice.c qe_val; tools/dn2pst.mjs QUAD_EXT_RANGES)
const int EXT_RANGES[kExt][3] = {{0,1,0},{0,127,127},{0,2,0},{0,127,32},{0,1,0},{0,1,0},{0,127,0},{-64,63,0},{0,1,0},
    {0,127,0},{0,127,0},{0,1,1},
    {0,8,0},{0,63,0},{-127,127,0},{0,8,0},{0,63,0},{-127,127,0},{0,8,0},{0,63,0},{-127,127,0},{0,8,0},{0,63,0},{-127,127,0},
    {0,8,0},{0,63,0},{-127,127,0},{0,8,0},{0,63,0},{-127,127,0},{0,8,0},{0,63,0},{-127,127,0},{0,8,0},{0,63,0},{-127,127,0}};
enum { X_AMOD = 74, X_HOLD = 75, X_PORT = 76, X_PTIM = 77, X_DRPRE = 79, X_FSHAPE = 80, X_FGAIN = 81, X_BWPOST = 82,
       X_BITS = 83, X_SRR = 84, X_CRPOST = 85, X_MOD0 = 86 };
void extDefaults(std::array<int, kExt> &x)
{
    for (int k = 0; k < kExt; k++)
        x[(size_t)k] = EXT_RANGES[k][2];
}
void extSet(std::array<int, kExt> &x, int id, int v)
{
    const int *r = EXT_RANGES[id - kExt0];
    x[(size_t)(id - kExt0)] = std::max(r[0], std::min(r[1], v));
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

// E1: version 6 (dn2pst.mjs quadPack6): version 5's bytes, 'Q' 6, DRIVE at 87, ext a byte each (value - min) at 88..
void pack6(const std::array<uint8_t, kBlob> &b5, int drive, const std::array<int, kExt> &x, std::array<uint8_t, kBlob6> &b6)
{
    b6.fill(0);
    std::copy(b5.begin(), b5.begin() + 87, b6.begin());
    b6[1] = 6;
    b6[87] = (uint8_t)std::max(0, std::min(127, drive));
    for (int k = 0; k < kExt; k++)
        b6[(size_t)(88 + k)] = (uint8_t)((x[(size_t)k] - EXT_RANGES[k][0]) & 255);
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
    extDefaults(s.ext);                          // (E1: a .dnsnd: FIZZ's values 74.. at their defaults, STORE)
    s.six = false;
    if (s.drive > 0) {                           // P1.5z (the owner): the Digitone drives before its filter: DRIVE PRE,
        extSet(s.ext, X_DRPRE, 1);               // STORE6; DRIVE 0 stays STORE
        s.six = true;
    }
    pack6(s.blob, s.drive, s.ext, s.blob6);
    return true;
}

// ================================================================ FIZZ P1.5x: Digitone II .dn2pst (Dnsnd.h parseDn2)
// FMToneSeq tools/dn2pst.mjs (parseDn2pst + dn2pstToQuad), line for line where it decides a value; the format is
// FMToneSeq research/DN2-FORMAT.md. The same doubles and Math.round as the .dnsnd port above, so the blobs are the JS's
// byte for byte (tests/dnsnd_test --dn2, tools/dn2pst_compare.mjs).
namespace {

const char *const kNot2 = "not a Digitone II preset";
const char *const DN2_MACHINES[5] = {"FM TONE", "WAVETONE", "FM DRUM", "SWARMER", "MIDI"};
const char *const DN2_FILTERS[6] = {"MULTI-MODE", "LOWPASS 4", "EQUALIZER", "COMB-", "LEGACY LP/HP", "COMB+"};
const char *const DN2_TAGS[32] = {"KICK", "SNARE", "DEEP", "BRASS", "STRINGS", "PERCUSSION", "HI-HAT", "CYMBAL",
    "EVOLVING", "EXPRESSIVE", "BASS", "LEAD", "PAD", "TEXTURE", "CHORD", "SOUND FX", "ARPEGGIO", "METALLIC", "ACOUSTIC",
    "ATMOSPHERIC", "NOISY", "GLITCH", "HARD", "SOFT", "DARK", "BRIGHT", "VINTAGE", "EPIC", "FAIL", "LOOP", "MINE",
    "FAVOURITE"};
// a Digitone tag -> FIZZ's tag bit (BASS KEYS PLUCK PAD LEAD DRUMS FX: core/seq/sounds.h QD_PK_*), -1 none
int dn2TagToFizz(int t)
{
    switch (t) {
    case 10: return 0;                          // BASS
    case 14: return 1;                          // CHORD -> KEYS
    case 12: return 3;                          // PAD
    case 11: return 4;                          // LEAD
    case 0: case 1: case 5: case 6: case 7: return 5;   // KICK SNARE PERCUSSION HI-HAT CYMBAL -> DRUMS
    case 15: return 6;                          // SOUND FX -> FX
    default: return -1;
    }
}
double dn2RatioA(int i) { return i < 20 ? (double)(i + 1) / 4 : i < 30 ? 5 + (double)(i - 19) / 2 : (double)(i - 19); }
// FM TONE's LFO destinations by DN2 parameter id (dn2pst.mjs DN2_LFO_DEST); -1: none
int dn2LfoDest(int id)
{
    static const std::map<int, int> m = {
        {0, 0}, {1, 40}, {5, 41}, {9, 42}, {17, 43}, {21, 44}, {25, 45}, {29, 46}, {33, 3}, {34, 4}, {35, 5}, {36, 6},
        {37, 11}, {38, 12}, {39, 13}, {40, 14}, {43, 17}, {44, 18}, {45, 19}, {46, 15}, {47, 20}, {48, 21}, {49, 22},
        {50, 16}, {51, 23}, {54, 24}, {58, 1}, {59, 2}, {60, 47}, {61, 48}, {62, 49}, {63, 50}, {64, 51}, {65, 52},
        {66, 7}, {67, 8}, {68, 9}, {69, 10}, {74, 25}, {75, 26}, {76, 27}, {77, 57}, {78, 30}, {79, 31}, {80, 32},
        {81, 33}, {83, 28}, {84, 29}, {87, 34}, {89, 35}, {90, 36}, {91, 37}, {92, 56}, {93, 55}, {94, 54}, {95, 39},
        {96, 38}, {104, 53},
        {73, 60}, {88, 58}, {100, 59}, {101, 62}, {102, 63}};   // (E4: FIZZ's destinations for the DN2's own)
    const auto it = m.find(id);
    return it == m.end() ? -1 : it->second;
}
juce::String dn2IdName(int id)
{
    static const std::map<int, const char *> m = {
        {2, "LFO2 SPD"}, {6, "LFO2 MULT"}, {10, "LFO2 FADE"}, {14, "LFO2 DEST"}, {18, "LFO2 WAVE"}, {22, "LFO2 SPH"},
        {26, "LFO2 MODE"}, {30, "LFO2 DEP"}, {41, "PHRT"}, {52, "A TRIG"}, {53, "A RESET"}, {55, "B TRIG"},
        {56, "B RESET"}, {70, "KEY TRACK A"}, {71, "KEY TRACK B1"}, {72, "KEY TRACK B2"}, {73, "FILTER TYPE"},
        {82, "FILTER KEY TRACK"}, {85, "FILTER RESET"}, {86, "AMP DELAY"}, {88, "AMP HOLD"}, {97, "AMP MODE"},
        {98, "AMP RESET"}, {99, "PORTAMENTO"}, {100, "PORTAMENTO TIME"}, {101, "BIT REDUCTION"},
        {102, "SAMPLE-RATE REDUX"}, {103, "SRR ROUTING"}, {105, "BW ROUTING"}, {106, "OVERDRIVE ROUTING"}};
    const auto it = m.find(id);
    return it == m.end() ? "#" + juce::String(id) : juce::String(it->second);
}
int multToQuad(int n)
{
    const int f = n / 6;
    return (n % 6) + 6 * (f >> 1) + 12 * (f & 1);
}
uint32_t be32(const uint8_t *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }
bool magic(const uint8_t *b, size_t n, size_t o, const uint8_t m[4])
{
    return o + 4 <= n && b[o] == m[0] && b[o + 1] == m[1] && b[o + 2] == m[2] && b[o + 3] == m[3];
}
const uint8_t BEEFBACE[4] = {0xbe, 0xef, 0xba, 0xce}, CONTAINER[4] = {0xac, 0x11, 0xd3, 0x03},
              FOOTER[4] = {0xaa, 0xa1, 0xda, 0xaa}, RECEND[4] = {0xba, 0xce, 0xf0, 0x0c};
uint32_t crc32z(const uint8_t *b, size_t n)     // zero initial register, reflected, xorout FFFFFFFF
{
    static uint32_t T[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            T[i] = c;
        }
        made = true;
    }
    uint32_t c = 0;
    for (size_t i = 0; i < n; i++)
        c = T[(c ^ b[i]) & 255] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// the LZ4 block chain from src[p]: u32be size, a block, .., u32be 0 (one dictionary); want bytes. -> end
bool lz4Chain(const uint8_t *src, size_t n, size_t p, size_t want, std::vector<uint8_t> &o, size_t &endOut,
              juce::String &err)
{
    o.assign(want, 0);
    size_t k = 0;
    const juce::String cut = juce::String(kNot2) + " (the sound data is cut short)";
    const juce::String bad = juce::String(kNot2) + " (the sound data does not decompress)";
    for (;;) {
        if (p + 4 > n)
            return err = cut, false;
        const size_t size = be32(src + p);
        p += 4;
        if (!size)
            break;
        const size_t end = p + size;
        if (end > n)
            return err = cut, false;
        auto byte = [&](int &b) {
            if (p >= end)
                return false;
            b = src[p++];
            return true;
        };
        while (p < end) {
            int t, b;
            if (!byte(t))
                return err = bad, false;
            size_t l = (size_t)(t >> 4);
            if (l == 15)
                do {
                    if (!byte(b))
                        return err = bad, false;
                    l += (size_t)b;
                } while (b == 255);
            if (k + l > want)
                return err = bad, false;
            for (size_t i = 0; i < l; i++) {
                if (!byte(b))
                    return err = bad, false;
                o[k++] = (uint8_t)b;
            }
            if (p >= end)
                break;                          // the block's last sequence: literals only
            int lo, hi;
            if (!byte(lo) || !byte(hi))
                return err = bad, false;
            const size_t off = (size_t)(lo | hi << 8);
            size_t m = (size_t)(t & 15) + 4;
            if ((t & 15) == 15)
                do {
                    if (!byte(b))
                        return err = bad, false;
                    m += (size_t)b;
                } while (b == 255);
            if (!off || off > k || k + m > want)
                return err = bad, false;
            for (size_t i = 0; i < m; i++, k++)
                o[k] = o[k - off];
        }
    }
    if (k != want)
        return err = juce::String(kNot2) + " (the sound data decompresses to " + juce::String((int)k) + " bytes, not " +
                     juce::String((int)want) + ")", false;
    endOut = p;
    return true;
}

// a stored container -> its body (readContainer); warnings as the JS's
bool readContainer(const uint8_t *pl, size_t n, std::vector<uint8_t> &body, uint32_t &objectVersion,
                   juce::StringArray &warnings, juce::String &err)
{
    if (n < 31 + 12 || !magic(pl, n, 0, CONTAINER))
        return err = juce::String(kNot2) + " (the payload's magic)", false;
    const int device = pl[7] << 8 | pl[8];
    const uint32_t kind = be32(pl + 13), length = be32(pl + 25);
    objectVersion = be32(pl + 17);
    if (device == 9)
        return err = "a Digitone 1 sound (device 9): read it with the .dnsnd reader", false;
    if (device != 15)
        warnings.add("the container's device is " + juce::String(device) + ", not 15 (Digitone II): read as one");
    if (kind != 3)
        return err = juce::String(kNot2) + " (the container holds content kind " + juce::String((int)kind) +
                     ", not 3: a sound)", false;
    size_t end;
    if (pl[29] != 0) {
        if (!lz4Chain(pl, n, 31, length, body, end, err))
            return false;
    } else {
        if (31 + (size_t)length > n)
            return err = juce::String(kNot2) + " (the body is cut short)", false;
        body.assign(pl + 31, pl + 31 + length);
        end = 31 + length;
    }
    if (n - end != 12 || !magic(pl, n, n - 4, FOOTER)) {
        warnings.add("the payload's trailer is not the usual 12 bytes ending AA A1 DA AA");
    } else {
        if (be32(pl + n - 12) != crc32z(pl + 31, n - 31 - 12))
            warnings.add("the payload's check field does not match (CRC-32, zero seed): read anyway");
        if (be32(pl + n - 8) != (uint32_t)(n - 43))
            warnings.add("the trailer's length field is not the payload's length - 43");
    }
    return true;
}

// the name: Windows-1252 -> FIZZ's 12 ASCII letters, accents folded (dn2pst.mjs cp1252 + fizzName)
juce::String cp1252(const uint8_t *b, int n)
{
    static const juce::juce_wchar HI[32] = {0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030,
        0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC,
        0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178};
    juce::String s;
    for (int i = 0; i < n; i++)
        s += (juce::juce_wchar)(b[i] >= 0x80 && b[i] < 0xa0 ? HI[b[i] - 0x80] : b[i]);
    return s;
}
juce::String fizzName(const juce::String &s)
{
    // NFD's base letter for the Latin-1 and Windows-1252 letters that decompose (U+00C0..U+00FF, S/Z/s/z caron, Y
    // diaeresis); every other character outside 0x20..0x7E -> '?'
    static const char *const L1 = "AAAAAA?CEEEEIIII?NOOOOO??UUUUY??aaaaaa?ceeeeiiii?nooooo??uuuuy?y";
    juce::String a;
    for (auto p = s.getCharPointer(); !p.isEmpty(); ++p) {
        const juce::juce_wchar c = *p;
        char o = '?';
        if (c >= 0x20 && c <= 0x7e)
            o = (char)c;
        else if (c >= 0xc0 && c <= 0xff)
            o = L1[c - 0xc0];
        else if (c == 0x160)
            o = 'S';
        else if (c == 0x17d)
            o = 'Z';
        else if (c == 0x161)
            o = 's';
        else if (c == 0x17e)
            o = 'z';
        else if (c == 0x178)
            o = 'Y';
        a += juce::String::charToString((juce::juce_wchar)(uint8_t)o);
    }
    a = a.trim().substring(0, 12).trimEnd();
    return a.isEmpty() ? juce::String("DIGITONE II") : a;
}

}   // namespace

bool parseDn2(const void *data, size_t n, Sound &s, juce::String &err)
{
    const uint8_t *bytes = (const uint8_t *)data;
    s = Sound();
    juce::StringArray warnings;
    std::vector<uint8_t> body, rec;
    bool haveHeader = false;
    uint32_t objectVersion = 0;
    juce::StringArray manTags;
    bool haveManTags = false;
    if (n >= 4 && u32(bytes) == 0x04034b50u) {                     // a .dn2pst (ZIP)
        juce::MemoryInputStream in(data, n, false);
        juce::ZipFile zip(in);
        auto entry = [&](int i, juce::MemoryBlock &mb) {
            std::unique_ptr<juce::InputStream> st(zip.createStreamForEntry(i));
            if (!st)
                return false;
            st->readIntoMemoryBlock(mb);
            return true;
        };
        juce::MemoryBlock mt, pl;
        const int mi = zip.getIndexOfFileName("manifest.json");
        if (mi < 0 || !entry(mi, mt))
            return err = juce::String(kNot2) + " (no manifest.json)", false;
        const juce::var man = juce::JSON::parse(mt.toString());
        if (!man.isObject())
            return err = juce::String(kNot2) + " (manifest.json is not JSON)", false;
        const juce::var ft = man["FileType"];
        if (!(ft.isString() && (ft.toString() == "Sound" || ft.toString() == "Preset")))
            return err = juce::String(kNot2) + " (FileType " + juce::JSON::toString(ft, true) + ", not \"Sound\")", false;
        if (ft.toString() != "Sound")
            warnings.add("FileType \"" + ft.toString() + "\": read as a sound");
        int pi = man["Payload"].isString() ? zip.getIndexOfFileName(man["Payload"].toString()) : -1;
        if (pi < 0)
            for (int i = 0; i < zip.getNumEntries() && pi < 0; i++)
                if (zip.getEntry(i)->filename != "manifest.json")
                    pi = i;
        if (pi >= 0 && (!man["Payload"].isString() || zip.getEntry(pi)->filename != man["Payload"].toString()))
            warnings.add("the payload " + juce::JSON::toString(man["Payload"], true) + " is not in the ZIP: read " +
                         juce::JSON::toString(zip.getEntry(pi)->filename, true));
        if (pi < 0 || !entry(pi, pl) || pl.getSize() == 0)
            return err = juce::String(kNot2) + " (no payload)", false;
        if (man["MetaInfo"].isObject() && man["MetaInfo"]["Tags"].isArray()) {
            haveManTags = true;
            for (const juce::var &t : *man["MetaInfo"]["Tags"].getArray())
                manTags.add(t.toString());
        }
        if (!readContainer((const uint8_t *)pl.getData(), pl.getSize(), body, objectVersion, warnings, err))
            return false;
        haveHeader = true;
    } else if (magic(bytes, n, 0, CONTAINER)) {                    // a +Drive read
        if (!readContainer(bytes, n, body, objectVersion, warnings, err))
            return false;
        haveHeader = true;
    } else {
        body.assign(bytes, bytes + n);                             // a body or a bare record
    }
    const size_t bn = body.size();
    if (bn >= 4 && body[0] == 0x44 && body[1] == 0x4e && body[2] == 0x31 && body[3] == 0x53)
        return err = "a Digitone 1 sound kept on a Digitone II (its body opens \"DN1S\"): load it on the device once to "
                     "convert it, or import the .dnsnd", false;
    if (magic(body.data(), bn, 5, BEEFBACE) && bn == 364) {
        rec.assign(body.begin() + 5, body.end());
        if (be32(body.data()) != be32(rec.data() + 4) || body[4] != 0)
            warnings.add("the body's 5-byte prefix is not the record's version and a 0");
    } else if (magic(body.data(), bn, 0, BEEFBACE)) {
        if (bn == 302)
            return err = "a Digitone 1 sound record (302 bytes): read it with the .dnsnd reader", false;
        rec.assign(body.begin(), body.begin() + (long)std::min<size_t>(bn, 359));
    } else {
        return err = juce::String(kNot2) + " (no BEEFBACE in a " + juce::String((int)bn) + "-byte body)", false;
    }
    if (rec.size() != 359)
        return err = juce::String(kNot2) + " (a " + juce::String((int)rec.size()) + "-byte record, not 359)", false;
    const uint8_t *d = rec.data();
    const uint32_t version = be32(d + 4);
    if (version < 1 || version > 3)
        return err = "a newer Digitone II sound (record version " + juce::String((int)version) + "): not supported", false;
    if (version != 2)
        warnings.add("record version " + juce::String((int)version) + ": read with version 2's layout (" +
                     (version == 3 ? juce::String("OS 1.11's version; ") : juce::String()) + "not verified on a real file)");
    if (!magic(d, 359, 355, RECEND))
        warnings.add("the record does not end BA CE F0 0C (DNX soundmap.ts DEFAULT_BYTES)");
    if (haveHeader && objectVersion != version)
        warnings.add("the container says version " + juce::String((int)objectVersion) + ", the record " +
                     juce::String((int)version));
    int nl = 0;
    while (nl < 16 && d[12 + nl])
        nl++;
    const juce::String name = cp1252(d + 12, nl);
    const uint32_t tagBits = be32(d + 8);
    juce::StringArray bitTags;
    for (int t = 0; t < 32; t++)
        if ((tagBits >> t) & 1u) {
            bitTags.add(DN2_TAGS[t]);
            const int f = dn2TagToFizz(t);
            if (f >= 0)
                s.tags |= 1 << f;
        }
    if (haveManTags && manTags.joinIntoString(",") != bitTags.joinIntoString(","))
        warnings.add("the manifest's tags (" + (manTags.isEmpty() ? juce::String("none") : manTags.joinIntoString(", ")) +
                     ") are not the record's (" + (bitTags.isEmpty() ? juce::String("none") : bitTags.joinIntoString(", ")) +
                     "): the record's kept");
    const int machine = d[244], filterMachine = d[245];
    const juce::String machineName = machine < 5 ? juce::String(DN2_MACHINES[machine]) : "machine " + juce::String(machine);
    const juce::String filterName = filterMachine < 6 ? juce::String(DN2_FILTERS[filterMachine])
                                                      : "filter " + juce::String(filterMachine);
    if (machine != 0)
        return err = "\"" + name + "\" is a " + machineName + " sound: FIZZ imports FM TONE sounds only", false;

    // ---- the parameters (dn2pst.mjs DN2_FIELDS: a value is its law's, in hundredths for the hi-res ones)
    const Rec r{d};
    Out o;
    for (int k = 0; k < 3; k++) {                                  // the LFOs
        o.set(qlfo(k, L_SPEED), jsRound(r.hi(30 + 2 * k, 64) * 100));
        o.set(qlfo(k, L_MULT), multToQuad(r.lo(38 + 2 * k)));
        o.set(qlfo(k, L_FADE), r.lo(46 + 2 * k) - 64);
        {
            const int v = r.lo(54 + 2 * k);
            int q = dn2LfoDest(v);
            if (k == 0 && q >= 40 && q <= 46)
                q = -1;                                            // LFO 1 on itself: FM TONE has none
            o.set(qlfo(k, L_DEST), q < 0 ? 0 : q);
            if (q < 0)
                o.notes.add("LFO " + juce::String(k + 1) + " destination " + dn2IdName(v) + ": FM TONE has none, off");
        }
        {
            const int v = r.lo(62 + 2 * k);
            o.set(qlfo(k, L_WAVE), v < 7 ? WAVE_TO_QUAD[v] : 0);
        }
        o.set(qlfo(k, L_PHASE), r.lo(70 + 2 * k));
        o.set(qlfo(k, L_TRIG), r.lo(78 + 2 * k));
        o.set(qlfo(k, L_DEPTH), jsRound(r.hi(86 + 2 * k, 64) * 100));
    }
    o.set(ALGO, r.lo(94) + 1);
    o.set(RC, std::min(18, r.lo(96)));
    o.set(RA, jsRound(dn2RatioA(std::min(35, r.lo(98))) * 4) - 1);
    {
        const int v = std::min(360, (r.lo(100) << 8 | r.lo(101)) >> 1);
        o.set(RB1, (double)(v / 19));
        o.set(RB2, v % 19);
    }
    o.set(HARM, jsRound(r.hi(102, 63) * 100));
    o.set(DTUN, jsRound(r.hi(104) * 100));
    o.set(FDBK, r.lo(106));
    o.set(MIX, r.lo(108) - 64);
    o.set(PHRT, r.lo(110));
    for (int i = 0; i < 4; i++) {
        o.set(AATK + i, r.lo(114 + 2 * i));
        o.set(BATK + i, r.lo(122 + 2 * i));
    }
    o.set(ADLY, r.lo(130)), o.set(ATRIG, r.lo(132)), o.set(ARST, r.lo(134));
    o.set(BDLY, r.lo(136)), o.set(BTRIG, r.lo(138)), o.set(BRST, r.lo(140));
    o.set(OFSC, jsRound(r.hi(160, 64) * 100 / 64));
    o.set(OFSA, jsRound(r.hi(162, 64) * 100 / 64));
    o.set(OFSB1, jsRound(r.hi(164, 64) * 100 / 64));
    o.set(OFSB2, jsRound(r.hi(166, 64) * 100 / 64));
    o.set(28, r.lo(168)), o.set(29, r.lo(170)), o.set(72, r.lo(172));   // AKEY, B1KEY, B2KEY (inferred)
    o.set(FREQ, jsRound(r.hi(176) * 100));
    o.set(RESO, jsRound(r.hi(178) * 100));
    o.set(FDEPTH, jsRound(r.hi(180, 64) * 100));
    o.set(FDLY, r.lo(182));
    o.set(FATK, r.lo(184)), o.set(FDEC, r.lo(186)), o.set(FSUS, r.lo(188)), o.set(FREL, r.lo(190));
    o.set(39, jsRound(std::min(100, r.lo(192)) * 127 / 100.0));   // FKTRK (a guess at the scale)
    o.set(BASE, jsRound(r.hi(194) * 100));
    o.set(WIDTH, r.lo(196));
    o.set(EATK, r.lo(202));
    o.pan = std::max(-64, std::min(63, r.lo(218) - 64));
    o.set(LEVEL, jsRound(r.hi(220)));
    o.drive = (int)std::max(0.0, std::min(127.0, jsRound(r.hi(236))));
    // the filter: machine + TYPE (E2: FIZZ's machines, dn2pst.mjs's: LEGACY, LOWPASS 4, MULTI, COMB+, COMB-, EQ; TYPE
    // their FSHAPE; EQ's GAIN RESO's coarse - 64; BW ROUTING 1: BWPOST)
    std::array<int, kExt> ext;
    extDefaults(ext);
    const int ty = r.lo(174);
    if (filterMachine == 1) {
        o.p[FTYPE] = 3;
    } else if (filterMachine == 4) {
        o.p[FTYPE] = ty == 0 ? 1 : ty == 1 ? 2 : 0;
        if (ty > 3)
            o.notes.add("LEGACY LP/HP type " + juce::String(ty) + ": not a known value, the filter off");
    } else if (filterMachine == 0 || filterMachine == 5 || filterMachine == 3 || filterMachine == 2) {
        o.p[FTYPE] = filterMachine == 0 ? 4 : filterMachine == 5 ? 5 : filterMachine == 3 ? 6 : 7;
        extSet(ext, X_FSHAPE, ty);
        if (filterMachine == 2)
            extSet(ext, X_FGAIN, (int)jsRound(r.hi(178)) - 64);
        if (filterMachine == 0 && ty)
            o.notes.add("MULTI-MODE TYPE " + juce::String(ty) + " taken 1:1 as FIZZ's morph (the law is not verified)");
    } else {
        o.p[FTYPE] = 0;
        o.notes.add(filterName + " filter: not a known machine, the filter off");
    }
    if (r.lo(238))
        extSet(ext, X_BWPOST, 1);
    if (r.lo(186) == 127) {
        o.p[FSUS] = 127;
        o.notes.add("filter DEC INF: FM TONE's filter SUS set to 127");
    }
    if (r.lo(198) == 0)
        o.notes.add("FILTER ENV RESET off not imported");
    {   // the amp: ADSR as it is; AHD (MODE 0): FIZZ's AHD (E1: AMOD, HOLD 1:1, 127 NOTE; DEC 127 INF, as FIZZ's)
        const int adec = r.lo(206);
        const double rel = r.hi(210);
        extSet(ext, X_HOLD, r.lo(204));                          // (HOLD is stored whatever the mode: kept)
        if (r.lo(222) == 0) {
            extSet(ext, X_AMOD, 1);
            o.p[EDEC] = std::min(127, adec);
            o.p[ESUS] = r.lo(208);                               // (stored though AHD has none: kept for ADSR)
            o.p[EREL] = rel >= 127 ? 127 : (int)std::min(126.0, jsRound(rel));
        } else {
            o.p[EDEC] = adec >= 127 ? 127 : std::min(126, adec);
            o.p[ESUS] = r.lo(208);
            o.p[EREL] = rel >= 127 ? 127 : (int)std::min(126.0, jsRound(rel));
        }
    }
    if (r.lo(224) == 0)
        o.notes.add("AMP ENV RESET off not imported (FM TONE's amp envelope always resets)");
    if (r.lo(230) || r.lo(232)) {                                // E3's CRUSH values, 1:1 (the laws not verified)
        extSet(ext, X_BITS, r.lo(230)), extSet(ext, X_SRR, r.lo(232)), extSet(ext, X_CRPOST, r.lo(234) ? 1 : 0);
        o.notes.add("BIT REDUCTION " + juce::String(r.lo(230)) + ", SAMPLE RATE REDUCTION " + juce::String(r.lo(232)) +
                    ": taken 1:1 (the laws are not verified)");
    }
    if (r.lo(226) == 64)                                         // (E1: PORT ON, TIME 1:1)
        extSet(ext, X_PORT, 1), extSet(ext, X_PTIM, r.lo(228));
    if (r.lo(240) == 0) {                                        // E1: OD ROUTING 0 read as before the filter (a guess)
        extSet(ext, X_DRPRE, 1);
        if (o.drive)
            o.notes.add("OVERDRIVE before the filter (OD ROUTING 0 read as PRE: the encoding is not verified)");
    }
    {
        const int oct = (int)(int8_t)d[251];                       // the sound's OCTAVE (inferred: +251)
        if (oct) {
            const bool ok = octaveInto(o.p, oct);
            o.notes.add("the sound's OCTAVE " + juce::String(oct / 12.0) + (ok ? ": every ratio scaled"
                                                                                 : " not imported (a ratio has no such value)"));
        }
    }
    {   // E4: the modulation records (+252 + 3k, k < 24: six sources x four, PB VEL MW BR AT KEY; the third byte the
        // destination's id, the first two the depth, bipolar about 64: inferred, not verified) into the eight slots
        static const int SRC[6] = {5, 1, 3, 6, 4, 2};
        static const char *const SRCN[6] = {"PB", "VEL", "MW", "BR", "AT", "KEY"};
        int slot = 0, lost = 0;
        if (rec.size() >= 252 + 72)
            for (int k = 0; k < 24; k++) {
                const int o3 = 252 + 3 * k, id = d[o3 + 2];
                if (!id)
                    continue;
                const int q = dn2LfoDest(id);
                if (q <= 0) {
                    o.notes.add(juce::String("MOD ") + SRCN[k >> 2] + " -> " + dn2IdName(id) + ": FIZZ has no such destination");
                    continue;
                }
                if (slot >= 8) {
                    lost++;
                    continue;
                }
                extSet(ext, X_MOD0 + 3 * slot, SRC[k >> 2]);
                extSet(ext, X_MOD0 + 3 * slot + 1, q);
                extSet(ext, X_MOD0 + 3 * slot + 2, (int)jsRound((d[o3] + d[o3 + 1] / 256.0 - 64) * 2));
                slot++;
            }
        if (slot)
            o.notes.add("MOD: " + juce::String(slot) +
                        " slot(s) from the Digitone's modulation records (their layout and depth law are inferred, not verified)");
        if (lost)
            o.notes.add("MOD: " + juce::String(lost) + " more record(s) not imported (FIZZ has eight slots)");
    }
    for (const juce::String &w : warnings)
        o.notes.add("file: " + w);
    auto send = [&](int off) { return (int)std::max(0.0, std::min(127.0, jsRound(r.hi(off)))); };
    s.name = fizzName(name);
    s.version = (int)version;
    s.values = o.p;
    pack(o.p, s.blob);
    s.drive = o.drive;
    s.pan = o.pan;
    s.chorus = send(212), s.delay = send(214), s.reverb = send(216);
    s.notes = o.notes;
    s.ext = ext;
    s.six = false;
    for (int k = 0; k < kExt; k++)
        s.six = s.six || ext[(size_t)k] != EXT_RANGES[k][2];
    pack6(s.blob, s.drive, ext, s.blob6);
    return true;
}

bool read(const juce::File &f, Sound &s, juce::String &err)
{
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb))
        return err = "cannot read " + f.getFileName(), false;
    if (f.hasFileExtension("dn2pst"))                           // (P1.5x: a Digitone II preset)
        return parseDn2(mb.getData(), mb.getSize(), s, err);
    return parse(mb.getData(), mb.getSize(), s, err);
}

std::vector<uint8_t> storeFrame(const Sound &s, bool batch)
{
    std::vector<uint8_t> f = {0xF0, 0x7D, 0x46, 0x5A, 0x01, (uint8_t)(s.six ? 0x11 : 0x10)};   // (E1: STORE6)
    const uint8_t *blob = s.six ? s.blob6.data() : s.blob.data();
    const int nb = s.six ? kBlob6 : kBlob;
    const size_t from = 5;
    f.push_back((uint8_t)((kImportIntoPool ? 0x01 : 0x00) | (batch ? 0x02 : 0x00)));   // flags: POOL / LIBRARY, BATCH
    f.push_back(0), f.push_back(0);              // slot 0: the first free
    for (int i = 0; i < 12; i++) {
        const juce::juce_wchar c = i < s.name.length() ? s.name[i] : ' ';
        f.push_back((uint8_t)(c >= 0x20 && c <= 0x7E ? c : '?'));
    }
    f.push_back((uint8_t)std::max(0, std::min(127, s.drive)));
    f.push_back(0);                              // (no LOAD: track 0)
    f.push_back((uint8_t)std::max(0, std::min(127, s.pan + 64)));   // P1.5x: the preset keeps its pan and sends
    f.push_back((uint8_t)std::max(0, std::min(127, s.chorus)));
    f.push_back((uint8_t)std::max(0, std::min(127, s.delay)));
    f.push_back((uint8_t)std::max(0, std::min(127, s.reverb)));
    f.push_back(0x0F);                           // mixmask: pan, chorus, delay, reverb
    for (int i = 0; i < nb; i += 7) {            // 7-bit: groups of 7 as 8, the first their top bits
        const int k = std::min(7, nb - i);
        uint8_t hi = 0;
        for (int j = 0; j < k; j++)
            hi |= (uint8_t)(((blob[i + j] >> 7) & 1) << j);
        f.push_back(hi);
        for (int j = 0; j < k; j++)
            f.push_back(blob[i + j] & 0x7F);
    }
    if (s.tags & 0x7F)
        f.push_back((uint8_t)(s.tags & 0x7F));   // P1.5x: the tags byte (FIZZ's kinds; none: FIZZ's classifier decides)
    uint32_t sum = 0;
    for (size_t i = from; i < f.size(); i++)
        sum += f[i];
    f.push_back((uint8_t)((0x80u - (sum & 0x7Fu)) & 0x7Fu));
    f.push_back(0xF7);
    return f;
}

}   // namespace fm1dnsnd
