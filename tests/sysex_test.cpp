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
#include <cmath>
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
    std::string worst_what;         // .. and to which request (its first bytes after F0)
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
                    if (n > worst || worst_what.empty()) {
                        char w[32];
                        std::snprintf(w, sizeof w, "%02X %02X %02X %02X", q.size() > 1 ? q[1] : 0, q.size() > 2 ? q[2] : 0,
                                      q.size() > 3 ? q[3] : 0, q.size() > 4 ? q[4] : 0);
                        worst_what = w;
                    }
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
    // the backup's command numbers and LIST header: FL (cr_backup.c, Felucca) LIST / GET / PUT = 65 / 66 / 67, LIST
    // answers 1, rc, n, items; SLOOP's editor.c v6 (Profile::bk_v6) BK_LIST / BK_GET / BK_PUT = 34 / 35 / 36, LIST
    // answers rc, n, items (web/editor.html parse[CMD.BK_LIST]); GET and PUT are the same bytes
    uint8_t bk = 65;
    size_t list_hdr = 2;
    explicit Session(Link &l) : k(l) {}
    void use_v6() { bk = 34, list_hdr = 1; }

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
        if (!k.request(fl(bk), fl_reply(bk), r, 3000))
            return why = "LIST: no reply", false;
        Bytes a = fl_args(r);
        const size_t h = list_hdr;            // (the items start after it and the count)
        if (a.size() < h + 1 || (h == 2 && a[0] != 1) || a[h - 1] != 0)
            return why = "LIST: rc", false;
        const size_t n = a[h];
        if (a.size() != h + 1 + n * 11)
            return why = "LIST: length", false;
        m.clear();
        for (size_t i = 0; i < n; i++)
            m.push_back({a[h + 1 + i * 11], r32(a, h + 1 + i * 11 + 1), r32(a, h + 1 + i * 11 + 6)});
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
            if (!k.request(fl(bk + 1, args), fl_reply(bk + 1), r, 1000))
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
        if (!k.request(fl(bk + 2, args), fl_reply(bk + 2), r, limit))
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
    bool bk_v6 = false;             // SLOOP's editor.c v6 backup numbers and LIST header (Session::use_v6)
    // a check on the core rebooted from the flash image (after the preset's), nullptr: none
    std::function<bool(Link &, Session &)> after_reboot = nullptr;
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

