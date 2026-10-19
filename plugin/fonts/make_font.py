# SPDX-License-Identifier: GPL-3.0-only
# The editor's type (plugin/Look.cpp): one static weight of Inter Tight (OFL 1.1, plugin/fonts/OFL.txt) cut from the
# variable font, subset to Latin-1 and the punctuation the editor prints. JUCE cannot pick a variable font's weight, so
# the build makes the four weights it uses (CMakeLists.txt) and embeds them (FontData).
#   make_font.py InterTight-VF.ttf <wght> <out.ttf>
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

src, wght, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
font = TTFont(src)
try:
    font = instancer.instantiateVariableFont(font, {"wght": wght}, updateFontNames=True)
except Exception:                       # (no STAT names to update: the default names stay)
    font = instancer.instantiateVariableFont(TTFont(src), {"wght": wght})
opts = subset.Options()
opts.layout_features = ["kern", "liga", "calt", "tnum", "case", "ccmp", "locl", "mark", "mkmk"]
opts.name_IDs = ["*"]
opts.name_languages = ["*"]
opts.notdef_outline = True
opts.glyph_names = False
sub = subset.Subsetter(opts)
unicodes = list(range(0x20, 0x7F)) + list(range(0xA0, 0x180)) + list(range(0x2010, 0x2028)) + \
    list(range(0x2030, 0x203B)) + [0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2715, 0x2713, 0x00B7]
sub.populate(unicodes=unicodes)
sub.subset(font)
font.save(out)
