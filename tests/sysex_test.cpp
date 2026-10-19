// SPDX-License-Identifier: GPL-3.0-only
// The managers' SysEx on a core (research/managers-2026-10-19/MANAGERS.md; core-glue/common/sysex.h): the requests the
// firmwares' own managers send, byte for byte as their JS builds them, through the core's midi_in / midi_out_take on
// the plugin's path (FM1Processor: the request's USB-MIDI packets offered before and after each render, the replies
// drained after it), at 48 kHz in 512-frame blocks.
//
//   sysex_test MODULE.fm1core ID
//
// Per firmware (the profiles below), whatever its manager uses:
//   INFO       the identity reply, within the managers' timeout (300..1500 ms), its version text
//   M-UPGRADE  F0 00 32 45 00 00 00 40 7F F7 (fm1-editor.com, X0X's backup, the installers): the package identity
//   LIST       the backup's objects (Felucca / ChoralRoot web/fm1backup.js bkManifest)
//   GET        every object, 256-byte chunks, one request in flight (captureBackup), each checked against LIST's CRC
//   PUT        a preset changed (begin / data / commit, restoreBackup), read back and compared
//   reboot     a second core booted from the first one's flash image (after 72 RESTART where the firmware has it: the
//              core halts with exit 0, the plugin's power cycle): the changed preset is there
//   update     the update keys (M-UPGRADE's update start F0 22 24 35 7F F7, UBOOT F0 22 24 35 7D F7) neither halt the
//              core nor stop it answering
// and the worst reply time seen, in host blocks (the reply drained after the block that took the request: a request
// that arrives just after a block waits one more).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"

namespace {

int fails = 0;
void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

using Bytes = std::vector<uint8_t>;

constexpr double kRate = 48000.0;
constexpr uint32_t kBlock = 512;
constexpr double kBlockMs = 1000.0 * kBlock / kRate;

uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (c & 1u ? 0xEDB88320u : 0u);
    }
    return ~c;
}

// ---- the plugin's path: FM1Processor forwardMidi / flushMidiIn / renderSegment / drainMidiOut ----
struct Link {
    Device &dev;
    std::deque<uint32_t> pend;      // packets the core has not taken yet (inRing_)
    Bytes sx;                       // the SysEx being collected from midi_out_take
    std::deque<Bytes> frames;       // whole SysEx replies, F0..F7
    uint64_t blocks = 0;
    uint32_t worst = 0;             // the most blocks a reply took
    std::vector<float> L, R;
    explicit Link(Device &d) : dev(d), L(kBlock), R(kBlock) {}

    void put_frame(const Bytes &f)
    {
        size_t i = 0;
        const size_t n = f.size();
        while (n - i > 3) {
            pend.push_back(4u | f[i] << 8 | f[i + 1] << 16 | (uint32_t)f[i + 2] << 24);
            i += 3;
        }
        const size_t r = n - i;
        pend.push_back((r == 1 ? 5u : r == 2 ? 6u : 7u) | f[i] << 8 | (r > 1 ? f[i + 1] << 16 : 0u) |
                       (r > 2 ? (uint32_t)f[i + 2] << 24 : 0u));
    }
    void flush()
    {
        while (!pend.empty() && dev.midi_in(pend.front()))
            pend.pop_front();
    }
    void drain()
    {
        uint32_t p;
        while (dev.midi_out_take(&p)) {
            const uint32_t cin = p & 15u;
            const uint8_t b[3] = {(uint8_t)(p >> 8), (uint8_t)(p >> 16), (uint8_t)(p >> 24)};
            const int k = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : cin == 5 ? 1 : 0;
            if (!k)
                continue;           // (notes, clock: not ours)
            if (cin == 5 && b[0] != 0xF7 && sx.empty())
                continue;           // (a one-byte system common)
            if (b[0] == 0xF0)
                sx.clear();
            sx.insert(sx.end(), b, b + k);
            if (cin != 4) {
                if (sx.size() >= 2 && sx.front() == 0xF0 && sx.back() == 0xF7)
                    frames.push_back(sx);
                sx.clear();
            }
        }
    }
    void block()
    {
        flush();
        dev.render(L.data(), R.data(), kBlock, kRate);
        flush();
        drain();
        blocks++;
    }
    void run_ms(double ms)
    {
        for (uint64_t end = blocks + (uint64_t)(ms / kBlockMs + 0.999); blocks < end;)
            block();
    }
    // a request and the first reply `match` accepts (other frames before it are dropped), within limit_ms of host
    // time; false: none (timed out)
    bool request(const Bytes &q, const std::function<bool(const Bytes &)> &match, Bytes &reply, double limit_ms = 2000,
                 uint32_t *took = nullptr)
    {
        frames.clear();
        put_frame(q);
        const uint64_t b0 = blocks;
        const uint64_t end = blocks + (uint64_t)(limit_ms / kBlockMs + 0.999);
        while (blocks < end) {
            block();
            while (!frames.empty()) {
                Bytes f = std::move(frames.front());
                frames.pop_front();
                if (match(f)) {
                    const uint32_t n = (uint32_t)(blocks - b0);
                    worst = std::max(worst, n);
                    if (took)
                        *took = n;
                    reply = std::move(f);
                    return true;
                }
            }
        }
        return false;
    }
};

