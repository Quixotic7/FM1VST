#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Checks a public Release build and packages it: the same on CI (.github/workflows/release.yml) and on a Mac.
#
#   tools/package_release.sh BUILD_DIR [OUT_DIR]          (OUT_DIR default: BUILD_DIR/dist)
#
# BUILD_DIR is a configured and built tree (cmake -DCMAKE_BUILD_TYPE=Release -DFM1_PUBLIC_BUILD=ON
# -DFM1_COPY_PLUGIN=OFF -DCMAKE_OSX_ARCHITECTURES=arm64 ..). Reads BUILD_DIR/release-manifest.tsv (CMakeLists.txt).
# The checks, each fatal:
#   - the manifest: a public build, arm64 only, the version; its cores are exactly tools/release/cores.tsv's
#   - each bundle (VST3, AU, Standalone): its Info.plist version, Contents/Resources/cores holding exactly those cores
#     and no gated one, every Mach-O in it arm64 only, linking no /opt/homebrew or /usr/local path, built for the
#     deployment target or older, and the signature valid (codesign --verify --deep --strict)
#   - libusb: the static library the build linked is built for the deployment target or older, and no binary
#     references kIOMainPortDefault below macOS 12 (a newer libusb's; it fails to load on macOS 11). A warning only
#     unless FM1_RELEASE_STRICT=1 (CI): a local build may link Homebrew's newer bottle
#   - the zip: the three bundles, 10 cores in each, no gated core's name anywhere in it
# Writes to OUT_DIR: FM1VST-<version>-macOS-arm64.zip (FM1VST.vst3, FM1VST.component, FM1VST.app, INSTALL.txt,
# README.txt, LICENSE, licences/), SHA256SUMS and RELEASE-NOTES.md (the release's body: the cores, what's new from
# CHANGELOG.md).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
build="$(cd "${1:?usage: package_release.sh BUILD_DIR [OUT_DIR]}" && pwd)"
out="${2:-$build/dist}"
strict="${FM1_RELEASE_STRICT:-0}"
manifest="$build/release-manifest.tsv"
fails=0

die() { echo "::error title=package_release::$*" >&2; echo "package_release: FAIL: $*" >&2; exit 1; }
bad() { echo "package_release: FAIL: $*" >&2; fails=$((fails + 1)); }
ok() { echo "package_release: ok: $*"; }
warn_or_bad() {
    if [ "$strict" = 1 ]; then bad "$*"; else echo "package_release: WARNING (fatal with FM1_RELEASE_STRICT=1): $*" >&2; fi
}
ver_le() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1" ]; }
max_minos() {
    otool -l "$1" | awk '/cmd LC_BUILD_VERSION/ {m=1} /cmd LC_VERSION_MIN_MACOSX/ {v=1}
                         m && $1 == "minos" {print $2; m=0} v && $1 == "version" {print $2; v=0}' | sort -V | tail -1
}
info() { awk -F'\t' -v k="$1" '$1 == "info" && $2 == k {print $3}' "$manifest"; }

# ---- the manifest
[ -f "$manifest" ] || die "$manifest missing (configure with this tree's CMakeLists.txt)"
version="$(info version)"
target="$(info deployment_target)"
[ -n "$version" ] || die "no version in the manifest"
[ "$(info public_build)" = ON ] || die "not a public build (configure with -DFM1_PUBLIC_BUILD=ON)"
[ "$(info architectures)" = arm64 ] || die "architectures '$(info architectures)', not arm64 (-DCMAKE_OSX_ARCHITECTURES=arm64)"
[ -n "$target" ] || die "no deployment target (-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)"
if [ -n "${FM1_RELEASE_VERSION:-}" ] && [ "$FM1_RELEASE_VERSION" != "$version" ]; then
    die "the tag says $FM1_RELEASE_VERSION, CMakeLists.txt's project() says $version"
