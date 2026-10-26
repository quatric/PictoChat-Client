#!/usr/bin/env bash
# Build mbedTLS 3.6 (HTTPS and wss://), configured by config/mbedtls_switch.h,
# into ./portlibs/switch. The devkitPro switch-mbedtls package is the older 2.28.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/switch"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/portlibs.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
GCC="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc"
AR="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-ar"
CF="-O2 -Wall -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ffunction-sections -fdata-sections -I$DEVKITPRO/libnx/include"
MBEDTLS_VERSION=3.6.7
MBEDTLS_SHA256=$(cat "$ROOT/scripts/mbedtls.sha256")

mkdir -p "$PFX/lib" "$PFX/include"

curl -sSLf -o "$WORK/mbedtls.tar.bz2" \
    "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2"
echo "$MBEDTLS_SHA256  $WORK/mbedtls.tar.bz2" | sha256sum -c - 2>/dev/null \
    || echo "$MBEDTLS_SHA256  $WORK/mbedtls.tar.bz2" | shasum -a 256 -c -
tar -xjf "$WORK/mbedtls.tar.bz2" -C "$WORK"
MB="$WORK/mbedtls-$MBEDTLS_VERSION"
mkdir -p "$WORK/mbedobj"
for src in "$MB"/library/*.c; do
    "$GCC" $CF -Os -I"$MB/include" -I"$MB/library" \
        -DMBEDTLS_CONFIG_FILE="\"$ROOT/config/mbedtls_switch.h\"" \
        -c "$src" -o "$WORK/mbedobj/$(basename "${src%.c}").o"
done
rm -f "$PFX/lib/libmbedtls.a"
"$AR" rcs "$PFX/lib/libmbedtls.a" "$WORK"/mbedobj/*.o
rm -rf "$PFX/include/mbedtls" "$PFX/include/psa"
cp -r "$MB/include/mbedtls" "$MB/include/psa" "$PFX/include/"

echo "Built mbedTLS in $PFX"
