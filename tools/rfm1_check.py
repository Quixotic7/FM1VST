#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""rfm1_check: an .rfm1 file checked against docs/RFM1-FORMAT.md (version 1).

    rfm1_check.py [--quiet] <file.rfm1>..

Checks the WebAssembly binary (magic, every section's bounds), the manifest (the `rfm1` custom section: JSON, the
required fields and their forms), the `rfm1.credits` / `rfm1.license.<n>` sections, the imports (only the format's
WASI list, with their signatures), the exports (each `rfm1_*` with its signature, `memory`, `_initialize`), the memory
(one, defined here, exported, fixed: initial = maximum, at most 32 MiB, not shared, not 64-bit), and the WebAssembly
the module uses: every function body is decoded, so an instruction outside the format's feature set (SIMD, atomics,
reference types, tail calls, exceptions, memory.init / data.drop / table.*, which wasm3 0.5.0 does not run) is found,
as are passive data segments, non-MVP element segments, and LLVM's target_features section when present.

Prints each problem ("error:" or "warning:") and a summary line; exits 1 if any file has an error, 2 on a usage or
file error. Python 3 standard library only. Running the module is rfm1_run's job (tools/rfm1_run).
"""
import json
import re
import sys

MAGIC = b"\0asm\x01\0\0\0"
MAX_MEMORY = 32 * 1048576
I32, I64, F32, F64, V128, FUNCREF, EXTERNREF = 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x70, 0x6F
VT_NAME = {I32: "i32", I64: "i64", F32: "f32", F64: "f64", V128: "v128", FUNCREF: "funcref", EXTERNREF: "externref"}

# section 3: the imports, (params, results)
IMPORTS = {
    "fd_write": ((I32, I32, I32, I32), (I32,)),
    "fd_read": ((I32, I32, I32, I32), (I32,)),
    "fd_close": ((I32,), (I32,)),
    "fd_seek": ((I32, I64, I32, I32), (I32,)),
    "clock_time_get": ((I32, I64, I32), (I32,)),
    "proc_exit": ((I32,), ()),
}
# section 2: the exports, (params, results); all i32
EXPORTS = {
    "rfm1_version": (0, 1), "rfm1_get": (1, 1), "rfm1_layout": (1, 1), "rfm1_alloc": (1, 1), "rfm1_free": (1, 0),
    "rfm1_options": (4, 0), "rfm1_boot_options": (3, 1), "rfm1_init": (1, 0), "rfm1_tick": (1, 0),
    "rfm1_frame": (0, 0), "rfm1_idle": (0, 0), "rfm1_audio": (2, 0), "rfm1_midi_in": (1, 1),
    "rfm1_midi_out_take": (1, 1), "rfm1_halted": (0, 1), "rfm1_shutdown": (0, 0), "rfm1_flash_stage": (2, 1),
    "rfm1_flash": (0, 1), "rfm1_flash_dirty": (0, 1), "rfm1_flash_sync": (0, 1), "rfm1_stats": (2, 0),
    "rfm1_ui_info": (2, 0),
}
FEATURES_OK = {"sign-ext", "nontrapping-fptoint", "bulk-memory", "bulk-memory-opt", "mutable-globals", "multivalue"}


class Bad(Exception):
    pass


class R:
    """a reader over bytes"""
    __slots__ = ("b", "at", "end")

    def __init__(self, b, at=0, end=None):
        self.b, self.at, self.end = b, at, len(b) if end is None else end

    def byte(self):
        if self.at >= self.end:
            raise Bad("unexpected end of data at byte %d" % self.at)
        x = self.b[self.at]
        self.at += 1
        return x

    def u32(self):
        r = sh = 0
        while True:
            x = self.byte()
            r |= (x & 0x7F) << sh
            if not x & 0x80:
                return r
            sh += 7
            if sh > 35:
                raise Bad("bad LEB128 at byte %d" % self.at)

    def skip_leb(self):
        b, at, end = self.b, self.at, self.end
        while True:
            if at >= end:
                raise Bad("unexpected end of data at byte %d" % at)
            x = b[at]
            at += 1
            if not x & 0x80:
                break
        self.at = at

    def bytes(self, n):
        if self.at + n > self.end:
            raise Bad("unexpected end of data at byte %d" % self.at)
        v = self.b[self.at:self.at + n]
        self.at += n
        return v

    def name(self):
        n = self.u32()
        raw = self.bytes(n)
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            raise Bad("a name that is not UTF-8")


class Check:
    def __init__(self, path):
        self.path = path
        self.errors, self.warnings = [], []
        self.info = {}

    def err(self, m):
        self.errors.append(m)

    def warn(self, m):
        self.warnings.append(m)

    # ---------------------------------------------------------------------------------------------------------------
    def valtype(self, t, where):
        if t in (I32, I64, F32, F64):
            return
        if t == V128:
            self.err("%s: v128 (SIMD)" % where)
        elif t in (FUNCREF, EXTERNREF):
            self.err("%s: %s (reference types)" % (where, VT_NAME[t]))
        else:
            self.err("%s: unknown value type 0x%02x" % (where, t))

    def const_expr(self, r, where):
        while True:
            op = r.byte()
            if op == 0x0B:
                return
            if op in (0x41, 0x42):
                r.skip_leb()
            elif op == 0x43:
                r.bytes(4)
            elif op == 0x44:
                r.bytes(8)
            elif op == 0x23:
                r.u32()
            elif op in (0xD0, 0xD2):
                self.err("%s: ref.null / ref.func in a constant expression (reference types)" % where)
                r.u32()
            else:
                self.err("%s: opcode 0x%02x in a constant expression (extended-const)" % (where, op))
                return

    def limits(self, r):
        flags = r.byte()
        initial = r.u32()
        mx = r.u32() if flags & 1 else None
        return flags, initial, mx

    # ---------------------------------------------------------------------------------------------------------------
    def code(self, r, fidx):
        """one function body: its locals, then every instruction"""
        where = "function %d" % fidx
        for _ in range(r.u32()):
            r.u32()
            self.valtype(r.byte(), where + " (a local)")
        b, end = r.b, r.end
        at = r.at
        found = self.found
        while at < end:
            op = b[at]
            at += 1
            if op <= 0x01 or op in (0x05, 0x0B, 0x0F, 0x1A, 0x1B) or 0x45 <= op <= 0xC4:
                continue                                    # no immediate (MVP numerics, sign extension)
            if 0x20 <= op <= 0x24 or op in (0x0C, 0x0D, 0x10):
                while b[at] & 0x80:                         # one u32
                    at += 1
                at += 1
                continue
            if 0x28 <= op <= 0x3E:                          # memarg
                if b[at] & 0x40:
                    found.add("multi-memory")
                    raise Bad("%s: a memory index in a memarg (multi-memory): cannot decode further" % where)
                while b[at] & 0x80:
                    at += 1
                at += 1
                while b[at] & 0x80:
                    at += 1
                at += 1
                continue
            if op in (0x41, 0x42):
                while b[at] & 0x80:
                    at += 1
                at += 1
                continue
            if op == 0x43:
                at += 4
                continue
            if op == 0x44:
                at += 8
                continue
            if op in (0x02, 0x03, 0x04):                    # blocktype
                t = b[at]
                if t == 0x40 or t in (I32, I64, F32, F64):
                    at += 1
                elif t in (V128,):
                    found.add("simd")
                    at += 1
                elif t in (FUNCREF, EXTERNREF):
                    found.add("reference-types")
                    at += 1
                else:                                       # a type index (s33): multi-value
                    while b[at] & 0x80:
                        at += 1
                    at += 1
                continue
            if op == 0x0E:                                  # br_table
                r.at = at
                n = r.u32()
                for _ in range(n + 1):
                    r.skip_leb()
                at = r.at
                continue
            if op == 0x11:                                  # call_indirect: type, table (MVP: 0)
                r.at = at
                r.u32()
                if r.u32() != 0:
                    found.add("reference-types")
                at = r.at
                continue
            if op in (0x3F, 0x40):                          # memory.size / memory.grow
                if op == 0x40:
                    found.add("memory.grow")
                at += 1
                continue
            if op == 0xFC:
                r.at = at
                sub = r.u32()
                if sub <= 7:
                    pass                                    # saturating float-to-int
                elif sub == 10:
                    r.byte(), r.byte()                      # memory.copy
                elif sub == 11:
                    r.byte()                                # memory.fill
                elif sub == 8:
                    found.add("memory.init")
                    r.u32(), r.byte()
                elif sub == 9:
                    found.add("data.drop")
                    r.u32()
                elif sub in (12, 14):
                    found.add("table.init / table.copy")
                    r.u32(), r.u32()
                elif sub in (13, 15, 16, 17):
                    found.add("elem.drop / table.grow / table.size / table.fill")
                    r.u32()
                else:
                    found.add("0xFC %d" % sub)
                    raise Bad("%s: an unknown 0xFC instruction %d: cannot decode further" % (where, sub))
                at = r.at
                continue
            if op == 0xFD:
                found.add("simd")
                raise Bad("%s: a SIMD instruction (0xFD): cannot decode further" % where)
            if op == 0xFE:
                found.add("threads/atomics")
                raise Bad("%s: an atomic instruction (0xFE): cannot decode further" % where)
            if op in (0x12, 0x13, 0x14, 0x15):
                found.add("tail calls / typed function references")
                raise Bad("%s: opcode 0x%02x (tail call / call_ref)" % (where, op))
            if op in (0x06, 0x07, 0x08, 0x09, 0x0A, 0x18, 0x19, 0x1F):
                found.add("exception handling")
                raise Bad("%s: opcode 0x%02x (exception handling)" % (where, op))
            if op in (0x1C, 0x25, 0x26, 0xD0, 0xD1, 0xD2):
                found.add("reference-types")
                raise Bad("%s: opcode 0x%02x (reference types)" % (where, op))
            raise Bad("%s: unknown opcode 0x%02x" % (where, op))
        if at != end:
            raise Bad("%s: its instructions run past its end" % where)

    # ---------------------------------------------------------------------------------------------------------------
    def run(self):
        try:
            with open(self.path, "rb") as f:
                b = f.read()
        except OSError as e:
            self.err(str(e))
            return
        self.size = len(b)
        if b[:8] != MAGIC:
            self.err("not a WebAssembly module, version 1 (the magic \\0asm 01 00 00 00)")
            return
        self.found = set()
        try:
            self.module(b)
        except Bad as e:
            self.err(str(e))
        except IndexError:
            self.err("unexpected end of data inside a section")
        for f in sorted(self.found):
            if f == "memory.grow":
                self.warn("memory.grow is used (the memory is fixed: it can only fail; the format: no memory.grow)")
            else:
                self.err("uses %s, outside the format's feature set (wasm3 0.5.0 does not run it)" % f)

    def module(self, b):
        r = R(b, 8)
        types, funcs, imports_f = [], [], []
        exports = {}
        memories = []
        custom = {}
        order = []
        while r.at < r.end:
            sid = r.byte()
            size = r.u32()
            s, e = r.at, r.at + size
            if e > len(b):
                raise Bad("section %d runs past the end of the file" % sid)
            sr = R(b, s, e)
            order.append(sid)
            if sid == 0:
                name = sr.name()
                if name in custom and (name == "rfm1" or name.startswith("rfm1.")):
                    self.err("two custom sections named %s" % name)
                custom.setdefault(name, b[sr.at:e])
            elif sid == 1:
                for i in range(sr.u32()):
                    form = sr.byte()
                    if form != 0x60:
                        raise Bad("type %d: form 0x%02x (not a function type: GC proposal)" % (i, form))
                    ps = [sr.byte() for _ in range(sr.u32())]
                    rs = [sr.byte() for _ in range(sr.u32())]
                    for t in ps + rs:
                        self.valtype(t, "type %d" % i)
                    types.append((tuple(ps), tuple(rs)))
            elif sid == 2:
                for _ in range(sr.u32()):
                    mod, nm = sr.name(), sr.name()
                    kind = sr.byte()
                    what = "%s.%s" % (mod, nm)
                    if kind == 0:
                        ti = sr.u32()
                        imports_f.append(ti)
                        if mod != "wasi_snapshot_preview1" or nm not in IMPORTS:
                            self.err("imports %s: not in the format's list (section 3)" % what)
                        elif ti < len(types) and types[ti] != IMPORTS[nm]:
                            self.err("imports %s with the signature %s, not %s" % (
                                what, sig(types[ti]), sig(IMPORTS[nm])))
                    elif kind == 1:
                        sr.byte(), self.limits(sr)
                        self.err("imports a table %s (the module must import functions only)" % what)
                    elif kind == 2:
                        self.limits(sr)
                        self.err("imports its memory %s (it must define and export its own)" % what)
                    elif kind == 3:
                        sr.byte(), sr.byte()
                        self.err("imports a global %s (functions only)" % what)
                    elif kind == 4:
                        sr.byte(), sr.u32()
                        self.err("imports a tag %s (exception handling)" % what)
                    else:
                        raise Bad("import %s: unknown kind %d" % (what, kind))
                self.info["imports"] = None
            elif sid == 3:
                funcs = [sr.u32() for _ in range(sr.u32())]
            elif sid == 4:
                n = sr.u32()
                if n > 1:
                    self.err("%d tables (reference types)" % n)
                for _ in range(n):
                    t = sr.byte()
                    if t != FUNCREF:
                        self.err("a table of type 0x%02x (reference types)" % t)
                    self.limits(sr)
            elif sid == 5:
                for _ in range(sr.u32()):
                    memories.append(self.limits(sr))
            elif sid == 6:
                for i in range(sr.u32()):
                    t, mut = sr.byte(), sr.byte()
                    self.valtype(t, "global %d" % i)
                    self.const_expr(sr, "global %d" % i)
            elif sid == 7:
                for _ in range(sr.u32()):
                    nm = sr.name()
                    kind, idx = sr.byte(), sr.u32()
                    if nm in exports:
                        self.err("two exports named %s" % nm)
                    exports[nm] = (kind, idx)
            elif sid == 8:
                self.warn("a start function (the host calls _initialize; a start function runs at instantiation)")
            elif sid == 9:
                for i in range(sr.u32()):
                    flags = sr.u32()
                    if flags != 0:
                        raise Bad("element segment %d: flags %d (only the MVP's active segments: wasm3 0.5.0)" % (
                            i, flags))
                    self.const_expr(sr, "element segment %d" % i)
                    for _ in range(sr.u32()):
                        sr.u32()
            elif sid == 10:
                n = sr.u32()
                if n != len(funcs):
                    raise Bad("the code section has %d bodies for %d functions" % (n, len(funcs)))
                for i in range(n):
                    sz = sr.u32()
                    try:
                        self.code(R(b, sr.at, sr.at + sz), len(imports_f) + i)
                    except (Bad, IndexError) as ex:     # (the rest of that body undecoded; the next one checked)
                        self.err(str(ex) if isinstance(ex, Bad) else "function %d: truncated" % (len(imports_f) + i))
                    sr.at += sz
            elif sid == 11:
                for i in range(sr.u32()):
                    flags = sr.u32()
                    if flags == 1:
                        self.err("data segment %d is passive (memory.init: wasm3 0.5.0 does not run it)" % i)
                    elif flags != 0:
                        self.err("data segment %d: flags %d (only active segments of memory 0, the MVP form)" % (
                            i, flags))
                        if flags == 2:
                            sr.u32()
                    if flags != 1:
                        self.const_expr(sr, "data segment %d" % i)
                    sr.bytes(sr.u32())
            elif sid == 12:
                pass                                        # data count (bulk memory; wasm3 skips it)
            elif sid == 13:
                self.err("a tag section (exception handling)")
            else:
                self.err("an unknown section id %d" % sid)
            r.at = e
        rank = {sid: i for i, sid in enumerate((1, 2, 3, 4, 5, 13, 6, 7, 8, 9, 12, 10, 11))}
        known = [rank.get(s, 99) for s in order if s != 0]
        if known != sorted(set(known)):
            self.err("sections out of order or repeated: %s" % [s for s in order if s != 0])

        # the memory (section 6)
        if len(memories) != 1:
            self.err("%d memories defined (exactly one)" % len(memories))
        else:
            flags, initial, mx = memories[0]
            if flags & 2:
                self.err("the memory is shared (threads)")
            if flags & 4:
                self.err("the memory is 64-bit (memory64)")
            if mx is None:
                self.err("the memory has no maximum (fixed: initial = maximum)")
            elif mx != initial:
                self.err("the memory's maximum (%d pages) is not its initial size (%d pages)" % (mx, initial))
            if initial * 65536 > MAX_MEMORY:
                self.err("the memory is %.1f MiB (at most 32)" % (initial * 65536 / 1048576))
            self.info["memory"] = initial * 65536
        # the exports (section 2)
        nimp = len(imports_f)

        def ftype(idx):
            ti = imports_f[idx] if idx < nimp else (funcs[idx - nimp] if idx - nimp < len(funcs) else None)
            return types[ti] if ti is not None and ti < len(types) else None

        if exports.get("memory", (None,))[0] != 2:
            self.err("no memory exported as \"memory\"")
        for nm, (na, nr) in EXPORTS.items():
            if nm not in exports:
                self.err("no export %s" % nm)
                continue
            kind, idx = exports[nm]
            want = ((I32,) * na, (I32,) * nr)
            if kind != 0:
                self.err("the export %s is not a function" % nm)
            elif ftype(idx) != want:
                self.err("the export %s has the signature %s, not %s" % (nm, sig(ftype(idx)), sig(want)))
        if "_initialize" in exports:
            kind, idx = exports["_initialize"]
            if kind != 0 or ftype(idx) != ((), ()):
                self.err("the export _initialize is not a function () -> ()")
        self.info["exports"] = len(exports)

        # the custom sections (sections 1 and 4)
        if "rfm1" not in custom:
            self.err("no rfm1 custom section: not an .rfm1 (the manifest, section 4)")
        else:
            self.manifest(custom["rfm1"])
        if "rfm1.credits" in custom:
            try:
                custom["rfm1.credits"].decode("utf-8")
            except UnicodeDecodeError:
                self.err("rfm1.credits is not UTF-8")
        lic = sorted(int(n[len("rfm1.license."):]) for n in custom
                     if re.fullmatch(r"rfm1\.license\.\d+", n))
        if lic != list(range(len(lic))):
            self.warn("the licence sections are numbered %s (0, 1, .. expected)" % lic)
        for n in lic:
            raw = custom["rfm1.license.%d" % n]
            try:
                t = raw.decode("utf-8")
            except UnicodeDecodeError:
                self.err("rfm1.license.%d is not UTF-8" % n)
                continue
            if not t.split("\n", 1)[0].strip():
                self.err("rfm1.license.%d: its first line (the licence's name) is empty" % n)
        self.info["licenses"] = len(lic)
        for n in custom:
            if n.startswith("rfm1.") and n != "rfm1.credits" and not re.fullmatch(r"rfm1\.license\.\d+", n):
                self.warn("an unknown section %s" % n)
        if "target_features" in custom:
            tf = R(custom["target_features"])
            try:
                for _ in range(tf.u32()):
                    prefix = chr(tf.byte())
                    feat = tf.name()
                    if prefix in "+=" and feat not in FEATURES_OK:
                        self.err("target_features: %s%s (outside the format's feature set)" % (prefix, feat))
            except Bad:
                self.warn("target_features: unreadable")

    def manifest(self, raw):
        try:
            m = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as e:
            self.err("the manifest is not UTF-8 JSON: %s" % e)
            return
        if not isinstance(m, dict):
            self.err("the manifest is not a JSON object")
            return
        self.info["manifest"] = m
        for k in ("rfm1", "abi", "id", "name", "version", "license", "source_url"):
            if k not in m:
                self.err("the manifest has no \"%s\"" % k)
        if m.get("rfm1", 1) != 1:
            self.err("the manifest's rfm1 is %r (this checker knows version 1)" % m.get("rfm1"))
        if m.get("abi", 6) != 6:
            self.err("the manifest's abi is %r (version 1: 6)" % m.get("abi"))
        for k in ("id", "name", "version", "license", "source_url"):
            if k in m and (not isinstance(m[k], str) or not m[k].strip()):
                self.err("the manifest's %s is not a non-empty string" % k)
        if isinstance(m.get("id"), str) and not re.fullmatch(r"[a-z0-9_]{1,31}", m["id"]):
            self.err("the manifest's id %r is not [a-z0-9_]{1,31}" % m["id"])
        if isinstance(m.get("license"), str) and not re.fullmatch(r"[A-Za-z0-9.+\-() ]+", m["license"]):
            self.warn("the manifest's license %r does not look like an SPDX expression" % m["license"])
        su = m.get("source_url")
        if isinstance(su, str) and su:
            if not re.match(r"https?://", su):
                self.warn("the manifest's source_url %r is not an http(s) URL" % su)
            if not re.search(r"@[0-9a-f]{7,40}$", su):
                self.warn("the manifest's source_url %r does not end in @<commit>" % su)
        if "authors" in m and (not isinstance(m["authors"], list) or
                               not all(isinstance(a, str) and a.strip() for a in m["authors"])):
            self.err("the manifest's authors is not a list of names")
        if "built_by" in m and not isinstance(m["built_by"], str):
            self.err("the manifest's built_by is not a string")
        if "memory_mib" in m:
            mm = m["memory_mib"]
            if not isinstance(mm, (int, float)) or isinstance(mm, bool):
                self.err("the manifest's memory_mib is not a number")
            elif "memory" in self.info and abs(mm * 1048576 - self.info["memory"]) > 0.5:
                self.err("the manifest's memory_mib (%s) is not the module's memory (%.2f MiB)" % (
                    mm, self.info["memory"] / 1048576))


def sig(t):
    if t is None:
        return "(none)"
    return "(%s) -> (%s)" % (", ".join(VT_NAME.get(x, "?") for x in t[0]), ", ".join(VT_NAME.get(x, "?") for x in t[1]))


def main(argv):
    quiet = "--quiet" in argv
    paths = [a for a in argv if a != "--quiet"]
    if not paths or any(p.startswith("-") for p in paths):
        print("usage: rfm1_check.py [--quiet] <file.rfm1>..", file=sys.stderr)
        return 2
    bad = False
    for p in paths:
        c = Check(p)
        c.run()
        for w in c.warnings:
            print("%s: warning: %s" % (p, w))
        for e in c.errors:
            print("%s: error: %s" % (p, e))
        if c.errors:
            bad = True
            print("%s: FAILED (%d errors)" % (p, len(c.errors)))
        elif not quiet:
            m = c.info.get("manifest", {})
            print("%s: ok: %s %s (%s), %d bytes, %g MiB memory, %d exports, %d licence texts%s" % (
                p, m.get("id"), m.get("version"), m.get("license"), c.size, c.info.get("memory", 0) / 1048576,
                c.info.get("exports", 0), c.info.get("licenses", 0),
                ", %d warnings" % len(c.warnings) if c.warnings else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