// Felucca and its forks: a user preset stored as the web editor stores one (editor.html req.upStore: UP_STORE 19,
// slot 0, the name "SYSEX", its NUL), found in the backup object it changed (a user preset bank: Felucca's upreset.c
// up_bank_t, DUELL's 128-slot store), its name's first letter changed in the backup's copy, and restored over PUT
size_t find_text(const Bytes &b, const char *t)
{
    const size_t n = std::strlen(t);
    for (size_t i = 0; i + n <= b.size(); i++)
        if (!std::memcmp(&b[i], t, n))
            return i;
    return std::string::npos;
}
// store_cmd 19 (UP_STORE: slot, the name, NUL -> slot, rc); Melodee's shared library (its editor.html, INFO "shared":
// sharedBank.action(rq, 3, slot, track, name): LIBRARY 80, op 3 store, slot as two 7-bit bytes, the track, the name
// without its NUL -> op, slot, slot, rc)
ChangeFn up_store_change(uint8_t store_cmd = 19)
{
    return [=](Link &k, Session &s, std::vector<Obj> &man, uint32_t &id, Bytes &before, Bytes &after) {
        std::vector<Obj> was;
        if (!s.list(was))
            return false;
        Bytes r;
        const Bytes args = store_cmd == 80 ? Bytes{3, 0, 0, 0, 'S', 'Y', 'S', 'E', 'X'} : Bytes{0, 'S', 'Y', 'S', 'E', 'X', 0};
        const size_t rc_at = store_cmd == 80 ? 3 : 1;
        if (!k.request(fl(store_cmd, args), fl_reply(store_cmd), r, 5000)) {
            s.why = "store: no reply";
            return false;
        }
        if (fl_args(r).size() <= rc_at || fl_args(r)[rc_at]) {
            s.why = "store: rc " + std::to_string(fl_args(r).size() > rc_at ? fl_args(r)[rc_at] : -1);
            return false;
        }
        if (!s.list(man))
            return false;
        // the changed object that holds the preset (Mini-Tone: the project's object 0 changes with the store too)
        const Obj *o = nullptr;
        size_t at = std::string::npos;
        for (const Obj &c : man) {
            const Obj *w = find(was, c.id);
            if (!c.size || (w && w->size == c.size && w->crc == c.crc))
                continue;
            if (!s.get(c, before)) {
                s.why = "GET after the store " + s.why;
                return false;
            }
            o = &c;
            if ((at = find_text(before, "SYSEX")) != std::string::npos)
                break;
        }
        if (!o) {
            s.why = "no backup object changed after the store " + s.why;
            return false;
        }
        if (at == std::string::npos) {
            s.why = "object " + std::to_string(o->id) + " changed, but no changed object holds a preset named SYSEX";
            return false;
        }
        std::printf("    store: the preset is in object %u at byte %zu\n", o->id, at);
        id = o->id;
        after = before;
        after[at] = 'Z';
        return true;
    };
}
// ---- the sloop family ----
// SLOOP 2.8, sloopDX 4.0, ORBIT 0.7.1, No Man Sky FM 0.5: the firmware's own editor.c (web/EDITOR_PROTOCOL.md v12),
// their web editors' requests (web/editor.html `req`): INFO 1, the backup BK_LIST / BK_GET / BK_PUT 34 / 35 / 36
// (256-byte chunks, backupCapture / backupRestore), the user presets UP_STORE 19, sloopDX's DX7 bank BANK_BEGIN /
// BANK_WRITE / BANK_END / BANK_INFO 37..40 (512-byte chunks, dxbank.upload). No restart command: the reboot is the
// plugin's power cycle after a flash_sync.
// a user preset stored from the selected track's sound (req.upStore(slot, name): slot, strEnc(name)), as the
// editor's STORE: user preset bank 1 (backup object 6) holds it; then the backup's copy of that bank has the
// preset's name changed (up_rec_t: bank header 8 bytes, a record's name at +4: slot 0's first letter at byte 12,
// "SYSEX" -> "ZYSEX") and is restored over BK_PUT
bool sl_upreset_change(Link &k, Session &s, std::vector<Obj> &man, uint32_t &id, Bytes &before, Bytes &after)
{
    Bytes r;
    const Bytes store = fl(19, {0, 'S', 'Y', 'S', 'E', 'X', 0});
    if (!k.request(store, fl_reply(19), r, 2500) || fl_args(r).size() < 2 || fl_args(r)[1] != 0) {
        s.why = "UP_STORE 0 \"SYSEX\": rc " + std::to_string(fl_args(r).size() >= 2 ? fl_args(r)[1] : -1);
        return false;
    }
    if (!s.list(man))
        return false;
    const Obj *o = find(man, 6);
    if (!o || o->size < 8 + 16 * 192 || !s.get(*o, before)) {   // (up_bank_t: 8 + 16 records of 192, ORBIT 196)
        s.why = "user preset bank 1 (6) missing or empty after UP_STORE";
        return false;
    }
    if (std::memcmp(&before[12], "SYSEX", 6) != 0) {
        s.why = "user preset bank 1: slot 0 is not the preset stored";
        return false;
    }
    id = 6;
    after = before;
    after[12] = 'Z';
    return true;
}
// sloopDX: a DX7 .syx bank uploaded as its editor does (dxbank.upload: BANK_BEGIN, BANK_WRITE off(2) + 512 7-bit
// bytes x 8, BANK_END checksum: the bank plays and is stored in flash), BANK_INFO names its voices, the backup's
// object 8 (the 32 x 128 VMEM bytes) is that bank; after the reboot it is still there (sl_dxbank_kept). The preset PUT
// is the user preset bank's, as SLOOP's: object 8 cannot be restored over BK_PUT on this firmware (its begin answers
// rc 1: editor.c stages a PUT in proj_tmp, 3628 bytes, and the bank is 4096; the device's layout is the same, so its
// manager's backupRestore stops at "begin 8 rc 1" there too)
const Bytes &sl_dx_data()
{
    static const Bytes syx = dx7_bulk();
    static const Bytes data(syx.begin() + 6, syx.begin() + 6 + 4096);
    return data;
}
bool sl_dxbank_change(Link &k, Session &s, std::vector<Obj> &man, uint32_t &id, Bytes &before, Bytes &after)
{
    const Bytes syx = dx7_bulk();
    const Bytes &data = sl_dx_data();
    Bytes r;
    if (!k.request(fl(37), fl_reply(37), r, 1000) || fl_args(r).empty() || fl_args(r)[0]) {
        s.why = "BANK_BEGIN: no reply or rc";
        return false;
    }
    for (uint32_t off = 0; off < 4096; off += 512) {
        Bytes a = {(uint8_t)(off & 127u), (uint8_t)(off >> 7 & 127u)};
        a.insert(a.end(), data.begin() + off, data.begin() + off + 512);
        if (!k.request(fl(38, a), fl_reply(38), r, 1000) || fl_args(r).size() < 3 || fl_args(r)[2]) {
            s.why = "BANK_WRITE @" + std::to_string(off) + ": no reply or rc";
            return false;
        }
    }
    const uint32_t dirty = k.dev.flash_dirty();
    if (!k.request(fl(39, {syx[6 + 4096]}), fl_reply(39), r, 2500) || fl_args(r).empty() || fl_args(r)[0]) {
        s.why = "BANK_END: rc " + std::to_string(fl_args(r).empty() ? -1 : fl_args(r)[0]);
        return false;
    }
    if (k.dev.flash_dirty() == dirty) {
        s.why = "BANK_END: the bank was not written to flash";
        return false;
    }
    if (!k.request(fl(40), fl_reply(40), r, 1000) || fl_args(r).size() < 2 || !fl_args(r)[0] ||
        std::string((const char *)&fl_args(r)[1]).rfind("SYSEX V01", 0) != 0) {
        s.why = "BANK_INFO: the bank's names are not the voices sent";
        return false;
    }
    if (!s.list(man))
        return false;
    const Obj *o = find(man, 8);
    if (!o || o->size != 4096 || !s.get(*o, before)) {
        s.why = "the DX7 bank (8) missing or empty after BANK_END";
        return false;
    }
    if (before != data) {
        s.why = "the DX7 bank (8) is not the bank sent";
        return false;
    }
    return sl_upreset_change(k, s, man, id, before, after);
}
bool sl_dxbank_kept(Link &, Session &s)
{
    std::vector<Obj> m;
    Bytes b;
    return s.list(m) && find(m, 8) && s.get(*find(m, 8), b) && b == sl_dx_data();
}
// ---- end of the sloop family ----
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
// (fm1-base's backup.c is X0X's, from Polly's: the same frames, the request's body the app's magic)
bool x0x_backup(Link &k, Bytes &image, std::string &why, uint32_t *frames_out = nullptr,
                const Bytes &magic = {0x58, 0x30, 0x58, 0x42})
{
    k.frames.clear();
    image.clear();
    k.put_frame(upd_frame(0x42, magic));
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

std::function<void(const std::string &, Device &, Link &, Session &, const Profile &)>
installer_run(const char *reader, bool log);
// ---- LOOM-8: fm1-base's backup page (base/web/backup, base/tools/fm1_backup.py: backup.c, X0X's frames with the
// app's magic "LM8B" and its 22 objects: the user presets, then the three project slots' 7 each), the user sample
// bank (app/samples.c, tools/loom_samples.py: 7D 46 4C), the installer (the M-UPGRADE identity) and the screen
// reader ----
const Bytes kLoomMagic = {0x4C, 0x4D, 0x38, 0x42};          // APP_BACKUP_MAGIC 0x42384D4C, little-endian
// backup.c's image: head (magic, u16 version 1, u16 count, u32 flags, CRC-32 of the 12 bytes), the records in
// APP_BACKUP_OBJS's order (u16 id, u16 length, u32 CRC-32, the payload; length 0: absent), the tail
bool loom_parse(const Bytes &im, std::string &why, std::string *sizes = nullptr)
{
    if (im.size() < 16 + 22 * 8 + 4 || rd32le(im, 0) != 0x42384D4Cu || (im[4] | im[5] << 8) != 1 ||
        (im[6] | im[7] << 8) != 22 || rd32le(im, 12) != crc32(im.data(), 12))
        return why = "the image's head", false;
    size_t pos = 16;
    for (uint32_t want = 1; want <= 22; want++) {           // OBJ_PRESETS (1), OBJ_PROJ0 + 0..20 (2..22)
        if (pos + 8 > im.size())
            return why = "cut short", false;
        const uint32_t obj = im[pos] | im[pos + 1] << 8, ln = im[pos + 2] | im[pos + 3] << 8, c = rd32le(im, pos + 4);
        pos += 8;
        if (obj != want || ln > 3840 || pos + ln + 4 > im.size() || crc32(&im[pos], ln) != c)
            return why = "record " + std::to_string(want), false;
        if (sizes && ln)
            *sizes += " " + std::to_string(obj) + ":" + std::to_string(ln);
        pos += ln;
    }
    if (pos + 4 != im.size() || rd32le(im, pos) != crc32(im.data(), pos))
        return why = "the tail", false;
    return true;
}

// the sample bank's commands: F0 7D 46 4C cmd args F7 -> the reply's args (after the command byte)
bool sb_req(Link &k, uint8_t cmd, const Bytes &args, Bytes &out, double limit = 3000)
{
    Bytes r;
    if (!k.request(fl(cmd, args), fl_reply(cmd), r, limit))
        return false;
    out = fl_args(r);
    return true;
}
Bytes sb_pack7(const uint8_t *p, size_t n)                   // loom_samples.py pack7: each 7 bytes after their top bits
{
    Bytes o;
    for (size_t i = 0; i < n; i += 7) {
        const size_t g = std::min<size_t>(7, n - i);
        uint8_t hi = 0;
        for (size_t j = 0; j < g; j++)
            hi |= (uint8_t)((p[i + j] >> 7 & 1) << j);
        o.push_back(hi);
        for (size_t j = 0; j < g; j++)
            o.push_back(p[i + j] & 0x7F);
    }
    return o;
}
// tools/sampleio.py user_bank for one sample (a 440 Hz sine): its checkpoints and IMA ADPCM at virtual 2048, the
// header (magic "LSMP", version 2, the count, the data's length and CRC-32, the bank's size) and its 40-byte entry
Bytes loom_bank(const std::string &name, uint32_t n, uint16_t rate)
{
    static const int STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
                                 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337,
                                 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
                                 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
                                 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
                                 22385, 24623, 27086, 29794, 32767};
    static const int IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    Bytes ck, nib;
    int pred = 0, idx = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (i % 64 == 0) {
            const Bytes w = le32((uint32_t)(pred & 0xFFFF) | (uint32_t)idx << 16);
            ck.insert(ck.end(), w.begin(), w.end());
        }
        const int x = (int)(12000.0 * std::sin(6.283185307179586 * 440.0 * i / rate));
        const int step = STEP[idx];
        int diff = x - pred, code = 0, vd = step >> 3;
        if (diff < 0)
            code = 8, diff = -diff;
        if (diff >= step)
            code |= 4, diff -= step, vd += step;
        if (diff >= step >> 1)
            code |= 2, diff -= step >> 1, vd += step >> 1;
        if (diff >= step >> 2)
            code |= 1, vd += step >> 2;
        pred = code & 8 ? pred - vd : pred + vd;
        pred = std::max(-32768, std::min(32767, pred));
        idx = std::max(0, std::min(88, idx + IDX[code & 7]));
        nib.push_back((uint8_t)code);
    }
    if (nib.size() & 1)
        nib.push_back(0);
    Bytes data(ck);                                          // the checkpoints at 2048, the ADPCM after them
    for (size_t i = 0; i < nib.size(); i += 2)
        data.push_back((uint8_t)(nib[i] | nib[i + 1] << 4));
    const uint32_t c_off = 2048, a_off = 2048 + (uint32_t)ck.size();
    Bytes img(2048, 0);
    Bytes h = le32(0x504D534Cu);
    h.insert(h.end(), {2, 0, 1, 0});                         // version 2, one sample
    for (uint32_t v : {(uint32_t)data.size(), crc32(data.data(), data.size()), 0x1B000u + 0x17000u + 0x4000u}) {
        const Bytes w = le32(v);
        h.insert(h.end(), w.begin(), w.end());
    }
    std::copy(h.begin(), h.end(), img.begin());
    Bytes e(name.begin(), name.end());
    e.resize(12, 0);
    for (uint32_t v : {a_off, c_off, n, 0u, 0u}) {
        const Bytes w = le32(v);
        e.insert(e.end(), w.begin(), w.end());
    }
    e.insert(e.end(), {69, 0, 0, 0, (uint8_t)rate, (uint8_t)(rate >> 8), 0, 0});   // root A4, no bars, the rate
    std::copy(e.begin(), e.end(), img.begin() + 64);
    img.insert(img.end(), data.begin(), data.end());
    return img;
}
// loom_samples.py upload: BEGIN, the data in 256-byte WRITEs, the header in five 320-byte parts, END (each answered,
// its last byte the rc)
bool loom_upload(Link &k, const Bytes &img, std::string &why)
{
    Bytes r;
    if (!sb_req(k, 11, {0}, r, 10000) || r.empty() || r.back())
        return why = "BEGIN", false;
    for (uint32_t off = 2048; off < img.size(); off += 256) {
        Bytes a = {0, (uint8_t)(off & 0x7F), (uint8_t)(off >> 7 & 0x7F), (uint8_t)(off >> 14 & 0x7F)};
        const Bytes d = sb_pack7(&img[off], std::min<size_t>(256, img.size() - off));
        a.insert(a.end(), d.begin(), d.end());
        if (!sb_req(k, 12, a, r, 5000) || r.empty() || r.back())
            return why = "WRITE at " + std::to_string(off) + " rc " + (r.empty() ? "-" : std::to_string(r.back())), false;
    }
    for (uint8_t part = 0; part < 5; part++) {
        Bytes a = {0, part};
        const Bytes d = sb_pack7(&img[part * 320], 320);
        a.insert(a.end(), d.begin(), d.end());
        if (!sb_req(k, 16, a, r) || r.empty() || r.back())
            return why = "header part " + std::to_string(part), false;
    }
    if (!sb_req(k, 13, {0}, r, 20000) || r.empty() || r.back())
        return why = "END rc " + (r.empty() ? std::string("-") : std::to_string(r.back())), false;
    return true;
}
// SMP_INFO from 0: the count, and the first sample's name, rate and length
bool loom_bank_info(Link &k, uint32_t &count, std::string &first)
{
    Bytes r;
    if (!sb_req(k, 15, {0}, r) || r.size() < 8 || r[0] != 2)
        return false;
    count = r[5];
    first.clear();
    if (r[7]) {
        size_t i = 8;
        for (; i < r.size() && r[i]; i++)
            first += (char)r[i];
        if (i + 6 < r.size())
            first += " " + std::to_string(r[i + 1] | r[i + 2] << 7 | r[i + 3] << 14) + " Hz " +
                     std::to_string(r[i + 4] | r[i + 5] << 7 | r[i + 6] << 14);
    }
    return true;
}

