#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The fm1emu core's real-time factors (core-glue/fm1emu/README.md "Speed"): core_bench (tests/core_bench.cpp: 10 s
# of a held D4 F#4 A4 chord through Device::render at 48 kHz) for the fm1emu module with each package
# (FM1EMU_FWSC), next to the native core built from the same firmware where there is one. Each package uses its
# native core's conformance file where there is one (its `sound` prelude and settle time: the same firmware), else
# the stock firmware's. Not a ctest (machine dependent).
#
#   tests/fm1emu_bench.sh [BUILD_DIR] [SECONDS]        (BUILD_DIR: build-fm1emu/cmake; FM1EMU_JIT=0: the interpreter)
#
#   FM1_BENCH_MHZ=n     the emulated clock (the core's boot option "mhz"; default the core's own, 360)
#   FM1_BENCH_BLOCK=n   host blocks of n frames (512); core_bench prints the slowest block and the blocks that took
#                       longer than they last (a burst of emulation inside one block is a dropout)
#   FM1EMU_PKG_DIR=dir  every <hub id>.fwsc in dir instead of the list below (the Hub's packages: the catalog's ids),
#                       without the native rows
#
# Needs core_bench, cores/fm1emu.fm1core and the native cores built in BUILD_DIR, and the packages below.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=${1:-$ROOT/build-fm1emu/cmake}
SECS=${2:-10}
G=$ROOT/core-glue
PKGS=${FM1EMU_PKGS:-/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1}
EMU=$B/cores/fm1emu.fm1core
export FM1EMU_HOME=$B/conformance/home FM1_BENCH_INFO=1

if [ -n "${FM1EMU_PKG_DIR:-}" ]; then
    for pkg in "$FM1EMU_PKG_DIR"/*.fwsc; do
        id=$(basename "$pkg" .fwsc)
        case "$id" in
            fart) core=fartmachine ;;
            nmsfm) core=nomansky ;;
            *) core=$id ;;
        esac
        conf=$(ls "$G"/*/conformance_"$core".txt 2>/dev/null | head -1)
        [ -n "$conf" ] || { [ "$core" = melodee ] && conf=$G/melodee/conformance.txt; }
        [ -n "$conf" ] || conf=$G/fm1emu/conformance_fm1emu_mvave.txt
        printf '%-14s ' "$id"
        FM1EMU_FWSC="$pkg" "$B/core_bench" "$SECS" "$EMU=$conf" | tail -n +2 | tr '\n' ' '
        echo
    done
    exit 0
fi

# name | package | native core id (- none) | conformance file
while IFS='|' read -r name pkg native conf; do
    [ -z "$name" ] && continue
    echo "== $name ($pkg)"
    if [ ! -f "$pkg" ]; then
        echo "   (package not found)"
        continue
    fi
    if [ "$native" != "-" ]; then
        "$B/core_bench" "$SECS" "$B/cores/$native.fm1core=$conf" | tail -n +2
    fi
    FM1EMU_FWSC="$pkg" "$B/core_bench" "$SECS" "$EMU=$conf" | tail -n +2
done <<EOF
Felucca 1.6.1|$ROOT/build-fm1emu/fwsc/felucca-1.6.1.fwsc|felucca|$G/felucca_web/conformance_felucca.txt
stock V15|$PKGS/MvaveFM1Unbricker/FM-1.fwsc|-|$G/fm1emu/conformance_fm1emu_mvave.txt
SLOOP 2.8|$ROOT/cores/sloop-fm1/docs/firmware/sloop-2.8.fwsc|sloop|$G/sloop/conformance_sloop.txt
sloopDX 4.0|$ROOT/cores/dxsloop/docs/firmware/sloopdx-4.0.fwsc|sloopdx|$G/sloop/conformance_sloopdx.txt
OberVave 1.5|$ROOT/cores/obervave/docs/firmware/obervave-1.5.fwsc|obervave|$G/dinghy/conformance_obervave.txt
No Man Sky 0.5|$ROOT/cores/nomanskyFM/release/nmsfm-0.5.fwsc|nomansky|$G/sloop/conformance_nomansky.txt
FM-106|$ROOT/cores/fm-106/fm106.fwsc|fm106|$G/dinghy/conformance_fm106.txt
ChoralRoot 0.25.1|$PKGS/fm1-backups/5BD75BE3-before-remotefm1-20261017/choralroot-0.25.1.fwsc|choralroot|$G/choralroot/conformance_choralroot.txt
EOF
