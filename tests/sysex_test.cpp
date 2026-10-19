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

const Profile kProfiles[] = {
    {"choralroot", "ChoralRoot ", true, "FM-1_920", true, fm6_bank_change},
    // CoralFlower: no DX7 receive service (its main loop has no fm6_service): the chord part's sound, PRESETS +2, in
    // its settings record (30, cf_settings.c), restored to the one before
    {"coralflower", "CoralFlower ", true, "FM-1_930", true,
     panel_change(30, [](Device &d) { d.enc(EMU_E_PRESETS, 2); })},
    {"purplemonkey", "PurpleMonkey ", false, "FM-1_927", false, nullptr},
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
