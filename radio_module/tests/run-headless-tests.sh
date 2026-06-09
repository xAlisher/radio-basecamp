#!/usr/bin/env bash
# Headless load/dispatch test for radio_module via standalone logoscore.
#
# WHAT THIS PROVES (verified 2026-06-10):
#   - radio_module builds, installs, and LOADS under logoscore (manifest/IID/variant correct)
#   - initLogos is invoked and Q_INVOKABLE methods DISPATCH end-to-end ("Method call successful")
#
# WHAT IT CANNOT PROVE HERE:
#   - The actual JSON return value. Bare standalone logoscore's capability-token handshake fails
#     for EVERY module (confirmed: canonical capability_module.requestModule also returns `false`),
#     which gates the return value. Reading real returns requires the AppImage / a logoscore with a
#     working capability_module handshake. See basecamp-skills: standalone-logoscore-isolation,
#     sdk-capability-token-architecture, whole-archive-module-proxy-strip.
#
# Isolation: installs into a TEMP modules dir only — never the shared Basecamp/LogosApp dir.
#
# Usage:  ./tests/run-headless-tests.sh   [LOGOSCORE=/path/to/logoscore]
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"

LOGOSCORE="${LOGOSCORE:-$(find /nix/store -name logoscore -path '*/bin/*' 2>/dev/null | head -1)}"
if [[ -z "$LOGOSCORE" || ! -x "$LOGOSCORE" ]]; then
    echo "SKIP: logoscore not found (set LOGOSCORE=...)"; exit 0
fi

# Build the installable module dir (manifest + .so + variant) if not present.
INSTALL="${RADIO_INSTALL:-}"
if [[ -z "$INSTALL" ]]; then
    echo "Building radio_module #install ..."
    nix build "$HERE#install" --out-link /tmp/rm-install -L >/dev/null 2>&1 || { echo "FAIL: nix build .#install"; exit 1; }
    INSTALL=/tmp/rm-install
fi
SRC="$INSTALL/modules/radio_module"
[[ -f "$SRC/radio_module_plugin.so" ]] || { echo "FAIL: built module not found at $SRC"; exit 1; }

# Isolated temp modules dir.
MDIR="$(mktemp -d)"; trap 'rm -rf "$MDIR"' EXIT
mkdir -p "$MDIR/radio_module"; cp -rL "$SRC/." "$MDIR/radio_module/"; chmod -R u+w "$MDIR"

# Offline smoke: drop the delivery_module dep (ping doesn't need it) and add the variant keys
# logoscore resolves against.
python3 - "$MDIR/radio_module/manifest.json" <<'PY'
import sys, json
p = sys.argv[1]; m = json.load(open(p))
m["dependencies"] = [d for d in m.get("dependencies", []) if d != "delivery_module"]
so = next(iter(m["main"].values()), "radio_module_plugin.so")
m["main"] = {k: so for k in ("linux-amd64","linux-amd64-dev","linux-x86_64","linux-x86_64-dev")}
json.dump(m, open(p,"w"), indent=2)
PY

OUT="$("$LOGOSCORE" -m "$MDIR" -l radio_module -c "radio_module.ping()" --quit-on-finish 2>&1)"

pass=0; fail=0
chk(){ if grep -qiE "$2" <<<"$OUT"; then echo "PASS: $1"; ((pass++)); else echo "FAIL: $1"; ((fail++)); fi; }
chk "radio_module registry connected (plugin loaded)" 'connected to registry: "local:logos_radio_module'
chk "ping() dispatched end-to-end"                    'Method call successful'
grep -qi 'silently skip\|no such method\|failed to load' <<<"$OUT" && { echo "FAIL: load/skip error in output"; ((fail++)); }

echo "---"; echo "radio_module headless load test: $pass passed, $fail failed"
echo "(return-value assertions deferred to AppImage — see header)"
[[ $fail -eq 0 ]]