// ---- the FL protocol (7D 46 4C): Felucca's editor / backup, ChoralRoot's cr_backup.c (web/fm1backup.js) ----
Bytes fl(uint8_t cmd, const Bytes &args = {})
{
    Bytes f = {0xF0, 0x7D, 0x46, 0x4C, cmd};
    f.insert(f.end(), args.begin(), args.end());
    f.push_back(0xF7);
    return f;
}
std::function<bool(const Bytes &)> fl_reply(uint8_t cmd)
{
    return [cmd](const Bytes &f) { return f.size() >= 6 && f[1] == 0x7D && f[2] == 0x46 && f[3] == 0x4C && f[4] == cmd; };
}
Bytes fl_args(const Bytes &f) { return Bytes(f.begin() + 5, f.end() - 1); }   // d.slice(5, -1)
void u32(Bytes &a, uint32_t v)                                                 // bkU32
{
    for (int i = 0; i < 5; i++)
        a.push_back((uint8_t)(v >> (7 * i) & 127u));
}
uint32_t r32(const Bytes &a, size_t p)
{
    return a[p] | a[p + 1] << 7 | a[p + 2] << 14 | (uint32_t)a[p + 3] << 21 | (uint32_t)a[p + 4] << 28;
}
Bytes pack(const uint8_t *p, size_t n)                                         // bkPack
{
    Bytes a;
    for (size_t off = 0; off < n; off += 7) {
        const size_t k = std::min<size_t>(7, n - off);
        uint8_t m = 0;
        for (size_t i = 0; i < k; i++)
            m |= (uint8_t)(p[off + i] >> 7 << i);
        a.push_back(m);
        for (size_t i = 0; i < k; i++)
            a.push_back(p[off + i] & 127u);
    }
    return a;
}
bool unpack(const Bytes &a, size_t from, size_t size, Bytes &out)              // bkUnpack
{
    out.assign(size, 0);
    size_t i = from, j = 0;
    while (j < size) {
        const size_t n = std::min<size_t>(7, size - j);
        if (i >= a.size())
            return false;
        const uint8_t m = a[i++];
        if (m >> n)
            return false;
        for (size_t k = 0; k < n; k++) {
            if (i >= a.size() || a[i] > 127)
                return false;
            out[j++] = (uint8_t)(a[i++] | (m >> k & 1u) << 7);
        }
    }
    return i == a.size();
}

struct Obj {
    uint32_t id, size, crc;
};

struct Session {
    Link &k;
    std::string why;
    explicit Session(Link &l) : k(l) {}

    bool info(std::string &version, Bytes *raw = nullptr, double limit = 1500)
    {
        Bytes r;
        uint32_t took = 0;
        if (!k.request(fl(1), fl_reply(1), r, limit, &took))
            return false;
        Bytes a = fl_args(r);
        version.clear();
        for (uint8_t b : a) {
            if (!b)
                break;
            version += (char)b;
        }
        if (raw)
            *raw = a;
        std::printf("    INFO: \"%s\" in %u blocks (%u bytes)\n", version.c_str(), took, (unsigned)r.size());
        return true;
    }
    bool list(std::vector<Obj> &m)
    {
        Bytes r;
        if (!k.request(fl(65), fl_reply(65), r, 3000))
            return why = "LIST: no reply", false;
        Bytes a = fl_args(r);
        if (a.size() < 3 || a[0] != 1 || a[1] != 0)
            return why = "LIST: rc", false;
        const size_t n = a[2];
        if (a.size() != 3 + n * 11)
            return why = "LIST: length", false;
        m.clear();
        for (size_t i = 0; i < n; i++)
            m.push_back({a[3 + i * 11], r32(a, 3 + i * 11 + 1), r32(a, 3 + i * 11 + 6)});
        return true;
    }
    bool get(const Obj &o, Bytes &bytes)        // captureBackup's loop for one object
    {
        bytes.assign(o.size, 0);
        for (uint32_t off = 0; off < o.size; off += 256) {
            const uint32_t size = std::min<uint32_t>(256, o.size - off);
            Bytes args = {(uint8_t)o.id};
            u32(args, off);
            args.push_back(size & 127u);
            args.push_back((uint8_t)(size >> 7));
            Bytes r, chunk;
            if (!k.request(fl(66, args), fl_reply(66), r, 1000))
                return why = "GET " + std::to_string(o.id) + " @" + std::to_string(off) + ": no reply", false;
            Bytes a = fl_args(r);
            if (a.size() < 9 || a[1])
                return why = "GET " + std::to_string(o.id) + ": rc " + std::to_string(a.size() > 1 ? a[1] : -1), false;
            if (a[0] != o.id || r32(a, 2) != off || (a[7] | a[8] << 7) != (int)size || !unpack(a, 9, size, chunk))
                return why = "GET " + std::to_string(o.id) + ": unexpected reply", false;
            std::copy(chunk.begin(), chunk.end(), bytes.begin() + off);
        }
        if (crc32(bytes.data(), bytes.size()) != o.crc)
            return why = "GET " + std::to_string(o.id) + ": CRC differs from LIST's", false;
        return true;
    }
    int put_op(const Bytes &args, double limit = 4000)   // -> rc, or -1 (no reply)
    {
        Bytes r;
        if (!k.request(fl(67, args), fl_reply(67), r, limit))
            return -1;
        Bytes a = fl_args(r);
        return a.size() >= 3 ? a[2] : -1;
    }
    int put(uint32_t id, const Bytes &b)        // restoreBackup's begin / data / commit; -> the first nonzero rc
    {
        Bytes begin = {0, (uint8_t)id};
        u32(begin, (uint32_t)b.size());
        u32(begin, crc32(b.data(), b.size()));
        int rc = put_op(begin);
        if (rc)
            return rc;
        for (uint32_t off = 0; off < b.size(); off += 256) {
            Bytes d = {1, (uint8_t)id};
            u32(d, off);
            const size_t n = std::min<size_t>(256, b.size() - off);
            Bytes pk = pack(b.data() + off, n);
            d.insert(d.end(), pk.begin(), pk.end());
            if ((rc = put_op(d)))
                return put_op({3, (uint8_t)id}), rc;
        }
        return put_op({2, (uint8_t)id}, 5000);
    }
};

