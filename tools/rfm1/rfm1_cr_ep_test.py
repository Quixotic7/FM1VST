#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ctest rfm1_cr_ep: ChoralRoot's engine parameters (ep1) in its .rfm1, through rfm1_run (wasm3) as a host drives them.

  - the record and description after init (rfm1_run --ep-info: 2 layers, 7 engines, the blob checked by rfm1_check's
    validator);
  - the holds: LOCK, SET while held, FREE, the firmware's own edit during a hold, FREE_ALL, an engine switch;
  - a SET on the chord layer's engine lands (the record's value and the watched cell's words follow), a SET of a pid that
    is not on the layer's engine now and of an unknown pid answers -1, a SET produces no readout (rd1);
  - the audio of a run whose host reads the record after every frame (--ep, a page watched) is bit-identical to a run
    that never calls rfm1_ep.

  rfm1_cr_ep_test.py --run <rfm1_run> --file <choralroot.rfm1>
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import rfm1_check  # noqa: E402

fails = []


def check(c, what):
    if not c:
        fails.append(what)
        print("FAIL:", what)


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, timeout=600)
    return r.returncode, r.stdout, r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--file", required=True)
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as d:
        blob = os.path.join(d, "desc.bin")
        rc, out, err = run([a.run, "--ep-info", "--out", blob, a.file])
        check(rc == 0, "rfm1_run --ep-info: %d %s" % (rc, err[-300:]))
        import json
        info = json.loads(out)
        check(info.get("nlayers") == 2 and info.get("nengines") == 7, "2 layers, 7 engines: %r" % info)
        with open(blob, "rb") as f:
            b = f.read()
        for e in rfm1_check.ep1_blob_errors(b, info.get("desc_len")):
            check(False, "the description: " + e)
        print("description: %(desc_len)d bytes, %(nengines)d engines, %(npages)d pages, %(nparams)d params" % info)

        # the chord layer plays FM6 after a power-on (its OP 1 page is page 0): Level (pid 1) and Mode (pid 5)
        rc, out, err = run([a.run, "--ep", "--readouts", "--seconds", "1", "--ep-watch", "0:0@4500",
                            "--ep-set", "0:1=50@4600", "--ep-set", "0:5=1@4700", "--ep-set", "0:288=40@4800",
                            "--ep-set", "1:1=40@4850", "--ep-set", "0:9999=1@4900", a.file])
        check(rc == 0, "rfm1_run --ep: %d %s" % (rc, err[-300:]))
        check("ep-layers@0: 0:0/0 1:-1/1" in out, "the layers after init: chord FM6 on channel 1, bass off on 2")
        check("ep-set@4600: layer 0 pid 1 = 50 -> 0" in out and "ep-values@4600: layer 0 1=50" in out,
              "a SET of OP 1 Level lands in the record")
        check(re.search(r'ep-cells@4600: page 0 layer 0: 1:"Level"="50"', out) is not None, "the watched cell follows")
        check(re.search(r'ep-cells@4700: .*5:"Mode"="FIXED"', out) is not None and '2:"Coarse"="10Hz"' in out,
              "Mode Fixed: the dynamic Coarse cell's words follow (fm6_ddesc)")
        check("ep-set@4800: layer 0 pid 288 = 40 -> -1" in out, "a VA pid on the FM6 layer: -1")
        check("ep-set@4850: layer 1 pid 1 = 40 -> -1" in out, "a SET on the bass layer while it is off: -1")
        check("ep-set@4900: layer 0 pid 9999 = 1 -> -1" in out, "an unknown pid: -1")
        check(not re.search(r"readout@4[6-9]\d\d:", out), "a SET pushes no readout")

        # the holds (SPEC.md 5): LOCK keeps the base, SET while held moves the base only, FREE writes it back; the
        # firmware's own edit during a hold (CC 16, the OSC block's first cell: OP 1 Level) becomes the base (FREE does
        # nothing); FREE_ALL; an engine switch drops the holds (OPT + PRESETS: FM6 -> FM TONE); a LOCK writes no edit
        rc, out, err = run([a.run, "--ep", "--keys", "none", "--min-rms", "0", "--seconds", "4",
                            "--ep-lock", "0:1=10@4600", "--ep-set", "0:1=70@4700", "--ep-free", "0:1@4800",
                            "--ep-lock", "0:1=20@4900", "--midi", "B01030@5000", "--ep-free", "0:1@5100",
                            "--ep-lock", "0:2=5@5200", "--ep-lock", "0:3=9@5210", "--ep-free-all", "0@5300",
                            "--ep-free", "0:4@5350", "--ep-lock", "0:5=1@5400", "--press", "GLO@6000+900",
                            "--turn", "PRESETS@6300:1", "--ep-lock", "1:1=5@7000", "--ep-free-all", "255@7100", a.file])
        check(rc == 0, "rfm1_run --ep-lock: %d %s" % (rc, err[-300:]))
        vals = {}
        for m in re.finditer(r"ep-values@(\d+): layer 0 (.*)", out):
            for kv in m.group(2).split():
                k, v = kv.split("=")
                vals.setdefault(int(m.group(1)), {})[int(k)] = v
        check(vals.get(4600, {}).get(1) == "10", "LOCK: the locked value (OP 1 Level 10)")
        check(4700 not in vals, "SET while held: the base only, the locked value stays")
        check(vals.get(4800, {}).get(1) == "70", "FREE: the base written back, the SET's 70")
        check(vals.get(5010, {}).get(1) == "37", "the firmware's own edit (CC 16 = 48) during a hold: 37")
        check(5100 not in vals, "FREE after the firmware's own edit: nothing written (the firmware's value is the base)")
        check(vals.get(5300, {}).get(2) == "1" and vals.get(5300, {}).get(3) == "0",
              "FREE_ALL: every hold of the layer back to its base")
        check("ep-free@5350: layer 0 pid 4 = 0 -> 0" in out and 5350 not in vals, "FREE of a pid not held: nothing")
        check(re.search(r"ep-layers@\d+: 0:1/0", out) is not None, "OPT + PRESETS: the chord on FM TONE")
        check("ep-lock@7000: layer 1 pid 1 = 5 -> -1" in out, "a LOCK on the bass while it is off: -1")
        check("ep-free-all@7100: layer 255 pid 0 = 0 -> 0" in out, "FREE_ALL on every layer (255)")
        check(not re.search(r"ep-values@7\d\d\d: layer 0 5=0", out), "the engine switch dropped the hold: no write back")

        wa, wb = os.path.join(d, "a.wav"), os.path.join(d, "b.wav")
        rc1, _, _ = run([a.run, "--seconds", "3", "--wav", wa, a.file])
        rc2, _, _ = run([a.run, "--seconds", "3", "--ep", "--ep-watch", "0:0@100", "--wav", wb, a.file])
        check(rc1 == 0 and rc2 == 0, "the two runs")
        with open(wa, "rb") as f1, open(wb, "rb") as f2:
            check(f1.read() == f2.read(), "the audio with the record read after every frame is bit-identical")
    print("rfm1_cr_ep: %s" % ("%d FAILED" % len(fails) if fails else "all passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
