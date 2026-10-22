#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The test suite on a public Release build (.github/workflows/release.yml, or by hand):
#
#   tools/release_tests.sh BUILD_DIR
#
# Runs ctest; every test must pass, and the only skips allowed are those a public build already makes: param_test for
# the cores without a parameter map, and tier2_test (the release configures no Tier 2 slots). ChoralRoot, Felucca,
# Melodee and LOOM-8 are all in a public build now (ChoralRoot and Melodee un-gated 2026-10-19, LOOM-8 2026-10-21), so
# the tests that drive them (double_load_test, halt_test, plugin_test, knob_test, panel_render_test, ui_render_test,
# remote_test) must run. Any other skip (one of those, a core's smoke, conformance or golden test) fails.
set -uo pipefail
build="${1:?usage: release_tests.sh BUILD_DIR}"
allowed='^(param_test_[a-z0-9]+|tier2_test)$'
log="$(mktemp)"
ctest --test-dir "$build" -C Release --output-on-failure -j "$(sysctl -n hw.ncpu)" 2>&1 | tee "$log"
st=${PIPESTATUS[0]}
if [ "$st" -ne 0 ]; then
    echo "release_tests: ctest failed (exit $st)" >&2
    exit "$st"
fi
skipped="$(sed -n 's/^[[:space:]]*[0-9][0-9]* - \([^ ]*\) (Skipped)$/\1/p' "$log")"
bad="$(printf '%s\n' "$skipped" | grep -Ev "$allowed" | grep -v '^$' || true)"
if [ -n "$bad" ]; then
    echo "release_tests: skipped, but a public build must run them: $(echo $bad)" >&2
    exit 1
fi
summary="$(grep -E 'tests passed|tests failed' "$log" | tail -1)"
echo "release_tests: ok: $summary; skipped as a public build does: $(echo $skipped)"