fi
expected="$(awk -F'\t' '!/^#/ && NF {print $1}' "$here/release/cores.tsv" | sort)"
built="$(awk -F'\t' '$1 == "core" {print $2}' "$manifest" | sort)"
gated="$(awk -F'\t' '$1 == "gated" {print $2}' "$manifest" | sort)"
[ "$built" = "$expected" ] || die "the build's cores ($(echo $built)) are not the public set ($(echo $expected))"
for g in $gated; do
    if echo "$expected" | grep -qx "$g"; then die "$g is both gated and in tools/release/cores.tsv"; fi
done
ncores="$(echo "$expected" | wc -l | tr -d ' ')"
ok "manifest: FM1VST $version, public, arm64, macOS $target+, $ncores cores ($(echo $built)); gated left out: $(echo $gated)"

art="$build/FM1VST_artefacts/Release"
vst3="$art/VST3/FM1VST.vst3"
au="$art/AU/FM1VST.component"
app="$art/Standalone/FM1VST.app"

# ---- the bundles
check_bundle() {
    local b="$1" n plist_v cores f archs minos deps
    [ -d "$b" ] || { bad "$b missing"; return; }
    n="$(basename "$b")"
    plist_v="$(plutil -extract CFBundleShortVersionString raw -o - "$b/Contents/Info.plist" 2>/dev/null || true)"
    [ "$plist_v" = "$version" ] || bad "$n: Info.plist version '$plist_v', not $version"
    cores="$(cd "$b/Contents/Resources/cores" 2>/dev/null && ls | sed -n 's/\.fm1core$//p' | sort)"
    [ "$cores" = "$expected" ] || bad "$n: Contents/Resources/cores holds ($(echo $cores)), not the public set"
    if [ "$(ls "$b/Contents/Resources/cores" | grep -vc '\.fm1core$' || true)" != 0 ]; then
        bad "$n: other files in Contents/Resources/cores: $(ls "$b/Contents/Resources/cores" | grep -v '\.fm1core$' | tr '\n' ' ')"
    fi
    for g in $gated; do
        [ ! -e "$b/Contents/Resources/cores/$g.fm1core" ] || bad "$n: the gated core $g is in the bundle"
    done
    local nmacho=0
    while IFS= read -r -d '' f; do
        file -b "$f" | grep -q 'Mach-O' || continue
        nmacho=$((nmacho + 1))
        archs="$(lipo -archs "$f" 2>/dev/null || echo '?')"
        [ "$archs" = arm64 ] || bad "$n: ${f#$b/}: architectures '$archs', not arm64"
        deps="$(otool -L "$f" | tail -n +2 | grep -E '/opt/homebrew|/usr/local' || true)"
        [ -z "$deps" ] || bad "$n: ${f#$b/} links $(echo $deps)"
        minos="$(max_minos "$f")"
        { [ -n "$minos" ] && ver_le "$minos" "$target"; } || bad "$n: ${f#$b/} built for macOS ${minos:-?}, newer than $target"
        # (a WEAK reference is fine: JUCE's juce_AppleRemote_mac.mm uses it only on macOS 12+, and dyld resolves a
        # missing weak import to NULL on macOS 11; only a strong one, as a newer libusb's, fails to load there)
        if ! ver_le 12.0 "$target" && nm -m "$f" 2>/dev/null | grep -v ' weak ' | grep -q 'external _kIOMainPortDefault'; then
            warn_or_bad "$n: ${f#$b/} references kIOMainPortDefault (macOS 12+) but targets macOS $target: a libusb built for a newer macOS"
        fi
    done < <(find "$b" -type f -print0)
    [ "$nmacho" -ge $((ncores + 1)) ] || bad "$n: only $nmacho Mach-O files"
    codesign --verify --deep --strict "$b" 2>/dev/null || bad "$n: codesign --verify --deep --strict fails"
    ok "$n: version $plist_v, the $ncores cores, $nmacho Mach-O files checked (arm64, no Homebrew links, macOS <= $target), signature valid"
}
for b in "$vst3" "$au" "$app"; do check_bundle "$b"; done

libusb="$(info libusb)"
if [ -z "$libusb" ] || [ ! -f "$libusb" ]; then
    warn_or_bad "no static libusb linked: Remote FM-1 has no vendor bulk (frames over SysEx only)"