void loom_run(const std::string &module, Device &dev, Link &k, Session &s, const Profile &p)
{
    std::string v, why, sz, first;
    installer_run("LOOM-8 ", true)(module, dev, k, s, p);   // the identity, the screen reader, the log, the update keys
    check(s.info(v) && v.rfind(p.info, 0) == 0, "samples.c INFO: \"" + v + "\"");
    Bytes r;
    check(sb_req(k, 25, {}, r) && r.size() == 1 && r[0] == 0, "samples.c PING: 0");
    uint32_t count = 99;
    check(loom_bank_info(k, count, first) && count == 0,
          "SMP_INFO on a fresh unit: " + std::to_string(count) + " user samples");
    // an upload as tools/loom_samples.py makes it: one 4096-sample sine at 22050 Hz
    const Bytes bank = loom_bank("SINE", 4096, 22050);
    const uint32_t dirty0 = dev.flash_dirty();
    check(loom_upload(k, bank, why), "a user sample uploaded (BEGIN, " + std::to_string((bank.size() - 2048 + 255) / 256) +
                                         " WRITEs, 5 header parts, END: the data's CRC checked) " + why);
    check(loom_bank_info(k, count, first) && count == 1 && first == "SINE 22050 Hz 4096",
          "SMP_INFO after it: " + std::to_string(count) + " sample, \"" + first + "\"; the NOR image written (" +
              std::to_string(dev.flash_dirty() - dirty0) + " writes)");

    Bytes a0, a, b, c;
    uint32_t frames = 0;
    bool ok = x0x_backup(k, a0, why, &frames, kLoomMagic) && loom_parse(a0, why, &sz);
    check(ok, "backup (0x42 \"LM8B\") of a fresh unit: " + std::to_string(a0.size()) + " bytes in " +
                  std::to_string(frames) + " frames, 22 records, each its CRC-32 (" + sz + " ) " + why);
    x0x_turn(dev, k, 2);              // G3: T1's step 2, saved into slot 1 (flash_sync: the autosave's write)
    sz.clear();
    ok = x0x_backup(k, a, why, &frames, kLoomMagic) && loom_parse(a, why, &sz);
    check(ok && a != a0, "backup after a step written on the panel (saved): " + std::to_string(a.size()) +
                             " bytes in " + std::to_string(frames) + " frames (" + sz + " ) " + why);
    x0x_turn(dev, k, 4);              // A3: step 3
    ok = x0x_backup(k, b, why, nullptr, kLoomMagic) && loom_parse(b, why);
    check(ok && b != a, "another step: the backup differs " + why);

    const uint32_t dirty1 = dev.flash_dirty();
    int rc = x0x_restore(k, a, why, EMU_B_HOME);
    check(rc == 11, "restore answered HOME on the FM-1: code " + std::to_string(rc) + " (11, RS_E_NO) " + why);
    ok = x0x_backup(k, c, why, nullptr, kLoomMagic);
    check(ok && c == b, "  nothing changed " + why);
    rc = x0x_restore(k, a, why, EMU_B_SEL);
    check(rc == 0, "restore of the earlier backup (0x41, SEL on the FM-1): code " + std::to_string(rc) + " " + why);
    check(dev.flash_dirty() != dirty1, "  the NOR image written (flash_dirty " + std::to_string(dirty1) + " -> " +
                                           std::to_string(dev.flash_dirty()) + ")");
    ok = x0x_backup(k, c, why, nullptr, kLoomMagic);
    check(ok && c == a, "backup after the restore: the restored project " + why);

    const bool alive = update_keys(k, s, p);
    if (alive) {
        std::string idd;
        check(mupg_identity_exact(k, idd) && idd == p.mupg, "  the installer and the backup page still find it");
    }
    dev.flash_sync();
    const Bytes image(dev.flash(), dev.flash() + dev.flash_size());
    std::unique_ptr<LoadedCore> lc2 = load(module);
    Device dev2(lc2->core());
    dev2.boot_from(image.data(), (uint32_t)image.size());
    Link k2(dev2);
    k2.run_ms(3000);
    Bytes d;
    ok = x0x_backup(k2, d, why, nullptr, kLoomMagic);
    check(ok && d == a, "rebooted from the flash image: the backup is the restored project " + why);
    check(loom_bank_info(k2, count, first) && count == 1 && first == "SINE 22050 Hz 4096",
          "rebooted from the flash image: the user sample is there (" + std::to_string(count) + ", \"" + first + "\")");
    k.worst = std::max(k.worst, k2.worst);
}

