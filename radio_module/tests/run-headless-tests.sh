#!/usr/bin/env bash
# Headless tests for radio_module via logoscore (Tier-2 integration).
#
# Tier 1 (offline Qt::Test) lives in tests/test_radio.cpp — build with -DRADIO_BUILD_TESTS=ON.
# This script exercises the built plugin under logoscore single-invocation mode (multi -c flags).
#
# Usage:
#   ./tests/run-headless-tests.sh
#   LOGOSCORE=/path/to/logoscore ./tests/run-headless-tests.sh
#
# Convention mirrors keeper-basecamp / stash-basecamp headless harnesses.
set -uo pipefail

LOGOSCORE="${LOGOSCORE:-}"
if [[ -z "$LOGOSCORE" ]]; then
    LOGOSCORE=$(find /nix/store -maxdepth 3 -name logoscore -path "*/bin/*" 2>/dev/null | head -1)
fi
if [[ -z "$LOGOSCORE" || ! -x "$LOGOSCORE" ]]; then
    echo "SKIP: logoscore not found (set LOGOSCORE=...)" >&2
    exit 0
fi

MODULE_SO="${MODULE_SO:-$(find "$(dirname "$0")/.." -name 'radio_module_plugin.*' \( -name '*.so' -o -name '*.dylib' \) 2>/dev/null | head -1)}"
if [[ -z "$MODULE_SO" ]]; then
    echo "FAIL: radio_module_plugin not built. Run 'nix build' first." >&2
    exit 1
fi

pass=0; fail=0
check() { # check <name> <expected-substr> <actual>
    if [[ "$3" == *"$2"* ]]; then echo "PASS: $1"; ((pass++)); else echo "FAIL: $1 — expected '$2' in: $3"; ((fail++)); fi
}

# --- #1 scaffold: ping ---
out=$("$LOGOSCORE" -c "radio_module.ping()" 2>/dev/null)
check "ping returns ok" '"ok":true' "$out"

# --- #2 origin (implemented in issue #2): start/stop MediaMTX ---
# out=$("$LOGOSCORE" -c 'radio_module.startStream({"name":"t","visibility":"public"})' -c 'radio_module.getStreamStatus()')
# check "stream waiting before OBS" '"state":"waiting"' "$out"

# --- #5 discovery (issue #5, network; XFAIL if delivery_module absent) ---
# Two-instance round-trip lives here; mark XFAIL (logged, never silent pass) when delivery_module missing.

echo "---"; echo "radio_module headless: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