// ---- M-UPGRADE (ota.c): F0 pack7(00 59 cmd len24 body sum) F7 ----
Bytes mupg_unpack(const Bytes &f)             // ota_unpack7 of the bytes between F0 and F7
{
    Bytes out;
    uint32_t acc = 0, nb = 0;
    for (size_t i = 1; i + 1 < f.size(); i++) {
        acc |= (uint32_t)(f[i] & 127u) << nb;
        nb += 7;
        if (nb >= 8) {
            out.push_back((uint8_t)acc);
            acc >>= 8;
            nb -= 8;
        }
    }
    return out;
}
const Bytes kMupgIdentity = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
const Bytes kOtaStart = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
const Bytes kUboot = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
bool mupg_identity(Link &k, std::string &id)
{
    Bytes r;
    uint32_t took = 0;
    auto match = [](const Bytes &f) {
        if (f.size() < 4 || f[1] != 0x00 || f[2] != 0x32)
            return false;
        const Bytes d = mupg_unpack(f);
        return d.size() >= 7 && d[0] == 0 && d[1] == 0x59 && d[2] == 0x11;
    };
    if (!k.request(kMupgIdentity, match, r, 1000, &took))
        return false;
    const Bytes d = mupg_unpack(r);
    const uint32_t len = d[3] | d[4] << 8 | d[5] << 16;
    id.clear();
    for (uint32_t i = 0; i < len && 6 + i < d.size() && d[6 + i]; i++)
        id += (char)d[6 + i];
    std::printf("    M-UPGRADE identity: \"%s\" in %u blocks\n", id.c_str(), took);
    return true;
}

// ---- the firmwares ----
struct Profile {
    const char *id;
    const char *info;               // INFO's version starts with this (nullptr: no FL INFO)
    bool backup;                    // LIST / GET / PUT
    const char *mupg;               // the M-UPGRADE identity (nullptr: not answered)
    bool restart;                   // 72 RESTART (the core halts with exit 0 once the reply is out)
    // the preset to change: (the manifest, the object's bytes) -> the id to PUT and its new bytes; false: none
    std::function<bool(Link &, Session &, std::vector<Obj> &, uint32_t &, Bytes &, Bytes &)> change;
    // a manager with a protocol of its own (X0X's backup, AMB-1's installer, FM-106's Librarian): the whole sequence
    // (module, the booted core, its link): used instead of the steps above
    std::function<void(const std::string &, Device &, Link &, Session &, const struct Profile &)> run = nullptr;
};