// ---- the installers' firmwares (FiMba, FoMni, Fart Machine One, AMB-1): the hub's installer (fm1ota.js) finds the
// unit by the M-UPGRADE identity and offers the update (refused: the update key is not taken); AMB-1's installer also
// reads the log (F0 7D 4C 47 F7: a frame a line, "end" last: its crash report). A screen reader where the firmware has
// one (announce.c). Nothing here writes the flash: no backup, no preset, no reboot to test.
std::function<void(const std::string &, Device &, Link &, Session &, const Profile &)>
installer_run(const char *reader, bool log)
{
    return [=](const std::string &, Device &, Link &k, Session &s, const Profile &p) {
        std::string id, name;
        check(mupg_identity_exact(k, id) && id == p.mupg, "M-UPGRADE identity: \"" + id + "\" (the package's " + p.mupg +
                                                              ", 34 bytes as the installer reads it)");
        if (reader)
            check(reader_hello(k, name) && name.rfind(reader, 0) == 0, "screen reader HELLO: \"" + name + "\"");
        if (log) {                        // installer.html "reportgo": the lines until "end", within 4 s
            k.frames.clear();
            k.put_frame({0xF0, 0x7D, 0x4C, 0x47, 0xF7});
            std::vector<std::string> lines;
            bool end = false;
            const uint64_t b0 = k.blocks;
            while (!end && k.blocks < b0 + (uint64_t)(4000 / kBlockMs)) {
                k.block();
                while (!k.frames.empty() && !end) {
                    const Bytes f = std::move(k.frames.front());
                    k.frames.pop_front();
                    if (f.size() < 5 || f[1] != 0x7D || f[2] != 0x4C || f[3] != 0x47)
                        continue;
                    const std::string t(f.begin() + 4, f.end() - 1);
                    end = t == "end";
                    if (!end)
                        lines.push_back(t);
                }
            }
            k.worst = std::max(k.worst, (uint32_t)(k.blocks - b0));
            check(end && !lines.empty() && lines[0] == "no crash recorded",
                  "the log readout (LG): " + std::to_string(lines.size()) + " line(s) then \"end\"" +
                      (lines.empty() ? std::string() : ": \"" + lines[0] + "\""));
        }
        const bool alive = update_keys(k, s, p);
        if (alive) {
            std::string again;
            check(mupg_identity_exact(k, again) && again == p.mupg, "  the installer still finds it");
        }
    };
}

