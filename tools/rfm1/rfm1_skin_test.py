#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A firmware's RemoteFM1Metadata.json skin through rfm1_pack.py and rfm1_check.py's skin checks (tools/rfm1_skin.py),
on a fixture repository made in a temporary folder and a stand-in module (no emcc needed). Exits 1 on a failure.

    python3 tools/rfm1/rfm1_skin_test.py
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
import rfm1_skin  # noqa: E402

FAILS = []


def ok(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def png(w, h, rgb=(200, 80, 40), pad=0):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(rgb) * w for _ in range(h))
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    if pad:
        out += chunk(b"tEXt", b"pad\0" + b"x" * pad)
    return out + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def jpeg_header(w, h):
    # SOI, an APP0, SOF0 with the size (enough for the size reader; not a decodable picture)
    app0 = b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\0\1\1\0\0\1\0\1\0\0"
    sof = b"\xff\xc0" + struct.pack(">HBHHB", 11, 8, h, w, 1) + b"\1\x11\0"
    return b"\xff\xd8" + app0 + sof + b"\xff\xd9"


def module():
    mem = b"\1" + b"\1" + rfm1_skin.leb(128) + rfm1_skin.leb(128)          # one memory, 8 MiB fixed
    return b"\0asm\1\0\0\0" + b"\5" + rfm1_skin.leb(len(mem)) + mem


THEME5 = {"body": "#1B1B22", "key": "#2C2C36", "knob": "#E8E2D0", "led": "#FF7A2F", "bezel": "#0E0E12"}