else
    m="$(max_minos "$libusb")"
    if ver_le "$m" "$target"; then ok "libusb: $libusb (macOS $m)"; else
        warn_or_bad "libusb: $libusb is built for macOS $m, newer than $target (tools/libusb_static.sh builds a fit one)"
    fi
fi
[ "$fails" = 0 ] || die "$fails check(s) failed"

# ---- the staging folder
name="FM1VST-$version-macOS-arm64"
mkdir -p "$out"
stage="$out/$name"
if [ -e "$stage" ]; then rm -rf "$stage"; fi
rm -f "$out/$name.zip" "$out/SHA256SUMS" "$out/RELEASE-NOTES.md"
mkdir -p "$stage/licences/cores"
ditto "$vst3" "$stage/FM1VST.vst3"
ditto "$au" "$stage/FM1VST.component"
ditto "$app" "$stage/FM1VST.app"
cp "$root/LICENSE" "$stage/LICENSE"
sed "s/@VERSION@/$version/g" "$here/release/INSTALL.txt" > "$stage/INSTALL.txt"

# the licences: each core's own files (its LICENSE, LICENSING.md, CREDITS, NOTICE, LICENSES/ ..; its submodules'
# too), RemoteFM1, libusb, JUCE and the SDKs it compiles in, Inter Tight
lic="$stage/licences"
core_rows=""
while IFS=$'\t' read -r kind id cver commit clic fam src; do
    [ "$kind" = core ] || continue
    cname="$(awk -F'\t' -v k="$id" '$1 == k {print $2}' "$here/release/cores.tsv")"
    rel="${src#$root/}"
    url="$(git -C "$root" config -f .gitmodules --get "submodule.$rel.url" 2>/dev/null || true)"
    url="${url%.git}"
    d="$lic/cores/$id"
    mkdir -p "$d"
    (cd "$src" && find . -maxdepth 3 \( -name .git -o -name build -o -name node_modules -o -name tools -o -name video \
                                        -o -name LICENSES \) -prune -o -type f \( -iname 'LICENSE*' -o -iname 'COPYING*' \
                                        -o -iname 'NOTICE*' -o -iname 'LICENSING*' -o -iname 'CREDITS*' -o -iname 'ATTRIBUTION*' \) -print) |
        while IFS= read -r f; do mkdir -p "$d/$(dirname "$f")"; cp "$src/$f" "$d/$f"; done
    if [ -d "$src/LICENSES" ]; then ditto "$src/LICENSES" "$d/LICENSES"; fi
    [ -f "$d/LICENSE" ] || bad "core $id: no LICENSE in $src"
    printf '%s %s (%s): %s, %s at %s\n' "$cname" "$cver" "$id" "$clic" "${url:-?}" "$commit" > "$d/SOURCE.txt"
    core_rows+="$id"$'\t'"$cname"$'\t'"$cver"$'\t'"$clic"$'\t'"$url"$'\t'"$commit"$'\n'
done < "$manifest"
# (the README's and the notes' order: tools/release/cores.tsv's, the default firmware first)
ordered=""
for id in $(awk -F'\t' '!/^#/ && NF {print $1}' "$here/release/cores.tsv"); do
    ordered+="$(printf '%s' "$core_rows" | awk -F'\t' -v k="$id" '$1 == k')"$'\n'