// ---- FM-106: web/Librarian.html (Roland-style F0 41 ..: 34 dump request, 30 store to a slot, 35 commit to flash) and
// the installer's M-UPGRADE identity (platform_poll: ota_service for the frames that are not 41 ..)
bool fm106_dump(Link &k, uint32_t idx, Bytes &r)           // requestSlotFromHardware: F0 41 34 chByte slot F7
{
    const uint8_t opt = (uint8_t)(idx / 64 % 4 + 1), slot = (uint8_t)(idx % 64);
    auto match = [=](const Bytes &f) {
        return f.size() >= 24 && f[1] == 0x41 && f[2] == 0x30 && (f[3] >> 4 & 7) == opt && f[4] == slot;
    };
    return k.request({0xF0, 0x41, 0x34, (uint8_t)(opt << 4), slot, 0xF7}, match, r, 500);
}
std::string fm106_name(const Bytes &d)
{
    std::string n;
    for (size_t i = 23; i < 39 && i + 1 < d.size(); i++)
        n += (char)d[i];
    return n;
}
void fm106_run(const std::string &module, Device &dev, Link &k, Session &s, const Profile &p)
{
    std::string id, why;
    check(mupg_identity_exact(k, id) && id == p.mupg, "M-UPGRADE identity: \"" + id + "\" (the package's " + p.mupg + ")");
    // readAllFromHardware (and the factory banks): every slot, one dump each
    uint32_t got = 0, named = 0;
    Bytes f3;
    for (uint32_t i = 0; i < 256; i++) {
        Bytes r;
        if (fm106_dump(k, i, r)) {
            got++;
            named += r.size() == 40;
            if (i == 3)
                f3 = r;
        }
    }
    check(got == 256, "dump request (34) of every slot, factory A / B and user A / B: " + std::to_string(got) +
                          " of 256 answered (" + std::to_string(named) + " named)");
    if (f3.size() != 40) {
        check(false, "factory A 4 has no named dump to start from");
        return;
    }
    // saveCurrentSlotToHardware: factory A 4's sound, its cutoff changed, named, into user A 6
    Bytes st = {0xF0, 0x41, 0x30, 0x30, 5};
    st.insert(st.end(), f3.begin() + 5, f3.begin() + 23);  // 16 sliders, sw1, sw2
    st[5 + 5] = (uint8_t)((st[5 + 5] + 37) & 0x7F);        // slider 6: VCF cutoff
    const std::string name = "SYSEX TEST      ";             // encodePatchName: 16, space padded
    st.insert(st.end(), name.begin(), name.end());
    st.push_back(0xF7);
    k.put_frame(st);
    k.run_ms(100);
    Bytes u;
    const bool ok = fm106_dump(k, 128 + 5, u) && u.size() == 40;
    check(ok && std::equal(st.begin() + 5, st.begin() + 23, u.begin() + 5) && fm106_name(u) == name,
          "store (30) to user A 6, dumped back: the sliders, switches and name \"" + (ok ? fm106_name(u) : "") + "\"");
    // burnToHardwareFlash. FM-106 Beta5's user bank is 16 + 128 x 82 = 10,512 bytes, its flash area 8 KiB (as Beta4):
    // presets_flash_save refuses it (n > FM106_PRESETS_FLASH_SIZE) on the device as here, yet the ACK goes out
    // (fm106_sysex.h ignores the result). So the commit writes nothing and the stored preset is gone after a power
    // cycle, on a unit too; once a firmware writes it, the reboot below must find it.
    const uint32_t dirty0 = dev.flash_dirty();
    Bytes ack;
    const bool acked = k.request({0xF0, 0x41, 0x35, 0x00, 0x01, 0xF7},
                                 [](const Bytes &f) { return f.size() == 6 && f[1] == 0x41 && f[2] == 0x35 && !f[4]; }, ack,
                                 2000);
    const bool wrote = dev.flash_dirty() != dirty0;
    check(acked, std::string("commit (35): the ACK; the NOR image ") +
                     (wrote ? "written" : "unchanged (Beta5: the bank does not fit its 8 KiB, as on the device)"));
    update_keys(k, s, p);
    const Bytes image(dev.flash(), dev.flash() + dev.flash_size());
    std::unique_ptr<LoadedCore> lc2 = load(module);
    Device dev2(lc2->core());
    dev2.boot_from(image.data(), (uint32_t)image.size());
    Link k2(dev2);
    k2.run_ms(3000);
    Bytes u2;
    const bool got2 = fm106_dump(k2, 128 + 5, u2);
    if (wrote)
        check(got2 && u2 == u, "rebooted from the flash image: user A 6 is the stored preset");
    else
        check(got2 && u2.size() == 24, "rebooted from the flash image: user A 6 blank again (nothing was written)");
    k.worst = std::max(k.worst, k2.worst);
}

