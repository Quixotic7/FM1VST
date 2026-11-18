# SPDX-License-Identifier: GPL-3.0-only
# TRAPSTATION OS's generate step (core_trapstationos.cmake GEN_PYTHON, run in the firmware's root with PYTHONPATH=tools):
# tools/build.py generate(), unchanged, except that tools/gen_gh_fonts.py, which reads DejaVu Sans Mono Bold from the
# system (/usr/share/fonts/truetype/dejavu/ on Linux, /mnt/c/Windows/Fonts/ under WSL: the release's build machine; the
# repository carries neither the font nor its licence), gets the copy FM1VST carries next to this file,
# licences/trapstationos/DejaVuSansMono-Bold.ttf (DejaVu 2.35, Bitstream Vera licence, DejaVu changes public domain:
# licences/trapstationos/DejaVu-LICENSE.txt). The firmware's tree is not modified: the script is run from its text with
# the one path replaced.
import os
import subprocess
import sys
from pathlib import Path

import build

FONT = str(Path(__file__).resolve().parent / "licences" / "trapstationos" / "DejaVuSansMono-Bold.ttf")
WSL_FONT = "/mnt/c/Windows/Fonts/DejaVuSansMono-Bold.ttf"
RUN = ("import sys\n"
       "script, out, font, old = sys.argv[1:5]\n"
       "src = open(script, encoding='utf-8').read()\n"
       "assert old in src, 'gen_gh_fonts.py: the font path moved'\n"
       "src = src.replace('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', font).replace(old, font)\n"
       "sys.argv = [script, out]\n"
       "exec(compile(src, script, 'exec'), {'__name__': '__main__', '__file__': script})\n")
_popen = subprocess.Popen


def popen(cmd, *a, **k):
    if len(cmd) >= 3 and Path(str(cmd[1])).name == "gen_gh_fonts.py":
        cmd = [cmd[0], "-c", RUN, str(cmd[1]), str(cmd[2]), FONT, WSL_FONT]
    return _popen(cmd, *a, **k)


build.subprocess.Popen = popen
os.environ.setdefault("TRAP_FM6_TEST", "1")   # (build.py main()'s defaults, set before its generate(): gen_final_ui.py
os.environ.setdefault("TRAP_UX_TEST", "1")    # reads TRAP_UX_TEST for the boot frames it keeps)
build.generate()
