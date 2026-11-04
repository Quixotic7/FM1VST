#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A firmware's RemoteFM1Metadata.json labels (tools/rfm1_labels.py) checked, then compiled into
core-glue/common/rfm1_shim.c with a stand-in core (tools/rfm1/labels_fixture_core.c), with and without live labels
of its own, and read back with rfm1_run --describe --labels. Exits 1 on a failure.

    python3 tools/rfm1/rfm1_labels_test.py [--emcc /path/to/emcc --run build/rfm1_run]

Without --emcc / --run only the checks of the metadata run.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import rfm1_labels  # noqa: E402

FAILS = []
FEATURES = ["-mcpu=mvp", "-msign-ext", "-mnontrapping-fptoint", "-mbulk-memory", "-mmutable-globals"]
CFLAGS = FEATURES + ["-O3", "-std=gnu11", "-ffp-contract=off", "-fno-common", "-DFM1CORE_RFM1=1",
                     "-I" + os.path.join(ROOT, "core-api")]
LINK = FEATURES + ["-O3", "--no-entry", "-sSTANDALONE_WASM", "-sFILESYSTEM=0", "-sINITIAL_MEMORY=1048576",
                   "-sALLOW_MEMORY_GROWTH=0", "-sEXPORTED_FUNCTIONS=[]", "-Wl,--gc-sections"]

META = {"labels": {
    "buttons": {"FX": "DRUMS", "sel": "TRACK", "OCT+": "", "SEQ": "steps", "ARP": "ARPEGGIATOR", "REC": "RÉC",
                "TURBO": "GO"},
    "knobs": {"K1": "CUTOFF", "MASTER": "VOL", "K9": "X"}}}


def ok(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def header(tmp, meta, name):
    d = os.path.join(tmp, name)
    os.makedirs(d)
    mp = os.path.join(d, "RemoteFM1Metadata.json")
    with open(mp, "w", encoding="utf-8") as f:
        f.write(meta if isinstance(meta, str) else json.dumps(meta))
    r = subprocess.run([sys.executable, os.path.join(TOOLS, "rfm1_labels.py"), "header", "--metadata", mp,
                        "--out", os.path.join(d, "rfm1_meta_labels.h")], capture_output=True, text=True)
    return d, r


def build(a, tmp, name, live, meta_dir):
    objs = []
    for src, extra in ((os.path.join(HERE, "labels_fixture_core.c"), ["-DLIVE=%d" % live]),
                       (os.path.join(ROOT, "core-glue", "common", "rfm1_shim.c"),
                        ["-DRFM1_META=1", "-I" + meta_dir] if meta_dir else [])):
        o = os.path.join(tmp, "%s_%s.o" % (name, os.path.basename(src)[:-2]))
        subprocess.run([a.emcc] + CFLAGS + extra + ["-c", src, "-o", o], check=True)
        objs.append(o)
    w = os.path.join(tmp, name + ".wasm")
    subprocess.run([a.emcc] + LINK + objs + ["-o", w], check=True)
    d = json.loads(subprocess.run([a.run, "--describe", "--labels", w], capture_output=True, text=True,
                                  check=True).stdout)
    with open(w, "rb") as f:
        return d, f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emcc")
    ap.add_argument("--run")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="rfm1_labels_test") as tmp:
        # the checks: each bad entry a warning and skipped, the rest kept
        warns = []
        d, r = header(tmp, META, "meta")
        lb = rfm1_labels.from_metadata(os.path.join(d, "RemoteFM1Metadata.json"), warns.append)
        ok(r.returncode == 0, "header written")
        ok(lb == {"buttons": {0: "DRUMS", 1: "TRACK", 13: ""}, "knobs": {3: "CUTOFF", 7: "VOL"}},
           "the good entries kept (any-case names, \"\" = no job): %s" % lb)
        for what in ("'steps' is not upper case", "'ARPEGGIATOR' is 11 characters", "'RÉC' is not printable ASCII",
                     "unknown button 'TURBO'", "unknown knob 'K9'"):
            ok(any(what in w for w in warns), "warned: %s" % what)
        ok(rfm1_labels.summary(open(os.path.join(d, "rfm1_meta_labels.h"), "rb").read()) == "3 buttons, 2 knobs",
           "the tag's counts")
        w2 = []
        ok(rfm1_labels.from_metadata(header(tmp, "{ nope", "badjson")[0] + "/RemoteFM1Metadata.json", w2.append)
           is None and len(w2) == 1 and "not valid JSON" in w2[0], "not JSON: one warning, no labels")
        w3 = []
        ok(rfm1_labels.from_metadata(header(tmp, {"skin": {}}, "nolabels")[0] + "/RemoteFM1Metadata.json",
                                     w3.append) is None and not w3, "no labels field: nothing, no warning")
        none_dir = header(tmp, {"skin": {}}, "none")[0]

        if a.emcc and a.run:
            plain, plain_w = build(a, tmp, "plain", 0, None)
            meta0, meta0_w = build(a, tmp, "meta0", 0, none_dir)
            ok(meta0_w == plain_w, "a metadata file without labels: the module byte for byte as without one")
            ok(plain["labels_epoch"] == 0 and plain["labels"] == [None] * 49 and
               plain["button_names"][:3] == ["GFX", None, "GENV"], "no live labels, no metadata: as before")
            m, mw = build(a, tmp, "meta", 0, d)
            want = [None] * 49
            want[3], want[7], want[8], want[9], want[21] = "CUTOFF", "VOL", "DRUMS", "TRACK", ""
            ok(m["labels_epoch"] != 0 and m["labels"] == want, "no live labels + metadata: the static table served")
            bn = m["button_names"]
            ok(bn[0] == "DRUMS" and bn[1] == "TRACK" and bn[2] == "GENV" and bn[13] == "" and bn[3] is None,
               "button_names: the author's entries over the glue's, the rest the glue's: %s" % bn)
            ok(rfm1_labels.summary(mw) == "3 buttons, 2 knobs", "the module's data has the tag (rfm1_check's line)")
            live, _ = build(a, tmp, "live", 1, None)
            lm, _ = build(a, tmp, "livemeta", 1, d)
            ok(live["labels_epoch"] == 7 and live["labels"][8] == "LIVE" and live["labels"][3] == "LCUT",
               "a core with live labels, no metadata: its own")
            ok(lm["labels_epoch"] != 0 and lm["labels"] == live["labels"],
               "a core with live labels + metadata: its labels untouched (%s)" % [x for x in lm["labels"] if x])
            ok(lm["button_names"][0] == "DRUMS", "... its button_names still the author's (the static fallback)")
        else:
            print("(no --emcc / --run: the module tests skipped)")
    print("%d failures" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