done
core_rows="$ordered"
default_core="$(info default_core)"
default_name="$(awk -F'\t' -v k="$default_core" '$1 == k {print $2}' "$here/release/cores.tsv")"
mkdir -p "$lic/RemoteFM1" "$lic/JUCE" "$lic/libusb"
cp "$root/remote/RemoteFM1/LICENSE" "$lic/RemoteFM1/LICENSE"
if [ -f "$root/remote/RemoteFM1/LICENSING.md" ]; then cp "$root/remote/RemoteFM1/LICENSING.md" "$lic/RemoteFM1/"; fi
cp "$root/plugin/fonts/OFL.txt" "$lic/InterTight-OFL.txt"
juce="$(info juce_source)"
[ -f "$juce/LICENSE.md" ] || die "JUCE's LICENSE.md not found in '$juce'"
cp "$juce/LICENSE.md" "$lic/JUCE/LICENSE.md"
for f in modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt:VST3_SDK-LICENSE.txt \
         modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt:AudioUnitSDK-LICENSE.txt \
         modules/juce_graphics/image_formats/pnglib/LICENSE:libpng-LICENSE.txt \
         modules/juce_graphics/unicode/sheenbidi/LICENSE:SheenBidi-LICENSE.txt; do
    if [ -f "$juce/${f%%:*}" ]; then cp "$juce/${f%%:*}" "$lic/JUCE/${f##*:}"; fi
done
if [ -n "$libusb" ] && [ -f "$libusb" ]; then
    lp="$(cd "$(dirname "$(readlink -f "$libusb")")/.." && pwd -P)"
    if [ -f "$lp/COPYING" ]; then cp "$lp/COPYING" "$lic/libusb/COPYING"; else warn_or_bad "libusb's COPYING not found in $lp"; fi
fi
juce_v="$(info juce_version)"
cat > "$lic/README.txt" << EOF
What FM1VST $version is made of, and under which licence
=========================================================

FM1VST itself: GPL-3.0-only (../LICENSE). Source: https://github.com/Quixotic7/FM1VST at the tag v$version, with
its submodules (git clone --recursive): the corresponding source of everything in this zip.

cores/<id>/      each bundled firmware core: its licence, its LICENSING.md / CREDITS / NOTICE files and LICENSES/
                 folder as its repository ships them, and SOURCE.txt (the repository and the commit it is built
                 from). AMB-1 is MPL-2.0 (cores/amb/LICENSE); JIANT FM's VOICE words are CMUdict, BSD-2-Clause
                 (cores/jiant/LICENSES/BSD-2-Clause-CMUdict.txt); the cores' fonts are under the SIL OFL 1.1 (their
                 LICENSES/OFL-*.txt). The plugin's About page shows each core's credits.
