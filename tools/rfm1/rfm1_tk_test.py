#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Move takeover (docs/RFM1-TAKEOVER.md, tk1) end to end on tkdemo.rfm1 (tools/rfm1/tkdemo): the record after
init (rfm1_run --tk-info: version, size, name, claims), rfm1_check accepting the file and rejecting a module with a
partial tk1 (an export renamed away) or without tk1 in its wants, the manifest's takeover entry, an rfm1_run --tk
scenario (pads, buttons, knobs, Shift's claims, the release of what is held, the screen as PBM against the font
rendered here, so the record's screen offset and its page / bit order are checked), and the seq counters moving only
on real changes. With --cc, rfm1_move.h without FM1_HOST_TK (the FM-1's build) is compiled: no symbol of the shim
referenced. Exits 1 on a failure.

    python3 tools/rfm1/rfm1_tk_test.py --run build/rfm1_run --file build/rfm1/tkdemo-1.0.rfm1 [--cc cc]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
CHECK = os.path.join(TOOLS, "rfm1_check.py")
FAILS = []


def ok(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def font():
    """tkdemo_font.h's glyphs: char -> 5 column bytes"""
    src = open(os.path.join(HERE, "tkdemo", "tkdemo_font.h")).read()

    def rows(name):
        body = re.search(r"%s\[[^=]*=\s*\{(.*?)\};" % name, src, re.S).group(1)
        return [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", g)] for g in re.findall(r"\{([^{}]*)\}", body)
                ] if "{" in body else [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)]]
    g = {" ": [0] * 5}
    for i, c in enumerate(rows("TKD_DIGITS")):
        g[str(i)] = c
    for i, c in enumerate(rows("TKD_LETTERS")):
        g[chr(65 + i)] = c
    g["+"], g["-"], g[":"] = rows("TKD_PLUS")[0], rows("TKD_MINUS")[0], rows("TKD_COLON")[0]
    return g


def render(texts):
    """tkdemo_move.c's screen: [(x, y, scale, text)..] and the rule at y = 9 -> 64 rows of 128 '0' / '1'"""
    g = font()
    px = [[0] * 128 for _ in range(64)]
    for x0, y0, sc, t in texts:
        for k, ch in enumerate(t):
            col = g.get(ch.upper(), [0] * 5)
            for c in range(5):
                for r in range(7):
                    if col[c] >> r & 1:
                        for dy in range(sc):
                            for dx in range(sc):
                                x, y = x0 + 6 * sc * k + c * sc + dx, y0 + r * sc + dy
                                if 0 <= x < 128 and 0 <= y < 64:
                                    px[y][x] = 1
    px[9] = [1] * 128
    return ["".join(str(v) for v in row) for row in px]


def pbm(out):
    """the P1 image in rfm1_run's stdout (--tk-screen -)"""
    lines = out.splitlines()
    i = lines.index("P1")
    ok(lines[i + 1] == "128 64", "the PBM is 128 x 64")
    return lines[i + 2:i + 2 + 64]


def run(a, *args):
    return subprocess.run([a.run] + list(args) + [a.file], capture_output=True, text=True)


def check(path, a):
    return subprocess.run([sys.executable, CHECK, "--run", a.run, path], capture_output=True, text=True)


def custom_rfm1(b):
    """the manifest (the rfm1 custom section)"""
    i = b.index(b"\x04rfm1")
    j = b.index(b"{", i)
    depth = 0
    for k in range(j, len(b)):
        depth += {ord("{"): 1, ord("}"): -1}.get(b[k], 0)
        if depth == 0:
            return json.loads(b[j:k + 1])
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--file", required=True)
    ap.add_argument("--cc")
    a = ap.parse_args()
    raw = open(a.file, "rb").read()

    # the record after init
    r = run(a, "--tk-info")
    info = json.loads(r.stdout) if r.returncode == 0 else {}
    ok(info.get("version") == 1 and info.get("size") == 1468, "the record: version 1, size 1468 (%s)" % r.stdout.strip())
    ok(info.get("name") == "TKDEMO" and info.get("name_terminated") is True, "the record's name TKDEMO")
    ok(info.get("claims") == 0x5F, "claims 0x5F: all but TRACKS and VOLUME")
    ok(info.get("scr_seq") == 1 and info.get("led_seq") == 0 and info.get("say_seq") == 0,
       "init drew once; the Shift LED written dark (unchanged: led_seq stays 0); nothing said")

    # the manifest and rfm1_check
    m = custom_rfm1(raw)
    ok("tk1" in m.get("wants", []), "the manifest wants tk1")
    ok(m.get("takeover") == {"name": "TKDEMO", "claims": ["screen", "pads", "steps", "knobs", "jog", "buttons"]},
       "the manifest's takeover entry: %s" % m.get("takeover"))
    c = check(a.file, a)
    ok(c.returncode == 0 and 'takeover (tk1, "TKDEMO" claims 0x5f)' in c.stdout,
       "rfm1_check --run accepts tkdemo: %s" % (c.stdout + c.stderr).strip())
    with tempfile.TemporaryDirectory(prefix="rfm1_tk_test") as tmp:
        ok(raw.count(b"rfm1_tk_input") == 1 and raw.count(b'"tk1"') == 1, "(the names to rename occur once each)")
        part = os.path.join(tmp, "partial.rfm1")
        open(part, "wb").write(raw.replace(b"rfm1_tk_input", b"rfm1_xx_input"))
        c = check(part, a)
        ok(c.returncode == 1 and "all three or none" in c.stdout,
           "rfm1_check rejects a partial tk1 (rfm1_tk_input renamed away): %s" % c.stdout.strip().splitlines()[0])
        nowant = os.path.join(tmp, "nowant.rfm1")
        open(nowant, "wb").write(raw.replace(b'"tk1"', b'"tk9"'))
        c = check(nowant, a)
        ok(c.returncode == 1 and 'no "tk1" in wants' in c.stdout, "rfm1_check rejects the exports without tk1 in wants")
        badname = os.path.join(tmp, "badtake.rfm1")
        open(badname, "wb").write(raw.replace(b'"claims": [\n      "screen"', b'"claims": [\n      "screeN"'))
        c = check(badname, a)
        ok(c.returncode == 1 and "takeover.claims" in c.stdout, "rfm1_check rejects an unknown takeover claim")

        # the FM-1's build: rfm1_move.h without FM1_HOST_TK references nothing of the shim
        if a.cc:
            src = os.path.join(tmp, "dev.c")
            open(src, "w").write(
                '#include "rfm1_move.h"\n'
                'static void in(const uint8_t p[4]) { (void)p; }\n'
                'static const rfm1_tk_ops_t O = {in, 0};\n'
                'int dev(void) { uint8_t *p; rfm1_tk_register("X", RFM1_TK_ALL, &O); p = rfm1_tk_screen();\n'
                '  rfm1_tk_screen_done(); rfm1_tk_led(0, 1, 2, 3); rfm1_tk_white(RFM1_TK_L_SHIFT, 9);\n'
                '  rfm1_tk_claims(0); rfm1_tk_say("x", 1); return rfm1_tk_shown() + (p != 0); }\n')
            obj = os.path.join(tmp, "dev.o")
            cc = subprocess.run([a.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-c",
                                 "-I" + os.path.join(ROOT, "core-api"), src, "-o", obj], capture_output=True, text=True)
            ok(cc.returncode == 0, "rfm1_move.h compiles without FM1_HOST_TK (-Wall -Wextra -Werror) %s" % cc.stderr)
            if cc.returncode == 0:
                nm = subprocess.run(["nm", obj], capture_output=True, text=True).stdout
                ok("rfm1_tk" not in nm, "... and references no rfm1_tk symbol (the device image unchanged)")

    # a takeover scenario
    args = ["--settle", "700", "--seconds", "0.1", "--tk", "--tk-speech",
            "--tk-ev", "pad:5@200+50",          # lit, said, dark
            "--tk-ev", "button:track1@300+20",  # not claimed: skipped
            "--tk-ev", "knob:1@320:3",
            "--tk-ev", "knob:2@330:127",
            "--tk-ev", "button:menu@340+5",     # "CC 50"
            "--tk-ev", "button:menu@350+5",     # the same screen again: scr_seq stays
            "--tk-ev", "button:shift@400+20",   # claims ALL, Shift's LED
            "--tk-ev", "button:track1@500",     # now claimed: sent, held
            "--tk-ev", "pad:0@550",             # held when the takeover is left
            "--tk-off-at", "560",
            "--tk-screen", "-"]
    r = run(a, *args)
    out = r.stdout
    ok(r.returncode == 0, "rfm1_run --tk exits 0 %s" % r.stderr.strip())
    lines = [l for l in out.splitlines() if l.startswith(("tk-", "tk:"))]
    want = [
        'tk: "TKDEMO" claims 0x5f',
        "tk-state@100: on claims 0x5f speech",
        "tk-in@200: 64499009", "tk-screen@200: seq 2", "tk-led@200: 5 255 120 0", 'tk-say@200: interrupt 1 "pad 5"',
        "tk-in@250: 00498008", "tk-led@250: 5 0 0 0",
        "tk-skip@300: 7f2bb00b (unclaimed)",
        "tk-skip@320: 002bb00b (unclaimed)",
        "tk-in@320: 0347b00b", "tk-screen@320: seq 3",
        "tk-in@330: 7f48b00b", "tk-screen@330: seq 4",
        "tk-in@340: 7f32b00b", "tk-screen@340: seq 5",
        "tk-in@345: 0032b00b",
        "tk-in@350: 7f32b00b",
        "tk-in@355: 0032b00b",
        "tk-in@400: 7f31b00b", "tk-claims@400: 0xff", "tk-screen@400: seq 6", "tk-led@400: 63 127 127 127",
        "tk-in@420: 0031b00b",
        "tk-in@500: 7f2bb00b", "tk-screen@500: seq 7",
        "tk-in@550: 64449009", "tk-screen@550: seq 8", "tk-led@550: 0 255 120 0", 'tk-say@550: interrupt 1 "pad 0"',
        "tk-in@560: 00448008 (release)", "tk-led@560: 0 0 0 0",
        "tk-in@560: 002bb00b (release)",
        "tk-state@560: off",
        "tk-skip@600: 002bb00b (not shown)",
        "tk-skip@650: 00448008 (not shown)",
        "tk: 14 inputs, 4 skipped, scr_seq 8, led_seq 5, say_seq 2, claims 0xff, not shown",
    ]
    ok(lines == want, "the scenario's output, line for line")
    if lines != want:
        import difflib
        print("\n".join(difflib.unified_diff(want, lines, "expected", "rfm1_run", lineterm="")))
    scr = pbm(out)
    exp = render([(0, 0, 1, "TKDEMO"), (0, 24, 2, "PAD 0"), (0, 56, 1, "CLAIMS ALL")])
    ok(scr == exp, "the screen at the end: the font's \"PAD 0\" and \"CLAIMS ALL\", pixel for pixel (offset 168, "
                   "8 pages of 128 columns, bit 0 the top)")
    if scr != exp:
        for y in range(64):
            if scr[y] != exp[y]:
                print("  row %2d got  %s\n         want %s" % (y, scr[y], exp[y]))
    r2 = run(a, "--settle", "300", "--seconds", "0.1", "--tk", "--tk-ev", "knob:3@200:125", "--tk-screen", "-")
    ok(pbm(r2.stdout) == render([(0, 0, 1, "TKDEMO"), (0, 24, 2, "K3 -3"), (0, 56, 1, "CLAIMS 5F")]),
       "a knob turned anticlockwise: \"K3 -3\"")
    ok("tk-say" not in r2.stdout and "tk-led" not in r2.stdout, "a knob: nothing said, no LED changed")
    r3 = run(a, "--settle", "300", "--seconds", "0.1", "--tk", "--tk-at", "50", "--tk-ev", "pad:1@20", "--tk-ev",
             "pad:2@100")
    ok("tk-skip@20: 64459009 (not shown)" in r3.stdout and "tk-in@100: 64469009" in r3.stdout and
       'interrupt 0 "' not in r3.stdout and '(speech off: a host would not say it) "pad 2"' in r3.stdout,
       "before the takeover is shown nothing is sent; without --tk-speech the say is marked")

    print("%d failures" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