// GR!MM (Dinghy): its own editor mailbox, F0 7D 47 52 <cmd> .. F7 (editor.c, docs/EDITOR_PROTOCOL.md; web/editor.html
// and tools/fm1_editor_test.py use it): 20 INFO, 21 GET obj off(u35) count(u14) -> obj rc off count pack7, 22 PUT
// BEGIN (op 0, fmt, len, crc) / DATA (op 1, off, pack7 <= 256 B) / COMMIT (op 2) -> op obj rc. The editor's backup of a
// user sound: factory sound 1 (object 80, GET only) written into user sound 1 (object 16) as the page writes one, read
// back, and still there after a reboot from the flash image; INFO's sample slots in use (the four factory samples
// samples.c writes into an erased area at boot) on both units. The installer finds it by its M-UPGRADE identity.
Bytes gr(uint8_t cmd, const Bytes &args = {})
{
    Bytes f = {0xF0, 0x7D, 0x47, 0x52, cmd};
    f.insert(f.end(), args.begin(), args.end());
    f.push_back(0xF7);
    return f;
}
std::function<bool(const Bytes &)> gr_reply(uint8_t cmd)
{
    return [cmd](const Bytes &f) { return f.size() >= 6 && f[1] == 0x7D && f[2] == 0x47 && f[3] == 0x52 && f[4] == cmd; };
}
// INFO: the version and the sample slots in use
bool gr_info(Link &k, std::string &ver, uint32_t &smp_used)
{
    Bytes r;
    if (!k.request(gr(20), gr_reply(20), r, 2000))
        return false;
    const Bytes a = fl_args(r);
    size_t i = 2;
    ver.clear();
    while (i < a.size() && a[i])
        ver += (char)a[i++];
    i++;                                  // (its 0)
    if (a.size() < 2 || a[0] != 2 || a[1] != 1 || i + 3 + 5 + 5 + 2 != a.size())
        return false;
    smp_used = a[i + 3 + 5 + 5 + 1];
    return true;
}
bool gr_get(Link &k, uint32_t obj, uint32_t size, Bytes &out, std::string &why)
{
    out.clear();
    for (uint32_t off = 0; off < size; off += 256) {
        const uint32_t n = std::min<uint32_t>(256, size - off);
        Bytes q = {(uint8_t)obj}, r, chunk;
        u32(q, off);
        q.push_back((uint8_t)(n & 127u));
        q.push_back((uint8_t)(n >> 7));
        if (!k.request(gr(21, q), gr_reply(21), r, 2000)) {
            why = "(GET " + std::to_string(obj) + ": no reply)";
            return false;
        }
        const Bytes a = fl_args(r);
        if (a.size() < 9 || a[0] != obj || a[1] != 0 || r32(a, 2) != off || !unpack(a, 9, n, chunk)) {
            why = "(GET " + std::to_string(obj) + ": rc " + std::to_string(a.size() > 1 ? a[1] : -1) + ")";
            return false;
        }
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return true;
}
int gr_put(Link &k, uint32_t obj, const Bytes &b, std::string &why)   // the last rc (0 ok), -1 no reply
{
    auto step = [&](Bytes q) -> int {
        Bytes r;
        if (!k.request(gr(22, q), gr_reply(22), r, 3000)) {
            why = "(PUT op " + std::to_string(q[0]) + ": no reply)";
            return -1;
        }
        const Bytes a = fl_args(r);
        return a.size() >= 3 && a[0] == q[0] && a[1] == obj ? a[2] : -1;
    };
    Bytes begin = {0, (uint8_t)obj, 1};
    u32(begin, (uint32_t)b.size());
    u32(begin, crc32(b.data(), b.size()));
    int rc = step(begin);
    for (uint32_t off = 0; !rc && off < b.size(); off += 256) {
        Bytes d = {1, (uint8_t)obj};
        u32(d, off);
        const Bytes p = pack(&b[off], std::min<size_t>(256, b.size() - off));
        d.insert(d.end(), p.begin(), p.end());
        rc = step(d);
    }
    return rc ? rc : step({2, (uint8_t)obj});
}
void grimm_run(const std::string &module, Device &dev, Link &k, Session &s, const Profile &p)
{
    std::string id, ver, why;
    uint32_t used = 0;
    check(mupg_identity_exact(k, id) && id == p.mupg, "M-UPGRADE identity: \"" + id + "\" (the package's " + p.mupg +
                                                          ", 34 bytes as the installer reads it)");
    check(gr_info(k, ver, used) && ver == "1.0" && used == 4,
          "INFO (20): protocol 2, format 1, version \"" + ver + "\", " + std::to_string(used) +
              " sample slots in use (the four factory samples)");
    Bytes fac, empty, back;
    check(gr_get(k, 80, 64, fac, why) && fac.size() == 64 && fac[0] == 0x54,
          "GET 80 (factory sound 1): 64 bytes, a sound " + why);
    check(gr_get(k, 16, 64, empty, why) && empty == Bytes(64, 0xFF), "GET 16 (user sound 1): empty on a fresh unit " + why);
    k.run_ms(2000);                       // (BEGIN and COMMIT want silence: TAPE is not idle until ~4 s after power-on)
    const uint32_t dirty = dev.flash_dirty();
    const int rc = gr_put(k, 16, fac, why);
    check(rc == 0, "PUT 16 (BEGIN, DATA, COMMIT): factory sound 1 into user sound 1: rc " + std::to_string(rc) + " " + why);
    check(dev.flash_dirty() != dirty, "  the NOR image written (flash_dirty " + std::to_string(dirty) + " -> " +
                                          std::to_string(dev.flash_dirty()) + ")");
    check(gr_get(k, 16, 64, back, why) && back == fac, "GET 16 after the PUT: the factory sound's bytes " + why);
    update_keys(k, s, p);
    dev.flash_sync();
    const Bytes image(dev.flash(), dev.flash() + dev.flash_size());
    std::unique_ptr<LoadedCore> lc2 = load(module);
    Device dev2(lc2->core());
    dev2.boot_from(image.data(), (uint32_t)image.size());
    Link k2(dev2);
    k2.run_ms(3000);
    Bytes b2;
    check(gr_get(k2, 16, 64, b2, why) && b2 == fac, "rebooted from the flash image: user sound 1 is the stored one " + why);
    used = 0;
    const size_t smp = image.size() >= 0xDF000u ? (size_t)std::count_if(image.begin() + 0x97000, image.begin() + 0xDF000,
                                                                       [](uint8_t b) { return b != 0xFF; }) : 0;
    check(smp > 0 && gr_info(k2, ver, used) && used == 4,
          "rebooted from the flash image: " + std::to_string(used) + " sample slots in use, read from the image's sample "
          "area (0x97000: " + std::to_string(smp) + " bytes written)");
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
    {"felucca", "FELUCCA v1.6.1", true, "FM-1_900", false, up_store_change()},
    {"jiant", "JIANT v0.9.9", true, "FM-1_900", false, up_store_change()},
    {"ghoulbox", "FELUCCA v1.4.1 GB2.3.2", true, "FM-1_900", false, up_store_change()},
    {"rainbow", "FELUCCA v1.1.5", true, "FM-1_900", false, up_store_change()},
    {"duell", "FELUCCA DUELL 0.2.20", true, "FM-1_900", false, up_store_change()},
    {"melodee", "MELODEE v1.4.5", true, "FM-1_900", false, up_store_change(80)},
    // Mini-Tone, FMini-1, Skiff: Felucca's editor.c (their web editors' INFO, BACKUP_LIST / GET / PUT, UP_STORE);
    // the identity is each build.py --release's (FM-1_9XY; Skiff's FM-1_7XY)
    {"minitone", "FELUCCA v1.0.1", true, "FM-1_910", false, up_store_change()},
    {"fmini", "FELUCCA v1.8.0", true, "FM-1_918", false, up_store_change()},
    {"skiff", "FELUCCA v1.4", true, "FM-1_714", false, up_store_change()},
    // the sloop family: editor.c's v6 backup; the identity is build.py's development FM-1_900 (no --release)
    {"sloop", "FELUCCA SLOOP 2.8", true, "FM-1_900", false, sl_upreset_change, true},
    {"sloopdx", "FELUCCA sloopDX 4.0", true, "FM-1_900", false, sl_dxbank_change, true, sl_dxbank_kept},
    {"orbit", "FELUCCA ORBIT 0.7.1", true, "FM-1_900", false, sl_upreset_change, true},
    {"nomansky", "FELUCCA NMS FM 0.5", true, "FM-1_900", false, sl_upreset_change, true},
    // plat, fm1base, dinghy
    {"x0x", "X0X ", false, "FM-1_9010100", false, nullptr, false, nullptr, x0x_run},
    {"amb", nullptr, false, "FM-1_7010300", false, nullptr, false, nullptr, installer_run("AMB-1 ", true)},
    {"loom", "LOOM-8 0.6-BETA", false, "FM-1_10000600", false, nullptr, false, nullptr, loom_run},
    {"fomni", nullptr, false, "FM-1_8000400", false, nullptr, false, nullptr, installer_run("FoMni-1 ", false)},
    {"fimba", nullptr, false, "FM-1_8000601", false, nullptr, false, nullptr, installer_run(nullptr, false)},
    {"fartmachine", nullptr, false, "FM-1_701", false, nullptr, false, nullptr, installer_run(nullptr, false)},
    {"fm106", nullptr, false, "FM-1_907", false, nullptr, false, nullptr, fm106_run},
    {"obervave", nullptr, false, "FM-1_955", false, nullptr, false, nullptr, installer_run(nullptr, false)},
    {"frodhes", nullptr, false, "FM-1_958", false, nullptr, false, nullptr, installer_run(nullptr, false)},
    {"grimm", nullptr, false, "FM-1_966", false, nullptr, false, nullptr, grimm_run},
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
    if (p && p->bk_v6)
        s.use_v6();
    k.run_ms(3000);                         // past the splash
    std::printf("sysex_test %s (%s %s)\n", id.c_str(), lc->core()->name, lc->core()->version);
    if (!p) {
        std::printf("  (no manager for this firmware: the update keys only)\n");
        Profile none{id.c_str(), nullptr, false, nullptr, false, nullptr, false};
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
            if (lc->core()->dump)
                lc->core()->dump();           // (the firmware's state, for the failure)
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
        if (p->bk_v6)
            s2.use_v6();
        k2.run_ms(3000);
        std::vector<Obj> m3;
        Bytes back;
        const bool got = s2.list(m3) && find(m3, put_id) && s2.get(*find(m3, put_id), back);
        check(got && back == changed, "rebooted from the flash image: object " + std::to_string(put_id) +
                                          " is the changed preset " + s2.why);
        if (p->after_reboot)
            check(p->after_reboot(k2, s2), "rebooted from the flash image: the profile's own check " + s2.why);
        if (k2.worst > k.worst)
            k.worst = k2.worst, k.worst_what = k2.worst_what;
    }
    std::printf("  worst reply: %u blocks of %u frames at %.0f Hz (%.1f ms; + one block for a request that just missed "
                "one: %.1f ms), request %s\n",
                k.worst, kBlock, kRate, k.worst * kBlockMs, (k.worst + 1) * kBlockMs, k.worst_what.c_str());
    std::printf("sysex_test %s: %s\n", id.c_str(), fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