def pack(tmp, meta, files=None, raw=None):
    """a fixture repo with this metadata (dict, or raw text) and files; rfm1_pack's (rc, out bytes, stderr)"""
    repo = tempfile.mkdtemp(dir=tmp)
    for rel, data in (files or {}).items():
        p = os.path.join(repo, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
    mp = os.path.join(repo, rfm1_skin.METADATA)
    with open(mp, "w", encoding="utf-8") as f:
        f.write(raw if raw is not None else json.dumps(meta))
    wasm, desc, out = (os.path.join(tmp, n) for n in ("m.wasm", "m.json", "m.rfm1"))
    with open(wasm, "wb") as f:
        f.write(module())
    with open(desc, "w") as f:
        json.dump({"id": "skintest", "name": "Skin test", "version": "1.0", "license": "MIT",
                   "source_url": "https://example.com/x@0123456789ab"}, f)
    base = [sys.executable, os.path.join(TOOLS, "rfm1_pack.py"), "--wasm", wasm, "--describe", desc, "--out", out]
    r = subprocess.run(base + ["--skin-metadata", mp], capture_output=True, text=True)
    with open(out, "rb") as f:
        b = f.read()
    r0 = subprocess.run(base, capture_output=True, text=True)
    with open(out, "rb") as f:
        b0 = f.read()
    return r.returncode, b, b0, r.stderr, repo


def check(b):
    errs, warns = [], []
    summary = rfm1_skin.check_sections(rfm1_skin.skin_sections(b), errs.append, warns.append)
    return summary, errs, warns


def main():
    with tempfile.TemporaryDirectory(prefix="rfm1_skin_test") as tmp:
        return run(tmp)


def run(tmp):
    portrait, landscape = png(90, 160), png(160, 90, (20, 90, 200))

    # the good case: two images, five colours, a dim
    meta = {"schema": 1, "card": {"tag": "x"},
            "skin": {"background": "art/p.png", "landscape": "art/l.png", "dim": 0.2, "theme": THEME5}}
    rc, b, b0, err, _ = pack(tmp, meta, {"art/p.png": portrait, "art/l.png": landscape})
    sk = rfm1_skin.skin_sections(b)
    ok(rc == 0 and not err.strip(), "good: packs without a warning (%r)" % err.strip())
    ok(list(sk) == [rfm1_skin.SKIN, rfm1_skin.BACKGROUND, rfm1_skin.LANDSCAPE], "good: the three sections, in order")
    ok(json.loads(sk.get(rfm1_skin.SKIN, b"{}")) == {"skin": 1, "dim": 0.2, "theme": THEME5}, "good: rfm1.skin's JSON")
    ok(sk.get(rfm1_skin.BACKGROUND) == portrait and sk.get(rfm1_skin.LANDSCAPE) == landscape,
       "good: the images byte for byte")
    ok(rfm1_skin.without_skin(b) == b0 and b.startswith(b0), "good: only the skin sections added, at the end")
    s, e, w = check(b)
    ok(not e and not w and s == "dim 0.2, theme 5 roles, background 90x160 PNG, landscape 160x90 PNG",
       "good: rfm1_check's skin: %s %s %s" % (s, e, w))

    # all fourteen roles
    t14 = {k: "#102030" for k in rfm1_skin.ROLES}
    rc, b, b0, err, _ = pack(tmp, {"skin": {"theme": t14}})
    ok(json.loads(rfm1_skin.skin_sections(b)[rfm1_skin.SKIN]) == {"skin": 1, "theme": t14} and not err.strip(),
       "fourteen roles: kept")

    # a JPEG
    rc, b, b0, err, _ = pack(tmp, {"skin": {"background": "bg.jpg"}}, {"bg.jpg": jpeg_header(1200, 2000)})
    ok(check(b)[0] == "background 1200x2000 JPEG" and not err.strip(), "a JPEG background")

    # the bad cases: each a warning, the pack succeeds, the rest used
    def bad(name, meta, files, expect_warn, expect_sections, raw=None):
        rc, b, b0, err, _ = pack(tmp, meta, files, raw)
        names = list(rfm1_skin.skin_sections(b))
        s, e, w = check(b)
        ok(rc == 0 and expect_warn in err and names == expect_sections and not e,
           "%s: rc %d, warning %r, sections %s%s" % (name, rc, err.strip().splitlines(), names,
                                                     " check errors %s" % e if e else ""))
        if not expect_sections:
            ok(b == b0, "%s: the file as without the metadata, byte for byte" % name)

    bad("invalid JSON", None, {}, "not valid JSON", [], raw="{ skin: ")
    bad("a path with ..", {"skin": {"background": "../outside.png", "theme": THEME5}}, {},
        "has \"..\"", [rfm1_skin.SKIN])
    bad("an absolute path", {"skin": {"background": "/etc/hosts", "theme": THEME5}}, {}, "is absolute",
        [rfm1_skin.SKIN])
    bad("a missing image", {"skin": {"background": "nope.png", "landscape": "l.png"}}, {"l.png": landscape},
        "'nope.png' not found", [rfm1_skin.SKIN, rfm1_skin.LANDSCAPE])
    bad("a 5 MiB image", {"skin": {"background": "big.png", "theme": THEME5}},
        {"big.png": png(8, 8, pad=5 << 20)}, "4 MiB", [rfm1_skin.SKIN])
    bad("a 5000 px image", {"skin": {"background": "wide.png", "theme": THEME5}},
        {"wide.png": png(5000, 2)}, "longer side at most 4096", [rfm1_skin.SKIN])
    bad("not an image", {"skin": {"background": "x.png", "theme": THEME5}}, {"x.png": b"GIF89a...."},
        "not a PNG or JPEG", [rfm1_skin.SKIN])
    bad("a bad hex colour", {"skin": {"background": "p.png", "theme": dict(THEME5, led="#FF7A2")}},
        {"p.png": portrait}, "led is '#FF7A2'", [rfm1_skin.SKIN, rfm1_skin.BACKGROUND])
    bad("dim 2", {"skin": {"background": "p.png", "dim": 2}}, {"p.png": portrait}, "skin.dim: 2",
        [rfm1_skin.SKIN, rfm1_skin.BACKGROUND])
    bad("only a dim", {"skin": {"dim": 0.3}}, {}, "no usable image and no theme", [])
    bad("an unknown field", {"skin": {"theme": THEME5, "sparkle": 1}}, {}, "unknown field 'sparkle'",
        [rfm1_skin.SKIN])

    # a symlink out of the repository
    outside = os.path.join(tmp, "outside.png")
    with open(outside, "wb") as f:
        f.write(portrait)
    rc, b, b0, err, repo = pack(tmp, {"skin": {"theme": THEME5}})
    os.symlink(outside, os.path.join(repo, "link.png"))
    with open(os.path.join(repo, rfm1_skin.METADATA), "w") as f:
        json.dump({"skin": {"background": "link.png", "theme": THEME5}}, f)
    warns = []
    s = rfm1_skin.from_metadata(os.path.join(repo, rfm1_skin.METADATA), warns.append)
    ok(s and s["background"] is None and any("symlink" in x for x in warns), "a symlink out: skipped (%s)" % warns)

    # no skin in the file: nothing added
    rc, b, b0, err, _ = pack(tmp, {"schema": 1, "card": {"tag": "x"}})
    ok(rc == 0 and b == b0 and not err.strip(), "a metadata file without a skin: the file unchanged")

    # rfm1_check on a broken skin: errors
    broken = module() + rfm1_skin.custom(rfm1_skin.SKIN, b'{"skin":2,"dim":3,"theme":{"body":"red"}}') + \
        rfm1_skin.custom(rfm1_skin.BACKGROUND, b"nope")
    s, e, w = check(broken)
    ok(len(e) == 5, "check: a broken skin's errors: %s" % e)

    print("%d failures" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