// ChoralRoot / CoralFlower / Melodee: the FM6 patch bank (backup id 8, fm6_bank.c), filled as a DX7 librarian fills it
// (fm1-editor.com, Dexed: a 32-voice VMEM bulk dump, F0 43 00 09 20 00 <4096> sum F7, fm6_store.c: "saved"), then
// one voice changed in the backup's copy (B1's name, its first letter: VMEM byte 118 is the low 7 bits of the slot's
// packed byte 104, fm6_pack7) and restored over PUT
Bytes dx7_bulk()
{
    Bytes d(4096, 0);
    for (int v = 0; v < 32; v++) {
        uint8_t *o = &d[v * 128];
        for (int op = 0; op < 6; op++) {
            uint8_t *x = &o[op * 17];
            x[0] = x[1] = x[2] = x[3] = 99;   // EG rates
            x[4] = 99;                        // EG level 1
            x[14] = op == 5 ? 99 : 70;        // output level
            x[15] = 2;                        // coarse 1 (bits 1..5), osc mode ratio
        }
        o[102] = o[103] = o[104] = o[105] = 99;   // pitch EG rates
        o[106] = o[107] = o[108] = o[109] = 50;   // pitch EG levels
        o[110] = (uint8_t)(v % 32);           // algorithm
        o[111] = 7;                           // feedback, osc key sync
        o[117] = 24;                          // transpose
        char name[11];
        std::snprintf(name, sizeof name, "SYSEX V%02d ", v + 1);
        std::memcpy(&o[118], name, 10);
    }
    Bytes f = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    uint32_t sum = 0;
    for (uint8_t b : d)
        f.push_back(b), sum += b;
    f.push_back((uint8_t)(-(int)sum & 127));
    f.push_back(0xF7);
    return f;
}
const Obj *find(const std::vector<Obj> &m, uint32_t id)
{
    for (const Obj &o : m)
        if (o.id == id)
            return &o;
    return nullptr;
}
bool fm6_bank_change(Link &k, Session &s, std::vector<Obj> &man, uint32_t &id, Bytes &before, Bytes &after)
{
    k.put_frame(dx7_bulk());
    k.run_ms(1000);
    if (!s.list(man))
        return false;
    const Obj *o = find(man, 8);
    if (!o || o->size < 32 * 112) {
        s.why = "the FM6 bank (8) is empty after a DX7 bulk dump";
        return false;
    }
    if (!s.get(*o, before))
        return false;
    id = 8;
    after = before;
    uint8_t &c = after[o->size - 32 * 112 + 104];   // B1, VMEM byte 118: "S" -> "Z"
    if ((c & 127u) != 'S') {
        s.why = "the FM6 bank: B1 is not the voice sent";
        return false;
    }
    c = (uint8_t)((c & 0x80u) | 'Z');
    return true;
}

// a firmware whose stored preset the panel changes (a sound chosen, a setting the firmware keeps): the object as it
// was is read first, the panel changes it (and the firmware writes it: flash_sync, as a host's save), then the earlier
// copy is restored over PUT, as a manager restores a backup: the device must take it back
using ChangeFn = std::function<bool(Link &, Session &, std::vector<Obj> &, uint32_t &, Bytes &, Bytes &)>;
ChangeFn panel_change(uint32_t obj, std::function<void(Device &)> act)
{
    return [obj, act](Link &k, Session &s, std::vector<Obj> &man, uint32_t &id, Bytes &before, Bytes &after) {
        if (!s.list(man))
            return false;
        const Obj *o = find(man, obj);
        Bytes was;
        if (!o || !o->size || !s.get(*o, was)) {
            s.why = "object " + std::to_string(obj) + " missing or empty on a fresh unit";
            return false;
        }
        act(k.dev);
        k.run_ms(1500);
        k.dev.flash_sync();
        k.run_ms(100);
        if (!s.list(man) || !(o = find(man, obj)) || !s.get(*o, before))
            return false;
        if (before == was) {
            s.why = "the panel did not change object " + std::to_string(obj);
            return false;
        }
        id = obj;
        after = was;
        return true;
    };
}

// ---- plat, fm1base, dinghy ----
bool update_keys(Link &k, Session &s, const Profile &p);
std::unique_ptr<LoadedCore> load(const std::string &module);

