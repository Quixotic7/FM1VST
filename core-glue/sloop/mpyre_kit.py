# SPDX-License-Identifier: GPL-3.0-only
"""M-PYRE's build/mpyre/sets.json before its generate step (core_mpyre.cmake's GEN_PYTHON runs this, then build.py).

M-PYRE's gen_samples.py compiles the HEAVY KIT (set PERC) from build/mpyre/sets.json, which the author's
tools/prep_mpyre.py makes from assets/mpyre-src/drums/*.aif (the drum recordings, CC0 per LICENSING.md). That folder is
not in the public tree (.gitignore), so the generate step cannot run from the published source.
  - assets/mpyre-src/drums present: prep_mpyre.py as the author runs it (the shipped kit).
  - absent: a stand-in. prep_mpyre.py's own drums() (its 16 lanes, cuts, fades, the reversed crash, the DROP made in
    code, its loudness balance) over the tree's CC0 acoustic kit (assets/samples-cc0/KIT, VCSL, CC0 1.0) instead of
    the missing recordings. Only PERC is written: the firmware reads nothing else from sets.json (the guitar, palm
    mute and bass are the sound pack, loaded into the user slots).
Run with the submodule root as the working directory and tools/ on PYTHONPATH (FM1Cores.cmake's generate step).
"""
import base64
import json
import subprocess
import sys
from pathlib import Path

SRC = Path.cwd()
OUT = SRC / "build" / "mpyre" / "sets.json"
TOOLS = SRC / "tools"
DRUMS = SRC / "assets" / "mpyre-src" / "drums"
KIT = SRC / "assets" / "samples-cc0" / "KIT"
# prep_mpyre.py's KIT recordings -> the CC0 kit's nearest sound
STAND_IN = {"Kick": "10_kick_BDrumNew_hit_v5", "Snare": "11_snare_Snare2_HitSN_v7",
            "Snare Ghost": "11_snare_Snare2_HitSN_v7", "Hihat Closed": "14_hihat_HitC_v3", "Hihat Open": "15_ohat_HitO",
            "Hihat Foot": "14_hihat_HitC_v3", "Ride": "19_ride_susCymb2_stick_mf1",
            "Bell": "20_cowbell_Cowbell1_Normal_v3", "Tom High": "17_tomhi_TomH_HitS_v4",
            "Tom Middle": "17_tomhi_TomH_HitS_v4", "Tom Floor": "16_tomlo_TomL_HitS_v4",
            "Crash 1": "18_crash_susCymb2_hit_f1", "Crash 2": "18_crash_susCymb2_hit_f1",
            "China": "18_crash_susCymb2_hit_f1"}


def main():
    if DRUMS.is_dir():
        subprocess.run([sys.executable, str(TOOLS / "prep_mpyre.py"), str(OUT.parent)], check=True)
        return
    sys.path.insert(0, str(TOOLS))
    import prep_mpyre as P
    load = P.load
    P.load = lambda path, sr=48000: load(KIT / f"{STAND_IN[Path(path).stem]}.wav", sr)
    zones = P.drums()
    js = {"PERC": [dict(name=z["name"], root=z["root"], lo=z["lo"], hi=z["hi"], loop=z["loop"], deemph=z["deemph"],
                        pcm=base64.b64encode(z["pcm"].tobytes()).decode()) for z in zones]}
    text = json.dumps(js)
    if not OUT.exists() or OUT.read_text() != text:   # (unchanged: gen_samples.py's cache key stays)
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(text)
    print(f"mpyre kit: assets/mpyre-src missing: PERC stand-in from assets/samples-cc0/KIT, {len(zones)} lanes")


main()
