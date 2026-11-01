#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Ripple's generate step in FM1VST (core_ripple.cmake's EXTRA_GEN, run in the submodule's app/ after fm1-base's
build.py generate() made the fonts and the screen reader's words): what Ripple's fm1.toml "gen" lines make, for the
host build instead of the device.

  ripple_gen.py --gen build/gen

  build/gen/disk_image.h       the empty disk (tools/mkdisk.py ../disk --header), as fm1.toml's
  build/gen/builtin_image.h    the built-in volume (tools/mkdisk.py --builtin): builtin/droplets as they are, and a
                               stand-in .cur for every current the build compiles in (currents/ and currents/extra:
                               the device's .cur files are JieLi code, built by its toolchain, which a host neither has
                               nor can run). A stand-in is the .cur header alone (magic, version 1, sizes 0): enough
                               for flow.c's web path (cur_link finds the file, then the current linked in by name);
                               core_ripple.c fills in its kind, voices and state size at power-on from the compiled
                               current's descriptor (the array is writable for that).
  build/gen/currents-web/      the Faust currents as C (sdk/tools/currents.py's own faust_to_c, from currents/gen/*.c
                               where Faust is not installed), as its --web build makes them: NAME_current.inc
                               including NAME_faust.inc, for core_ripple.cmake's wrapper units

Never writes into the submodule's sources: only app/build/ (its .gitignore'd build folder)."""
import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

CUR_MAGIC = 0x52554352                     # "RCUR" (app/flow.c cur_head_t)


def stand_in():
    # cur_head_t: magic, version (1), info (0: kind not said; filled at power-on), image_size, bss_size, entry,
    # nrelocs, sum, state_size: all 0
    return struct.pack("<IHHIIIIII", CUR_MAGIC, 1, 0, 0, 0, 0, 0, 0, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True, type=Path)
    a = ap.parse_args()
    app = Path(".").resolve()
    root = app.parent
    gen = a.gen.resolve()
    gen.mkdir(parents=True, exist_ok=True)
    py = sys.executable

    # the empty disk
    subprocess.run([py, str(root / "tools" / "mkdisk.py"), str(root / "disk"), "--header", str(gen / "disk_image.h")],
                   check=True)

    # the built-in volume: the shipped droplets and a stand-in .cur per compiled-in current
    vol = app / "build" / "fm1vst-builtin"
    if vol.exists():
        shutil.rmtree(vol)
    (vol / "currents").mkdir(parents=True)
    shutil.copytree(root / "builtin" / "droplets", vol / "droplets")
    names = []
    for d in (root / "currents", root / "currents" / "extra"):
        for f in sorted([*d.glob("*.dsp"), *d.glob("*.c"), *d.glob("*.cc")]):
            names.append(f.stem)
            (vol / "currents" / (f.stem + ".cur")).write_bytes(stand_in())
    out = gen / "builtin_image.h"
    subprocess.run([py, str(root / "tools" / "mkdisk.py"), str(vol), "--builtin", str(out)], check=True)
    txt = out.read_text()
    if "static const uint8_t BUILTIN_IMAGE" not in txt:
        sys.exit("ripple_gen: mkdisk.py's builtin_image.h has changed form (static const uint8_t BUILTIN_IMAGE)")
    out.write_text(txt.replace("static const uint8_t BUILTIN_IMAGE", "static uint8_t BUILTIN_IMAGE", 1))
    print("ripple_gen: %d currents on the built-in volume (stand-ins)" % len(names))

    # the Faust currents as C, as currents.py --web makes them (its own functions, not the C++ archive)
    sys.path.insert(0, str(root / "sdk" / "tools"))
    import currents                                   # noqa: E402  (Ripple's sdk/tools/currents.py, MIT)
    web = gen / "currents-web"
    if web.exists():
        shutil.rmtree(web)
    web.mkdir()
    dsps = sorted((root / "currents").glob("*.dsp")) + sorted((root / "currents" / "extra").glob("*.dsp"))
    currents.build_web([d.resolve() for d in dsps], web)
    for d in dsps:
        n = d.stem.replace("-", "_")
        if not (web / f"{n}_current.inc").exists():
            sys.exit(f"ripple_gen: {d.name}: no {n}_current.inc from currents.py --web")
    return 0


if __name__ == "__main__":
    sys.exit(main())