// the update's framing (ota.c; X0X web/backup/index.html PB.frame / parseFrame / response / parseRequest):
// F0 pack7(00 59 cmd len24 body ~sum(body)) F7, pack7 LSB first over the whole message
Bytes upd_wire(const Bytes &m)
{
    Bytes f = {0xF0};
    uint32_t acc = 0, nb = 0;
    for (uint8_t b : m) {
        acc |= (uint32_t)b << nb;
        nb += 8;
        while (nb >= 7) {
            f.push_back(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7;
        }
    }
    if (nb)
        f.push_back(acc & 0x7Fu);
    f.push_back(0xF7);
    return f;
}
Bytes upd_frame(uint8_t cmd, const Bytes &body)                // PB.frame
{
    const size_t n = body.size();
    Bytes m = {0x00, 0x59, cmd, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8 & 0xFF), 0};
    uint32_t sum = 0;
    for (uint8_t b : body)
        m.push_back(b), sum += b;
    m.push_back((uint8_t)~sum);
    return upd_wire(m);
}
bool upd_parse(const Bytes &f, uint8_t &cmd, Bytes &body)     // PB.parseFrame
{
    if (f.size() < 2 || f.front() != 0xF0 || f.back() != 0xF7)
        return false;
    const Bytes u = mupg_unpack(f);
    if (u.size() < 7 || u[0] != 0 || u[1] != 0x59)
        return false;
    const size_t n = u[3] | u[4] << 8 | u[5] << 16;
    if (n + 7 != u.size())
        return false;
    uint32_t sum = 0;
    for (size_t i = 6; i < 6 + n; i++)
        sum += u[i];
    if ((uint8_t)~sum != u[6 + n])
        return false;
    cmd = u[2];
    body.assign(u.begin() + 6, u.begin() + 6 + n);
    return true;
}
Bytes le32(uint32_t v) { return {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; }
uint32_t rd32le(const Bytes &b, size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }

// the M-UPGRADE identity as the X0X page reads it (PB.parseIdentity: 34 bytes decoded, "<model>_<digits>")
bool mupg_identity_exact(Link &k, std::string &id)
{
    Bytes r;
    auto match = [](const Bytes &f) {
        const Bytes d = f.size() >= 4 && f[1] == 0x00 && f[2] == 0x32 ? mupg_unpack(f) : Bytes();
        return d.size() == 34 && d[0] == 0 && d[1] == 0x59 && d[2] == 0x11;
    };
    uint32_t took = 0;
    if (!k.request(kMupgIdentity, match, r, 1000, &took))   // DELAY.hs
        return false;
    const Bytes d = mupg_unpack(r);
    id.clear();
    for (size_t i = 6; i < 33 && d[i]; i++)
        id += (char)d[i];
    std::printf("    M-UPGRADE identity: \"%s\" in %u blocks\n", id.c_str(), took);
    return true;
}

// the screen reader (announce.c, fm1-reader): HELLO -> HELLO ver, the firmware's name, 0, its words link
bool reader_hello(Link &k, std::string &name)
{
    Bytes r;
    const Bytes hello = {0xF0, 0x7D, 0x53, 0x52, 0x01, 0x01, 0xF7};
    auto match = [](const Bytes &f) { return f.size() >= 7 && f[1] == 0x7D && f[2] == 0x53 && f[3] == 0x52 && f[4] == 1; };
    if (!k.request(hello, match, r, 1000))
        return false;
    name.clear();
    for (size_t i = 6; i + 1 < r.size() && r[i]; i++)
        name += (char)r[i];
    k.put_frame({0xF0, 0x7D, 0x53, 0x52, 0x02, 0xF7});      // BYE: no more SAYs
    k.run_ms(100);
    k.frames.clear();
    std::printf("    screen reader HELLO: \"%s\"\n", name.c_str());
    return true;
}

// ---- X0X: web/backup/index.html (PB.backup / PB.restore), firmware/src/backup.c ----
// a backup: 0x42 "X0XB", the device's 0x42 frames (offset + data) to its 0x43 end (length, CRC-32, status, flags)
bool x0x_backup(Link &k, Bytes &image, std::string &why, uint32_t *frames_out = nullptr)
{
    k.frames.clear();
    image.clear();
    k.put_frame(upd_frame(0x42, {0x58, 0x30, 0x58, 0x42}));
    uint32_t frames = 0;
    const uint64_t end = k.blocks + (uint64_t)(20000 / kBlockMs);
    while (k.blocks < end) {
        k.block();
        while (!k.frames.empty()) {
            Bytes f = std::move(k.frames.front());
            k.frames.pop_front();
            uint8_t cmd;
            Bytes b;
            if (!upd_parse(f, cmd, b))
                continue;
            if (cmd == 0x42 && b.size() >= 4) {
                if (rd32le(b, 0) != image.size())
                    return why = "a backup frame's offset skips", false;
                image.insert(image.end(), b.begin() + 4, b.end());
                frames++;
            } else if (cmd == 0x43 && b.size() >= 10) {
                if (b[8])
                    return why = "the end frame's status " + std::to_string(b[8]), false;
                if (rd32le(b, 0) != image.size() || rd32le(b, 4) != crc32(image.data(), image.size()))
                    return why = "the end frame's length or CRC-32", false;
                if (frames_out)
                    *frames_out = frames;
                return true;
            }
        }
    }
    return why = "no end frame within 20 s", false;
}
// PB.parse: the head, the six records (ids 0 1 2 3 5 6, each its CRC-32), the tail
bool x0x_parse(const Bytes &im, std::string &why, std::string *sizes = nullptr)
{
    static const uint8_t ids[6] = {0, 1, 2, 3, 5, 6};
    if (im.size() < 16 + 6 * 8 + 4 || rd32le(im, 0) != 0x42583058u || (im[4] | im[5] << 8) != 1 ||
        (im[6] | im[7] << 8) != 6 || rd32le(im, 12) != crc32(im.data(), 12))
        return why = "the image's head", false;
    size_t pos = 16;
    for (uint8_t want : ids) {
        if (pos + 8 > im.size())
            return why = "cut short", false;
        const uint32_t obj = im[pos] | im[pos + 1] << 8, ln = im[pos + 2] | im[pos + 3] << 8, c = rd32le(im, pos + 4);
        pos += 8;
        if (obj != want || ln > 3840 || pos + ln + 4 > im.size() || crc32(&im[pos], ln) != c)
            return why = "record " + std::to_string(want), false;
        if (sizes)
            *sizes += " " + std::to_string(obj) + ":" + std::to_string(ln);
        pos += ln;
    }
    if (pos + 4 != im.size() || rd32le(im, pos) != crc32(im.data(), pos))
        return why = "the tail", false;
    return true;
}
// a restore: 0x41 (length, CRC-32), then the device's read requests (0x30) answered from the image (PB.response) until
// it reads a reply address (0xC0000000 restored, 0xCF0000xx failed: code xx); at 0xC1000000 the device asks on its
// screen: SEL pressed on the panel (as the page tells the user). -> 0 restored, the device's code, or -1 (no end)
int x0x_restore(Link &k, const Bytes &image, std::string &why, int answer = EMU_B_SEL)
{
    k.frames.clear();
    Bytes q = le32((uint32_t)image.size()), c = le32(crc32(image.data(), image.size()));
    q.insert(q.end(), c.begin(), c.end());
    k.put_frame(upd_frame(0x41, q));
    uint64_t sel_at = 0;
    uint32_t reads = 0;
    const uint64_t end = k.blocks + (uint64_t)(90000 / kBlockMs);
    while (k.blocks < end) {
        k.block();
        if (sel_at && k.blocks == sel_at)
            k.dev.buttons_tap(1u << answer);
        while (!k.frames.empty()) {
            Bytes f = std::move(k.frames.front());
            k.frames.pop_front();
            const Bytes u = f.size() >= 3 ? mupg_unpack(f) : Bytes();
            if (u.size() != 15 || u[0] != 0 || u[1] != 0x59 || u[2] != 0x30)
                continue;                                       // (PB.parseRequest)
            uint32_t sum = 0;
            for (int i = 6; i < 14; i++)
                sum += u[i];
            if ((uint8_t)~sum != u[14])
                continue;
            const uint8_t fl = u[6];
            const uint32_t addr = rd32le(u, 7), len = u[11] | u[12] << 8 | u[13] << 16;
            Bytes data;
            const uint32_t top = addr & 0xFF000000u;
            if (top == 0xC0000000u || top == 0xC1000000u || top == 0xCF000000u)
                data = {0x73, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0};   // "success\0"
            else if (len > 512 || addr >= image.size())
                return why = "a read outside the image", -1;
            else {
                data.assign(image.begin() + addr, image.begin() + std::min<size_t>(image.size(), addr + len));
                data.resize(len, 0);
                reads++;
            }
            const uint32_t n = (uint32_t)data.size();
            Bytes m = {0x00, 0x59, 0x30, (uint8_t)((n + 8) & 0xFF), (uint8_t)((n + 8) >> 8), 0, fl};
            Bytes a = le32(addr);
            m.insert(m.end(), a.begin(), a.end());
            m.push_back(n & 0xFF);
            m.push_back(n >> 8 & 0xFF);
            m.push_back(0);
            m.insert(m.end(), data.begin(), data.end());
            uint32_t s2 = 0;
            for (size_t i = 6; i < m.size(); i++)
                s2 += m[i];
            m.push_back((uint8_t)~s2);
            k.put_frame(upd_wire(m));
            if (addr == 0xC1000000u) {
                std::printf("    restore: %u reads, then RESTORE FROM USB? on the screen: %s\n", reads,
                            answer == EMU_B_SEL ? "SEL" : "HOME");
                sel_at = k.blocks + 10;                           // (a person's press, after the question is up)
                continue;
            }
            if (top == 0xC0000000u || top == 0xCF000000u)
                return top == 0xC0000000u ? 0 : (int)(addr & 0xFFFF);
        }
    }
    return why = "no result within 90 s", -1;
}

// a change the panel makes to X0X's saved project: a white key writes a step into the 909 pattern (the 909 screen at
// power-on; conformance_x0x.txt "save"), written as AUTOSAVE writes it (flash_sync)
void x0x_turn(Device &d, Link &k, int key)
{
    d.keys_tap(1u << key);
    k.run_ms(400);
    d.flash_sync();
    k.run_ms(100);
}

void x0x_run(const std::string &module, Device &dev, Link &k, Session &s, const Profile &p)
{
    std::string id, v, name, why, sz;
    check(mupg_identity_exact(k, id) && id == p.mupg && id.rfind("FM-1_9", 0) == 0,
          "M-UPGRADE identity: \"" + id + "\" (the X0X page: 34 bytes, starts FM-1_9; the package's " + p.mupg + ")");
    check(s.info(v) && v.rfind(p.info, 0) == 0, "editor.c INFO: \"" + v + "\"");
    check(reader_hello(k, name) && name.rfind("X0X ", 0) == 0, "screen reader HELLO: \"" + name + "\"");

    Bytes a0, a, b, c;
    uint32_t frames = 0;
    bool ok = x0x_backup(k, a0, why, &frames) && x0x_parse(a0, why, &sz);
    check(ok, "backup (0x42) of a fresh unit: " + std::to_string(a0.size()) + " bytes in " + std::to_string(frames) +
                  " frames, every record's CRC-32 (" + sz + " ) " + why);
    x0x_turn(dev, k, 2);              // G3: step 2
    sz.clear();
    ok = x0x_backup(k, a, why, &frames) && x0x_parse(a, why, &sz);
    check(ok && a != a0, "backup after a change on the panel (saved): " + std::to_string(a.size()) + " bytes in " +
                             std::to_string(frames) + " frames (" + sz + " ) " + why);
    x0x_turn(dev, k, 4);              // A3: step 3
    ok = x0x_backup(k, b, why) && x0x_parse(b, why);
    check(ok && b != a, "another change: the backup differs " + why);

    const uint32_t dirty0 = dev.flash_dirty();
    int rc = x0x_restore(k, a, why, EMU_B_HOME);
    check(rc == 11, "restore answered HOME on the FM-1: code " + std::to_string(rc) + " (11, DECLINED) " + why);
    ok = x0x_backup(k, c, why);
    check(ok && c == b, "  nothing changed " + why);
    rc = x0x_restore(k, a, why, EMU_B_SEL);
    check(rc == 0, "restore of the earlier backup (0x41, SEL on the FM-1): code " + std::to_string(rc) + " " + why);
    check(dev.flash_dirty() != dirty0, "  the NOR image written (flash_dirty " + std::to_string(dirty0) + " -> " +
                                           std::to_string(dev.flash_dirty()) + ")");
    ok = x0x_backup(k, c, why);
    check(ok && c == a, "backup after the restore: the restored project " + why);

    const bool alive = update_keys(k, s, p);
    if (alive) {
        std::string idd;
        check(mupg_identity_exact(k, idd) && idd == p.mupg, "  the X0X page still finds it");
    }
    dev.flash_sync();
    const Bytes image(dev.flash(), dev.flash() + dev.flash_size());
    std::unique_ptr<LoadedCore> lc2 = load(module);
    Device dev2(lc2->core());
    dev2.boot_from(image.data(), (uint32_t)image.size());
    Link k2(dev2);
    k2.run_ms(3000);
    Bytes d;
    ok = x0x_backup(k2, d, why);
    check(ok && d == a, "rebooted from the flash image: the backup is the restored project " + why);
    k.worst = std::max(k.worst, k2.worst);
}
// ---- end of plat, fm1base, dinghy ----

const Profile kProfiles[] = {
    {"choralroot", "ChoralRoot ", true, "FM-1_920", true, fm6_bank_change},
    // CoralFlower: no DX7 receive service (its main loop has no fm6_service): the chord part's sound, PRESETS +2, in
    // its settings record (30, cf_settings.c), restored to the one before
    {"coralflower", "CoralFlower ", true, "FM-1_930", true,
     panel_change(30, [](Device &d) { d.enc(EMU_E_PRESETS, 2); })},
    {"purplemonkey", "PurpleMonkey ", false, "FM-1_927", false, nullptr},
    // plat, fm1base, dinghy
    {"x0x", "X0X ", false, "FM-1_9010100", false, nullptr, x0x_run},
};

const Profile *profile_of(const std::string &id)
{
    for (const Profile &p : kProfiles)
        if (id == p.id)
            return &p;
    return nullptr;
}

std::unique_ptr<LoadedCore> load(const std::string &module)
{
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc)
        std::printf("sysex_test: %s\n", err.c_str());
    return lc;
}

