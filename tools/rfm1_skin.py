#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""rfm1_skin: an .rfm1's skin (docs/RFM1-FORMAT.md section 8): add one to a built file, show one, or read a
firmware's RemoteFM1Metadata.json skin (docs/RFM1-METADATA.md). Python 3 standard library only.

  rfm1_skin.py add firmware.rfm1 -o skinned.rfm1 [--background bg.png] [--landscape bg-wide.png]
               [--theme theme.json] [--dim 0.2]
  rfm1_skin.py add firmware.rfm1 -o skinned.rfm1 --metadata <repo>/RemoteFM1Metadata.json
  rfm1_skin.py show firmware.rfm1

The skin is three custom sections at the end of the file (rfm1.skin: {"skin": 1, "dim": .., "theme": {..}};
rfm1.skin.background; rfm1.skin.background.landscape); the code is untouched. An existing skin is replaced.
theme.json: {"body": "#RRGGBB", "key": .., "knob": .., "led": .., "bezel": ..} or all fourteen roles.

A RemoteFM1Metadata.json's skin is checked field by field: a bad field is skipped with a warning, never an error
(a firmware's build must not fail on it). Its paths are relative to the file's folder (the repository's root), and
must stay inside it. (A port of RemoteFM1IOS's scripts/rfm1_skin.py, with the metadata's checks; rfm1_pack.py and
rfm1_check.py use this module.)
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

SKIN = "rfm1.skin"
BACKGROUND = SKIN + ".background"
LANDSCAPE = SKIN + ".background.landscape"
MAX_IMAGE = 4 << 20                       # bytes, each image
MAX_SIDE = 4096                           # px, the longer side (the host scales to 2048)
MAX_DIM = 0.8
METADATA = "RemoteFM1Metadata.json"
# design/tokens.json's roles (RemoteFM1IOS): a theme gives all fourteen, or the five a user picks
ROLES = ("body", "label", "bed", "key", "hi", "lo", "keyText", "knob", "mark", "bezel", "lcd", "ink", "led", "ledOff")
PICKED = ("body", "key", "knob", "led", "bezel")
HEX = re.compile(r"#[0-9A-Fa-f]{6}")


# --- WebAssembly sections ------------------------------------------------------------------------------------------
def leb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def read_leb(b, at):
    v = shift = 0
    while True:
        c = b[at]
        at += 1
        v |= (c & 0x7F) << shift
        if not c & 0x80:
            return v, at
        shift += 7
        if shift > 35:
            raise ValueError("bad LEB128")


def sections(b):
    """(id, name or None, section start, end, payload start) for each section"""
    if b[:8] != b"\0asm\1\0\0\0":
        raise ValueError("not a WebAssembly module (version 1)")
    at, out = 8, []
    while at < len(b):
        start = at
        sid = b[at]
        size, at = read_leb(b, at + 1)
        end = at + size
        if end > len(b):
            raise ValueError("a section runs past the end of the file")
        name, payload = None, at
        if sid == 0:
            n, p = read_leb(b, at)
            name = b[p:p + n].decode("utf-8", "replace")
            payload = p + n
        out.append((sid, name, start, end, payload))
        at = end
    return out


def skin_sections(b):
    """{name: payload} of the rfm1.skin* custom sections (as RemoteFM1's SkinFile.sections reads them)"""
    return {nm: b[p:e] for sid, nm, s, e, p in sections(b) if sid == 0 and (nm or "").startswith(SKIN)}


def custom(name, payload):
    n = name.encode()
    body = leb(len(n)) + n + payload
    return b"\0" + leb(len(body)) + body


def without_skin(b):
    return b[:8] + b"".join(b[s:e] for sid, nm, s, e, p in sections(b) if not (nm or "").startswith(SKIN))


# --- images and themes ---------------------------------------------------------------------------------------------
def image_info(d):
    """("PNG" | "JPEG", width, height) of an image's bytes; (kind, None, None) if its size is unreadable; None if it is
    neither a PNG nor a JPEG"""
    if d[:8] == b"\x89PNG\r\n\x1a\n":
        if len(d) >= 24 and d[12:16] == b"IHDR":
            w, h = struct.unpack(">II", d[16:24])
            return "PNG", w, h
        return "PNG", None, None
    if d[:3] == b"\xff\xd8\xff":
        at = 2
        while at + 4 <= len(d):
            if d[at] != 0xFF:
                break
            m = d[at + 1]
            if m == 0xFF:                 # fill byte
                at += 1
                continue
            if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
                at += 2
                continue
            ln = struct.unpack(">H", d[at + 2:at + 4])[0]
            if m in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                if at + 9 <= len(d):
                    h, w = struct.unpack(">HH", d[at + 5:at + 9])
                    return "JPEG", w, h
                break
            at += 2 + ln
        return "JPEG", None, None
    return None


def image_problem(d):
    """why the bytes cannot be a skin image (None if they can)"""
    if len(d) > MAX_IMAGE:
        return "%d bytes (at most %d, 4 MiB)" % (len(d), MAX_IMAGE)
    info = image_info(d)
    if info is None:
        return "not a PNG or JPEG"
    kind, w, h = info
    if w is None:
        return "a %s whose size cannot be read" % kind
    if not w or not h:
        return "a %s of %dx%d px" % (kind, w, h)
    if max(w, h) > MAX_SIDE:
        return "%dx%d px (the longer side at most %d)" % (w, h, MAX_SIDE)
    return None


def theme_problems(t):
    """(the theme to keep or None, [problems]): all fourteen roles, else the five picked ones"""
    if not isinstance(t, dict):
        return None, ["the theme is not an object of roles"]
    probs = []
    for k, v in t.items():
        if k not in ROLES:
            probs.append("theme: unknown role %r ignored" % k)
        elif not (isinstance(v, str) and HEX.fullmatch(v)):
            probs.append("theme: %s is %r, not \"#RRGGBB\"" % (k, v))
    good = {k: v for k, v in t.items() if k in ROLES and isinstance(v, str) and HEX.fullmatch(v)}
    if all(k in good for k in ROLES):
        return {k: good[k] for k in ROLES}, probs
    if all(k in good for k in PICKED):
        if len(good) > len(PICKED):
            probs.append("theme: not all fourteen roles: only the five (%s) kept" % ", ".join(PICKED))
        return {k: good[k] for k in PICKED}, probs
    miss = [k for k in PICKED if k not in good]
    probs.append("theme: %s missing or bad (need the five %s, or all fourteen roles): no theme" % (
        ", ".join(miss), ", ".join(PICKED)))
    return None, probs


def skin_json(dim=None, theme=None):
    meta = {"skin": 1}
    if dim is not None:
        meta["dim"] = dim
    if theme:
        meta["theme"] = theme
    return json.dumps(meta, separators=(",", ":")).encode()


def skin_bytes(dim=None, theme=None, background=None, landscape=None):
    out = custom(SKIN, skin_json(dim, theme))
    if background is not None:
        out += custom(BACKGROUND, background)
    if landscape is not None:
        out += custom(LANDSCAPE, landscape)
    return out


# --- RemoteFM1Metadata.json ----------------------------------------------------------------------------------------
def repo_file(root, rel, what, warn):
    """a path of the metadata, checked: relative, inside root (symlinks too), a file. None (with a warning) if not"""
    if not isinstance(rel, str) or not rel.strip():
        warn("skin.%s: not a path (a string)" % what)
        return None
    if os.path.isabs(rel) or rel.startswith(("/", "\\")) or re.match(r"[A-Za-z]:", rel):
        warn("skin.%s: %r is absolute (paths are relative to the repository's root)" % (what, rel))
        return None
    if ".." in re.split(r"[\\/]", rel):
        warn("skin.%s: %r has \"..\" (paths stay inside the repository)" % (what, rel))
        return None
    p = os.path.join(root, rel)
    real, rroot = os.path.realpath(p), os.path.realpath(root)
    if os.path.commonpath([real, rroot]) != rroot:
        warn("skin.%s: %r leads out of the repository (a symlink)" % (what, rel))
        return None
    if not os.path.isfile(real):
        warn("skin.%s: %r not found" % (what, rel))
        return None
    return real


def from_metadata(path, warn):
    """the skin of a RemoteFM1Metadata.json: dict(dim, theme, background, landscape, sources), or None (no skin, or
    nothing usable). Every problem goes to warn(message); nothing raises on bad content"""
    root = os.path.dirname(os.path.abspath(path))
    try:
        with open(path, "rb") as f:
            m = json.loads(f.read().decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as e:
        warn("not valid JSON, ignored (%s)" % e)
        return None
    if not isinstance(m, dict):
        warn("not a JSON object, ignored")
        return None
    if "schema" in m and m["schema"] != 1:
        warn("schema %r (this build knows 1): read as 1" % m["schema"])
    for k in m:
        if k not in ("$schema", "schema", "target", "card", "skin", "labels"):
            warn("unknown field %r ignored" % k)
    s = m.get("skin")
    if s is None:
        return None
    if not isinstance(s, dict):
        warn("skin: not an object, ignored")
        return None
    for k in s:
        if k not in ("background", "landscape", "dim", "theme"):
            warn("skin: unknown field %r ignored" % k)
    out = {"dim": None, "theme": None, "background": None, "landscape": None, "sources": {}}
    for k in ("background", "landscape"):
        if k not in s:
            continue
        p = repo_file(root, s[k], k, warn)
        if not p:
            continue
        with open(p, "rb") as f:
            d = f.read()
        why = image_problem(d)
        if why:
            warn("skin.%s: %s: %s: skipped" % (k, s[k], why))
            continue
        out[k] = d
        out["sources"][k] = {"path": s[k], "sha256": hashlib.sha256(d).hexdigest(), "bytes": len(d)}
    if "dim" in s:
        dv = s["dim"]
        if isinstance(dv, bool) or not isinstance(dv, (int, float)) or not 0 <= dv <= MAX_DIM:
            warn("skin.dim: %r is not a number 0 .. %g: skipped (0)" % (dv, MAX_DIM))
        else:
            out["dim"] = dv
    if "theme" in s:
        th, probs = theme_problems(s["theme"])
        for p in probs:
            warn("skin." + p)
        out["theme"] = th
    if out["background"] is None and out["landscape"] is None and not out["theme"]:
        warn("skin: no usable image and no theme: no skin")
        return None
    return out


# --- checking a file's skin (rfm1_check.py) ------------------------------------------------------------------------
def check_sections(sk, err, warn):
    """an .rfm1's rfm1.skin* sections ({name: payload}) against section 8; returns a summary ("" if none)"""
    if not sk:
        return ""
    for n in sk:
        if n not in (SKIN, BACKGROUND, LANDSCAPE):
            warn("an unknown section %s" % n)
    parts = []
    if SKIN not in sk:
        warn("skin images without an rfm1.skin section (a host ignores them)")
    else:
        try:
            o = json.loads(sk[SKIN].decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as e:
            err("rfm1.skin is not UTF-8 JSON: %s" % e)
            o = None
        if o is not None and not isinstance(o, dict):
            err("rfm1.skin is not a JSON object")
            o = None
        if o is not None:
            if o.get("skin") != 1 or isinstance(o.get("skin"), bool):
                err("rfm1.skin's skin is %r (1)" % o.get("skin"))
            for k in o:
                if k not in ("skin", "dim", "theme"):
                    warn("rfm1.skin: unknown key %r" % k)
            if "dim" in o:
                dv = o["dim"]
                if isinstance(dv, bool) or not isinstance(dv, (int, float)) or not 0 <= dv <= MAX_DIM:
                    err("rfm1.skin's dim %r is not 0 .. %g" % (dv, MAX_DIM))
                else:
                    parts.append("dim %g" % dv)
            if "theme" in o:
                th, probs = theme_problems(o["theme"])
                for p in probs:            # (a theme a host can still read, its five roles: warnings)
                    (warn if th else err)("rfm1.skin's " + p)
                if th:
                    parts.append("theme %d roles" % len(th))
            elif SKIN in sk and BACKGROUND not in sk and LANDSCAPE not in sk:
                warn("rfm1.skin has no theme and there is no image: a host shows no skin")
    for n, label in ((BACKGROUND, "background"), (LANDSCAPE, "landscape")):
        if n in sk:
            d = sk[n]
            why = image_problem(d)
            if why:
                err("%s: %s" % (n, why))
            else:
                kind, w, h = image_info(d)
                parts.append("%s %dx%d %s" % (label, w, h, kind))
    return ", ".join(parts) or "empty"


# --- the command line ----------------------------------------------------------------------------------------------
def add(a):
    def _warn(m):
        print("rfm1_skin: warning: %s: %s" % (a.metadata, m), file=sys.stderr)

    with open(a.firmware, "rb") as f:
        b = f.read()
    keep = without_skin(b)
    if a.metadata:
        s = from_metadata(a.metadata, _warn)
        if not s:
            sys.exit("rfm1_skin: %s: no usable skin" % a.metadata)
        sk = skin_bytes(s["dim"], s["theme"], s["background"], s["landscape"])
    else:
        if not (a.background or a.landscape or a.theme):
            sys.exit("give --background, --landscape, --theme or --metadata")
        imgs = {}
        for k in ("background", "landscape"):
            p = getattr(a, k)
            if p:
                with open(p, "rb") as f:
                    imgs[k] = f.read()
                why = image_problem(imgs[k])
                if why:
                    sys.exit("rfm1_skin: %s: %s" % (p, why))
        theme = None
        if a.theme:
            with open(a.theme, encoding="utf-8") as f:
                theme, probs = theme_problems(json.load(f))
            if probs or not theme:
                sys.exit("rfm1_skin: %s: %s" % (a.theme, "; ".join(probs)))
        dim = None if a.dim is None else max(0.0, min(MAX_DIM, a.dim))
        sk = skin_bytes(dim, theme, imgs.get("background"), imgs.get("landscape"))
    out = keep + sk
    with open(a.output, "wb") as f:
        f.write(out)
    print("%s: %d bytes (skin %d)" % (a.output, len(out), len(sk)))


def show(a):
    with open(a.firmware, "rb") as f:
        sk = skin_sections(f.read())
    if not sk:
        print("no skin")
    for nm, payload in sk.items():
        print("%s: %d bytes%s" % (nm, len(payload), " " + payload.decode("utf-8", "replace") if nm == SKIN else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("add")
    p.add_argument("firmware")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--background")
    p.add_argument("--landscape")
    p.add_argument("--theme")
    p.add_argument("--dim", type=float)
    p.add_argument("--metadata", help="a " + METADATA + " (its skin; paths relative to its folder)")
    s = sub.add_parser("show")
    s.add_argument("firmware")
    a = ap.parse_args()
    try:
        (add if a.cmd == "add" else show)(a)
    except ValueError as e:
        sys.exit("rfm1_skin: %s: %s" % (a.firmware, e))


if __name__ == "__main__":
    main()