RemoteFM1/       libremotefm1 (Remote FM-1's host library), GPL-3.0.
libusb/          libusb $( [ -f "$lic/libusb/COPYING" ] && echo "(linked statically)" ), LGPL-2.1-or-later: COPYING.
                 Its source: https://github.com/libusb/libusb
JUCE/            the JUCE framework $juce_v, used under its open-source licence, the AGPLv3
                 (https://www.gnu.org/licenses/agpl-3.0.html; GPL-3.0 section 13 allows the combination), and the
                 SDKs it compiles in: the VST3 SDK (MIT), the AudioUnitSDK (Apache-2.0), libpng, SheenBidi.
InterTight-OFL.txt  the editor's type, Inter Tight (The Inter Project Authors), SIL Open Font License 1.1.
EOF

# the README
{
    echo "FM1VST $version"
    echo "==========$(printf '%*s' ${#version} '' | tr ' ' '=')"
    echo
    echo "The M-VAVE FM-1 as a plug-in: FM1VST runs FM-1 firmwares, each compiled from its own unmodified source, as"
    echo "loadable cores, with the FM-1's panel as the editor. AU, VST3 and Standalone; macOS $target or later, Apple"
    echo "silicon. Installing: INSTALL.txt (this build is not notarised: it says how to allow it)."
    echo
    echo "The cores in this build (the default firmware: ${default_name:-$default_core}):"
    echo
    printf '%s' "$core_rows" | while IFS=$'\t' read -r id cname cver clic url commit; do
        printf '  %-14s %-12s %-14s %s\n' "$cname" "$cver" "$clic" "$url"
    done
    echo
    echo "Pick one in the settings sheet (the corner button) > Firmware. More cores: drop a .fm1core into"
    echo "~/Library/Application Support/fm1emu/cores/."
    echo
    echo "Remote FM-1: a real FM-1 can be the panel of the core running in FM1VST (its keys, knobs and buttons play"
    echo "it, its screen and LEDs show it). The unit needs the RemoteFM1 firmware: https://quixotic7.github.io/RemoteFM1/"
    echo
    echo "FM1VST is GPL-3.0-only (LICENSE); every part's licence and credits are in licences/ (licences/README.txt)."
    echo "Source: https://github.com/Quixotic7/FM1VST (tag v$version). FM1VST is independent of and unaffiliated"
    echo "with M-VAVE."
} > "$stage/README.txt"
[ "$fails" = 0 ] || die "$fails check(s) failed"

# ---- the zip, checked, and its checksum
(cd "$out" && ditto -c -k --norsrc --keepParent "$name" "$name.zip")
listing="$(unzip -Z1 "$out/$name.zip")"
for want in "$name/FM1VST.vst3/Contents/MacOS/FM1VST" "$name/FM1VST.component/Contents/MacOS/FM1VST" \
            "$name/FM1VST.app/Contents/MacOS/FM1VST" "$name/INSTALL.txt" "$name/README.txt" "$name/LICENSE" \
            "$name/licences/README.txt" "$name/licences/JUCE/LICENSE.md" "$name/licences/InterTight-OFL.txt"; do
    echo "$listing" | grep -qx "$want" || bad "zip: $want missing"
done
for fmt in vst3 component app; do
    n="$(echo "$listing" | grep -c "^$name/FM1VST\.$fmt/Contents/Resources/cores/[^/]*\.fm1core$" || true)"
    [ "$n" = "$ncores" ] || bad "zip: FM1VST.$fmt holds $n cores, not $ncores"
done
for g in $gated; do
    if echo "$listing" | grep -q "/$g\.fm1core$\|/cores/$g/"; then bad "zip: the gated core $g is in it"; fi
done
if echo "$listing" | grep -q '\.DS_Store\|/\._'; then bad "zip: Finder litter (.DS_Store, ._ files) in it"; fi
[ "$fails" = 0 ] || die "$fails check(s) failed"
(cd "$out" && shasum -a 256 "$name.zip" > SHA256SUMS)
size="$(du -h "$out/$name.zip" | cut -f1 | tr -d ' ')"
ok "zip: $out/$name.zip ($size, $(echo "$listing" | wc -l | tr -d ' ') entries), 3 bundles x $ncores cores, no gated core"

# ---- the release notes: the cores, then CHANGELOG.md's section for this version
{
    echo "FM1VST $version for macOS $target or later on Apple silicon: AU, VST3 and Standalone in one zip."
    echo "Not notarised: see INSTALL.txt in the zip for allowing it (\`xattr -dr com.apple.quarantine\` or System"
    echo "Settings > Privacy & Security), and for rescanning in Live and Logic."
    echo
    echo "### The cores in this build"
    echo
    echo "| firmware | version | licence | source |"
    echo "|---|---|---|---|"
    printf '%s' "$core_rows" | while IFS=$'\t' read -r id cname cver clic url commit; do
        d=""; [ "$id" = "$default_core" ] && d=" (default)"
        echo "| $cname$d | $cver | $clic | [${url#https://github.com/}]($url) at \`${commit:0:7}\` |"
    done
    echo
    echo "Remote FM-1 needs the unit on the [RemoteFM1](https://quixotic7.github.io/RemoteFM1/) firmware."
    echo
    awk -v v="$version" '
        /^## / { if (on) exit; if (index($0, "## " v " ") == 1 || $0 == "## " v) { on = 1; print "### What'"'"'s new"; next } }
        on { sub(/^### /, "#### "); print }' "$root/CHANGELOG.md"
    echo
    echo "SHA-256: \`$(cut -d' ' -f1 "$out/SHA256SUMS")\` (SHA256SUMS)"
} > "$out/RELEASE-NOTES.md"
grep -q "What's new" "$out/RELEASE-NOTES.md" || warn_or_bad "CHANGELOG.md has no '## $version' section"
ok "release notes: $out/RELEASE-NOTES.md"
echo "$out/$name.zip"
