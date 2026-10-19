#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# A static arm64 libusb-1.0.a fit for the release's deployment target, for Remote FM-1's vendor bulk transport
# (CMakeLists.txt: -DFM1_LIBUSB_PREFIX=<prefix>).
#
#   tools/libusb_static.sh PREFIX DEPLOYMENT_TARGET [ENV_FILE]
#
# Homebrew's libusb-1.0.a is used when it is arm64 and built for DEPLOYMENT_TARGET or older. A bottle is built for
# the macOS it was made on (minos 14, 15 ..): linked into an 11.0 plugin it references kIOMainPortDefault (macOS 12)
# and fails on older systems, so then libusb is built from its release tarball (the version and checksum Homebrew's
# formula pins) into PREFIX, with -mmacosx-version-min=DEPLOYMENT_TARGET. Either way the prefix holds
# lib/libusb-1.0.a, include/libusb-1.0/libusb.h and COPYING (LGPL-2.1, for the release's licences folder).
# Prints the prefix on stdout (everything else goes to stderr) and, with ENV_FILE ($GITHUB_ENV), appends
# FM1_LIBUSB_PREFIX=<prefix> to it.
set -euo pipefail
LIBUSB_VERSION=1.0.30
LIBUSB_SHA256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
LIBUSB_URL="https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2"

prefix="${1:?usage: libusb_static.sh PREFIX DEPLOYMENT_TARGET [ENV_FILE]}"
target="${2:?usage: libusb_static.sh PREFIX DEPLOYMENT_TARGET [ENV_FILE]}"
env_file="${3:-}"
mkdir -p "$prefix" && prefix="$(cd "$prefix" && pwd)"   # (absolute: configure --prefix needs it)
log() { echo "libusb_static: $*" >&2; }

# the newest minos (LC_BUILD_VERSION, or LC_VERSION_MIN_MACOSX's version) of any object in a Mach-O file or archive
max_minos() {
    otool -l "$1" | awk '/cmd LC_BUILD_VERSION/ {m=1} /cmd LC_VERSION_MIN_MACOSX/ {v=1}
                         m && $1 == "minos" {print $2; m=0} v && $1 == "version" {print $2; v=0}' | sort -V | tail -1
}
# a <= b, as versions
ver_le() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1" ]; }

fit() {   # fit <libusb-1.0.a>: arm64 and built for the target or older
    local a="$1" archs minos
    [ -f "$a" ] || { log "$a: missing"; return 1; }
    archs="$(lipo -archs "$a" 2>/dev/null || true)"
    case " $archs " in *" arm64 "*) ;; *) log "$a: architectures '$archs', not arm64"; return 1 ;; esac
    minos="$(max_minos "$a")"
    if [ -z "$minos" ] || ! ver_le "$minos" "$target"; then
        log "$a: built for macOS ${minos:-?}, newer than the deployment target $target"
        return 1
    fi
    log "$a: arm64, macOS $minos <= $target: fit"
}

done_with() {
    echo "$1"
    if [ -n "$env_file" ]; then
        echo "FM1_LIBUSB_PREFIX=$1" >> "$env_file"
    fi
    exit 0
}

# 1. an earlier run's build in PREFIX
if [ -f "$prefix/lib/libusb-1.0.a" ] && fit "$prefix/lib/libusb-1.0.a"; then
    done_with "$prefix"
fi

# 2. Homebrew's
if command -v brew > /dev/null 2>&1; then
    bp="$(brew --prefix libusb 2>/dev/null || true)"
    if [ -n "$bp" ] && fit "$bp/lib/libusb-1.0.a"; then
        real="$(cd "$bp" && pwd -P)"
        [ -f "$real/COPYING" ] || log "warning: no COPYING in $real"
        done_with "$real"
    fi
fi

# 3. built from the release tarball
log "building libusb $LIBUSB_VERSION from $LIBUSB_URL for macOS $target, arm64"
work="$(mktemp -d)"
curl -fsSL --retry 3 -o "$work/libusb.tar.bz2" "$LIBUSB_URL"
echo "$LIBUSB_SHA256  $work/libusb.tar.bz2" | shasum -a 256 -c - >&2
mkdir "$work/src"
tar -xjf "$work/libusb.tar.bz2" -C "$work/src" --strip-components 1
(
    cd "$work/src"
    export MACOSX_DEPLOYMENT_TARGET="$target"
    ./configure --prefix="$prefix" --enable-static --disable-shared --disable-dependency-tracking \
        CC="$(xcrun -f clang)" CFLAGS="-arch arm64 -mmacosx-version-min=$target -O2" \
        LDFLAGS="-arch arm64 -mmacosx-version-min=$target"
    make -j "$(sysctl -n hw.ncpu)"
    make install
) >&2
cp "$work/src/COPYING" "$prefix/COPYING"
fit "$prefix/lib/libusb-1.0.a" || { log "the built libusb-1.0.a is not fit"; exit 1; }
done_with "$prefix"
