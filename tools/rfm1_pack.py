#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""rfm1_pack: a firmware's WebAssembly module made into an .rfm1 file (docs/RFM1-FORMAT.md, version 1).

Adds the custom sections the format names, at the module's end: `rfm1` (the manifest, JSON), `rfm1.credits` (the
descriptor's credits, when it has any) and `rfm1.license.<n>` (each licence text, its first line the licence's name).
Any rfm1* sections the input already has are replaced, so packing twice gives the same file.

The manifest's id, name, version, licence and source_url come from the descriptor, as the module itself reports it
(`rfm1_run --describe module.wasm`), so the two never disagree; options override them. Python 3 standard library only.

    rfm1_pack.py --wasm felucca.wasm --describe felucca.describe.json --out felucca-1.6.1.rfm1 \\
        --author "Leo Kuroshita (Hügelton Instruments)" \\
        --license-file "GPL-3.0-only ({name})=cores/Felucca/LICENSE" --built-by "FM1VST 0.6.2 fm1_add_rfm1, emcc 6.0.13"

--license-file NAME=PATH: NAME is the section's first line ("{name}" is replaced by the firmware's name); texts that are
the same (whitespace aside) are carried once, under their names joined by "; ".
"""
import argparse
import json
import os
import re
import sys

MAGIC = b"\0asm\x01\0\0\0"


def leb_u32(b, at):
    r = sh = 0
    while True:
        x = b[at]
        at += 1
        r |= (x & 0x7F) << sh
        if not x & 0x80:
            return r, at
        sh += 7
        if sh > 35:
            raise ValueError("bad LEB128")


def enc_u32(v):
    out = bytearray()
    while True:
        x = v & 0x7F
        v >>= 7
        if v:
            out.append(x | 0x80)
        else:
            out.append(x)
            return bytes(out)


def sections(b):
    """(id, payload start, payload end, section start) for each section"""
    at = 8
    while at < len(b):
        sid = b[at]
        start = at
        size, at = leb_u32(b, at + 1)
        if at + size > len(b):
            raise ValueError("a section runs past the end of the file")
        yield sid, at, at + size, start
        at += size


def custom_name(b, s, e):
    n, q = leb_u32(b, s)
    return b[q:q + n].decode("utf-8", "replace"), q + n


def custom_section(name, payload):
    nb = name.encode("utf-8")
    body = enc_u32(len(nb)) + nb + payload
    return b"\0" + enc_u32(len(body)) + body


def memory_bytes(b):
    """the module's own memory's initial size, bytes (None if it has none)"""
    for sid, s, e, _ in sections(b):
        if sid == 5:
            count, q = leb_u32(b, s)
            if count < 1:
                return None
            flags, q = leb_u32(b, q)
            initial, q = leb_u32(b, q)
            return initial * 65536
    return None


def norm(t):
    return " ".join(t.split())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--wasm", required=True, help="the module (emcc's output)")
    ap.add_argument("--describe", required=True, help="the descriptor as JSON (rfm1_run --describe)")
    ap.add_argument("--out", required=True, help="the .rfm1 to write")
    ap.add_argument("--author", action="append", default=[], help="an author (repeat)")
    ap.add_argument("--license-file", action="append", default=[], metavar="NAME=PATH")
    ap.add_argument("--credits-file", help="the credits text (default: the descriptor's credits)")
    ap.add_argument("--built-by", default="")
    for k in ("id", "name", "version", "license", "source-url"):
        ap.add_argument("--" + k, help="override the descriptor's " + k)
    a = ap.parse_args()

    with open(a.wasm, "rb") as f:
        b = f.read()
    if b[:8] != MAGIC:
        sys.exit("rfm1_pack: %s: not a WebAssembly module (version 1)" % a.wasm)
    with open(a.describe, encoding="utf-8") as f:
        d = json.load(f)

    # the module without its old rfm1 sections
    keep = bytearray(b[:8])
    for sid, s, e, start in sections(b):
        if sid == 0:
            name, _ = custom_name(b, s, e)
            if name == "rfm1" or name.startswith("rfm1."):
                continue
        keep += b[start:e]

    mem = memory_bytes(b)
    if mem is None:
        sys.exit("rfm1_pack: %s: the module defines no memory" % a.wasm)
    man = {
        "rfm1": 1,
        "abi": d.get("abi", 6),
        "id": a.id or d["id"],
        "name": a.name or d["name"],
        "version": a.version or d["version"],
        "license": a.license or d.get("license") or "",
        "source_url": a.source_url or d.get("source_url") or "",
    }
    if a.author:
        man["authors"] = a.author
    if a.built_by:
        man["built_by"] = a.built_by
    man["memory_mib"] = mem / 1048576 if mem % 1048576 else mem // 1048576
    if not re.fullmatch(r"[a-z0-9_]{1,31}", man["id"]):
        sys.exit("rfm1_pack: the id %r is not [a-z0-9_]{1,31}" % man["id"])
    for k in ("license", "source_url"):
        if not man[k]:
            sys.exit("rfm1_pack: no %s (the descriptor has none: give --%s)" % (k, k.replace("_", "-")))
    out = bytearray(keep)
    out += custom_section("rfm1", json.dumps(man, ensure_ascii=False, indent=2).encode("utf-8"))

    credits = d.get("credits")
    if a.credits_file:
        with open(a.credits_file, encoding="utf-8") as f:
            credits = f.read()
    if credits and credits.strip():
        out += custom_section("rfm1.credits", credits.strip().encode("utf-8") + b"\n")

    texts = []                                 # [names, text]
    for spec in a.license_file:
        if "=" not in spec:
            sys.exit("rfm1_pack: --license-file %r: NAME=PATH" % spec)
        name, path = spec.split("=", 1)
        name = name.replace("{name}", man["name"])
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read().strip()
        for t in texts:
            if norm(t[1]) == norm(text):
                t[0].append(name)
                break
        else:
            texts.append([[name], text])
    for i, (names, text) in enumerate(texts):
        out += custom_section("rfm1.license.%d" % i, ("; ".join(names) + "\n\n" + text + "\n").encode("utf-8"))

    tmp = a.out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, a.out)
    print("rfm1_pack: %s: %s %s, %d bytes (%d licence texts%s)" % (
        a.out, man["id"], man["version"], len(out), len(texts), ", credits" if credits else ""))


if __name__ == "__main__":
    main()
