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
const Obj *changed_obj(const std::vector<Obj> &was, const std::vector<Obj> &now)
{
    for (const Obj &o : now) {
        const Obj *w = find(was, o.id);
        if (o.size && (!w || w->size != o.size || w->crc != o.crc))
            return &o;
    }
    return nullptr;
}
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
        const Obj *o = changed_obj(was, man);
        if (!o || !s.get(*o, before)) {
            s.why = "no backup object changed after the store " + s.why;
            return false;
        }
        const size_t at = find_text(before, "SYSEX");
        if (at == std::string::npos) {
            s.why = "object " + std::to_string(o->id) + " changed, but holds no preset named SYSEX";
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
// SLOOP 2.7.1, sloopDX 4.0, ORBIT 0.7.1, No Man Sky FM 0.4: the firmware's own editor.c (web/EDITOR_PROTOCOL.md v12),
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
    {"melodee", "MELODEE v1.4.4", true, "FM-1_900", false, up_store_change(80)},
    // the sloop family: editor.c's v6 backup; the identity is build.py's development FM-1_900 (no --release)
    {"sloop", "FELUCCA SLOOP 2.7.1", true, "FM-1_900", false, sl_upreset_change, true},
    {"sloopdx", "FELUCCA sloopDX 4.0", true, "FM-1_900", false, sl_dxbank_change, true, sl_dxbank_kept},
    {"orbit", "FELUCCA ORBIT 0.7.1", true, "FM-1_900", false, sl_upreset_change, true},
    {"nomansky", "FELUCCA NMS FM 0.4", true, "FM-1_900", false, sl_upreset_change, true},
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
