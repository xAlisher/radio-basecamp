#!/usr/bin/env bash
# Build + run the Tier-1 direct test (tests/direct_test.cpp): instantiate the plugin in-process
# and verify startStream mints the card + spawns MediaMTX (#2/#3). No IPC/capability layer —
# this is how we prove side-effectful methods that bare logoscore can't (gated returns).
#
# Paths are derived automatically: SDK/module roots from the `nix develop` env, and
# Qt/QtRemoteObjects/OpenSSL from the built plugin's ldd. Needs mediamtx + curl on PATH.
#
# Usage:  ./tests/run-direct-test.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"

echo "Building plugin (for ldd-derived Qt paths)..."
nix build .#default --out-link /tmp/rm-result >/dev/null 2>&1
SO=/tmp/rm-result/lib/radio_module_plugin.so
libdir(){ ldd "$SO" 2>/dev/null | grep -oE "/nix/store/[^ ]*/$1" | head -1 | xargs -r dirname; }
QT_LIB="$(libdir libQt6Core.so.6)";        QT="$(dirname "$QT_LIB")"
QRO_LIB="$(libdir libQt6RemoteObjects.so.6)"; QRO="$(dirname "${QRO_LIB:-$QT_LIB}")"
SSL_LIB="$(ldd "$(find /nix/store -maxdepth 3 -name liblogos_core.so 2>/dev/null | head -1)" 2>/dev/null | grep -oE '/nix/store/[^ ]*/libssl.so.3' | head -1 | xargs -r dirname)"
MOC="$QT/libexec/moc"

# SDK + module include/lib roots come from the dev-shell env (grep past the shell banner).
ROOTS="$(nix develop --command bash -c 'printf "RR %s %s\n" "$LOGOS_CPP_SDK_ROOT" "$LOGOS_MODULE_ROOT"' 2>/dev/null | grep '^RR ')"
SDK="$(awk '{print $2}' <<<"$ROOTS")"; MODULE="$(awk '{print $3}' <<<"$ROOTS")"
SDKLIB="$SDK/lib/liblogos_sdk.a"
# SDK headers transitively include nlohmann/json.hpp — find it in the store.
NLO="$(find /nix/store -maxdepth 1 -type d -name '*nlohmann_json*' 2>/dev/null | head -1)"
echo "QT=$QT  QRO=$QRO  SSL=$SSL_LIB  NLO=$NLO"
echo "SDK=$SDK  MODULE=$MODULE"

MOCINC="-I$SDK/include/cpp -I$SDK/include/core -I$MODULE/include/module_lib -I$QT/include -I$QT/include/QtCore -Isrc"
GINC="-Isrc -I$SDK/include/cpp -I$SDK/include/core -I$MODULE/include/module_lib \
  -isystem $NLO/include \
  -isystem $QT/include -isystem $QT/include/QtCore -isystem $QT/include/QtNetwork \
  -isystem $QT/mkspecs/linux-g++ -isystem $QRO/include/QtRemoteObjects"

"$MOC" $MOCINC src/radio_plugin.h -o /tmp/radio_moc.cpp
g++ -std=c++17 -fPIC $GINC tests/direct_test.cpp src/radio_plugin.cpp /tmp/radio_moc.cpp \
   "$SDKLIB" -L "$SSL_LIB" -lssl -lcrypto \
   -L "$QT/lib" -L "$QRO/lib" -lQt6Core -lQt6Network -lQt6RemoteObjects \
   -Wl,-rpath,"$QT/lib:$QRO/lib:$SSL_LIB" -o /tmp/radio_direct_test
echo "Built. Running (isolated high ports)..."

MTX="${RADIO_MEDIAMTX_BIN:-$(find /nix/store -maxdepth 4 -type f -name mediamtx -path '*/bin/*' 2>/dev/null | head -1)}"
RADIO_MEDIAMTX_BIN="$MTX" RADIO_HLS_PORT=18888 RADIO_API_PORT=19997 \
RADIO_RTMP_PORT=11935 RADIO_WHIP_PORT=18889 RADIO_SRT_PORT=18890 \
  /tmp/radio_direct_test
rc=$?
pkill -x mediamtx 2>/dev/null || true
exit $rc