bool update_keys(Link &k, Session &s, const Profile &p)
{
    k.put_frame(kOtaStart);
    k.run_ms(500);
    k.put_frame(kUboot);
    k.run_ms(1000);
    const bool alive = !k.dev.halted();
    check(alive, "the update keys (F0 22 24 35 7F F7, F0 22 24 35 7D F7): the core runs on");
    if (p.info) {
        std::string v;
        check(alive && s.info(v) && v.rfind(p.info, 0) == 0, "after them INFO still answers");
    }
    if (p.mupg) {
        std::string id;
        check(alive && mupg_identity(k, id) && id == p.mupg, "after them the M-UPGRADE identity still answers");
    }
    return alive;
}

}   // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("usage: sysex_test MODULE.fm1core ID\n");
        return 2;
    }
    const std::string module = argv[1], id = argv[2];
    const Profile *p = profile_of(id);
    std::unique_ptr<LoadedCore> lc = load(module);
    if (!lc)
        return 1;
    Device dev(lc->core());
    dev.boot_from(nullptr, 0);              // a fresh unit, as the plugin boots one
    Link k(dev);
    Session s(k);
    k.run_ms(3000);                         // past the splash
    std::printf("sysex_test %s (%s %s)\n", id.c_str(), lc->core()->name, lc->core()->version);
    if (!p) {
        std::printf("  (no manager for this firmware: the update keys only)\n");
        Profile none{id.c_str(), nullptr, false, nullptr, false, nullptr};
        update_keys(k, s, none);
        std::printf("sysex_test %s: %s\n", id.c_str(), fails ? "FAIL" : "PASS");
        return fails ? 1 : 0;
    }
    if (p->run) {                           // (its own protocol: the profile's sequence)
        p->run(module, dev, k, s, *p);
        std::printf("  worst reply: %u blocks of %u frames at %.0f Hz (%.1f ms; + one block for a request that just "
                    "missed one: %.1f ms)\n",
                    k.worst, kBlock, kRate, k.worst * kBlockMs, (k.worst + 1) * kBlockMs);
        std::printf("sysex_test %s: %s\n", id.c_str(), fails ? "FAIL" : "PASS");
        return fails ? 1 : 0;
    }

    if (p->info) {
        std::string v;
        const bool ok = s.info(v);
        check(ok && v.rfind(p->info, 0) == 0, "INFO: \"" + v + "\" (starts \"" + p->info + "\")");
    }
    if (p->mupg) {
        std::string mid;
        const bool ok = mupg_identity(k, mid);
        check(ok && mid == p->mupg, "M-UPGRADE identity: \"" + mid + "\" (the package's " + p->mupg + ")");
    }

    std::vector<Obj> man;
    Bytes changed;
    uint32_t put_id = 0;
    if (p->backup) {
        check(s.list(man), "LIST: " + std::to_string(man.size()) + " objects " + s.why);
        uint32_t total = 0, nonempty = 0;
        bool all = true;
        for (const Obj &o : man) {
            Bytes b;
            if (!o.size)
                continue;
            nonempty++;
            total += o.size;
            if (!s.get(o, b)) {
                all = false;
                std::printf("    %s\n", s.why.c_str());
            }
        }
        check(all, "GET: every object (" + std::to_string(nonempty) + " not empty, " + std::to_string(total) +
                       " bytes), each matching LIST's CRC");
        std::printf("    manifest:");
        for (const Obj &o : man)
            std::printf(" %u:%u", o.id, o.size);
        std::printf("\n");
    }

    if (p->backup && p->change) {
        Bytes before, after;
        if (!p->change(k, s, man, put_id, before, after)) {
            check(false, "a preset to change: " + s.why);
        } else {
            const int rc = s.put(put_id, after);
            check(rc == 0, "PUT " + std::to_string(put_id) + " (a preset changed): rc " + std::to_string(rc));
            Bytes back;
            std::vector<Obj> m2;
            const bool got = s.list(m2) && find(m2, put_id) && s.get(*find(m2, put_id), back);
            check(got && back == after, "GET " + std::to_string(put_id) + " after the PUT: the changed bytes " + s.why);
            changed = after;
        }
    }
    const bool alive = update_keys(k, s, *p);

    if (!changed.empty() && alive) {      // the reboot: from the flash image, as the plugin powers a core on again
        if (p->restart) {
            Bytes r;
            const bool ok = k.request(fl(72), fl_reply(72), r, 2000);
            check(ok && fl_args(r).size() >= 1 && fl_args(r)[0] == 0, "72 RESTART: rc 0");
            uint32_t ms = 0;
            while (!dev.halted() && ms < 1000)
                k.run_ms(kBlockMs), ms += (uint32_t)kBlockMs;
            check(dev.halted() && lc->core()->halted() == 0x100,
                  "72 RESTART: the core halts with exit 0 (the plugin powers it on again) after " + std::to_string(ms) + " ms");
        } else {
            dev.flash_sync();
        }
        const Bytes image(dev.flash(), dev.flash() + dev.flash_size());
        std::unique_ptr<LoadedCore> lc2 = load(module);
        Device dev2(lc2->core());
        dev2.boot_from(image.data(), (uint32_t)image.size());
        Link k2(dev2);
        Session s2(k2);
        k2.run_ms(3000);
        std::vector<Obj> m3;
        Bytes back;
        const bool got = s2.list(m3) && find(m3, put_id) && s2.get(*find(m3, put_id), back);
        check(got && back == changed, "rebooted from the flash image: object " + std::to_string(put_id) +
                                          " is the changed preset " + s2.why);
        k.worst = std::max(k.worst, k2.worst);
    }
    std::printf("  worst reply: %u blocks of %u frames at %.0f Hz (%.1f ms; + one block for a request that just missed "
                "one: %.1f ms)\n",
                k.worst, kBlock, kRate, k.worst * kBlockMs, (k.worst + 1) * kBlockMs);
    std::printf("sysex_test %s: %s\n", id.c_str(), fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
