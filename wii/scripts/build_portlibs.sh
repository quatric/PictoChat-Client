#!/usr/bin/env bash
# Build the libraries PictoChat needs beyond the devkitPro portlibs into
# ./portlibs/ppc:
#   - mbedTLS (HTTPS), configured by config/mbedtls_wii.h
#   - libwiidrc, libwupc and libsicksaxis (Wii U GamePad, Wii U Pro Controller
#     and DualShock 3), the same optional controller libraries GlowWii uses.
#     libwupc needs a small API compatibility patch because its original
#     wrapper targeted libogc internals that have since changed.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/ppc"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/portlibs.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
GCC="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc"
AR="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-ar"
LIBOGC_INC="$DEVKITPRO/libogc/include"
CF="-O2 -Wall -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -I$LIBOGC_INC"
MBEDTLS_VERSION=3.6.7
MBEDTLS_SHA256=$(cat "$ROOT/scripts/mbedtls.sha256")

mkdir -p "$PFX/lib" "$PFX/include/wiidrc" "$PFX/include/wupc"

# ── mbedTLS ──────────────────────────────────────────────────────────────────
curl -sSLf -o "$WORK/mbedtls.tar.bz2" \
    "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2"
echo "$MBEDTLS_SHA256  $WORK/mbedtls.tar.bz2" | sha256sum -c - 2>/dev/null \
    || echo "$MBEDTLS_SHA256  $WORK/mbedtls.tar.bz2" | shasum -a 256 -c -
tar -xjf "$WORK/mbedtls.tar.bz2" -C "$WORK"
MB="$WORK/mbedtls-$MBEDTLS_VERSION"
mkdir -p "$WORK/mbedobj"
for src in "$MB"/library/*.c; do
    "$GCC" $CF -Os -I"$MB/include" -I"$MB/library" \
        -DMBEDTLS_CONFIG_FILE="\"$ROOT/config/mbedtls_wii.h\"" \
        -c "$src" -o "$WORK/mbedobj/$(basename "${src%.c}").o"
done
rm -f "$PFX/lib/libmbedtls.a"
"$AR" rcs "$PFX/lib/libmbedtls.a" "$WORK"/mbedobj/*.o
rm -rf "$PFX/include/mbedtls" "$PFX/include/psa"
cp -r "$MB/include/mbedtls" "$MB/include/psa" "$PFX/include/"

# ── Controllers (as in GlowWii) ──────────────────────────────────────────────
git clone --quiet https://github.com/FIX94/libwiidrc.git "$WORK/libwiidrc"
git -C "$WORK/libwiidrc" checkout --quiet 4e156201db17231ec6664d66e478151a859f8c5b
git clone --quiet https://github.com/FIX94/libwupc.git "$WORK/libwupc"
git -C "$WORK/libwupc" checkout --quiet 24145b35dbc2c1950f6f48079d577acc7efb0c4d
git clone --quiet https://github.com/xerpi/libsicksaxis.git "$WORK/libsicksaxis"
git -C "$WORK/libsicksaxis" checkout --quiet 4ddd49e687a7d2466691fd2c73d8a55a90fe1ca6

"$GCC" $CF -I"$WORK/libwiidrc/include" -I"$WORK/libwiidrc/source" \
    -c "$WORK/libwiidrc/source/wiidrc.c" -o "$WORK/wiidrc.o"
"$AR" rcs "$PFX/lib/libwiidrc.a" "$WORK/wiidrc.o"
cp "$WORK/libwiidrc/include/wiidrc/wiidrc.h" "$PFX/include/wiidrc/"

python3 - "$WORK/libwupc/source/wupc.c" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
s = p.read_text()
s = s.replace(
    "extern __typeof(wiiuse_register) __real_wiiuse_register;",
    "extern __typeof(wiiuse_accept) __real_wiiuse_accept;")
s = s.replace(
    "bte_registerdeviceasync(stat->sock, _bdaddr, __WUPC_HandleConnect);",
    "bte_listenasync(stat->sock, _bdaddr, __WUPC_HandleConnectStep2);")
marker = "static s32 __WUPC_HandleDisconnect(void *arg,struct bte_pcb *pcb __attribute__((unused)),u8 err)"
step2 = """static s32 __WUPC_HandleConnectStep2(void *arg,struct bte_pcb *pcb,u8 err)
{
    if(err != ERR_OK)
    {
        bte_disconnect(pcb);
        return err;
    }
    return bte_listenasync_step2(pcb, __WUPC_HandleConnect);
}

"""
s = s.replace(marker, step2 + marker)
s = s.replace(
    "int __wrap_wiiuse_register(struct wiimote_listen_t *wml, struct bd_addr *bdaddr, struct wiimote_t *(*assign_cb)(struct bd_addr *bdaddr))",
    "int __wrap_wiiuse_accept(struct wiimote_listen_t *wml, struct bd_addr *bdaddr, const u8 *name, struct wiimote_t *(*assign_cb)(wiimote_listen *wml, u8 err))")
s = s.replace(
    "return __real_wiiuse_register(wml,bdaddr,assign_cb);",
    "return __real_wiiuse_accept(wml,bdaddr,name,assign_cb);")
p.write_text(s)
PY

"$GCC" $CF -I"$WORK/libwupc/include" -I"$WORK/libwupc/source" \
    -c "$WORK/libwupc/source/wupc.c" -o "$WORK/wupc.o"
"$AR" rcs "$PFX/lib/libwupc.a" "$WORK/wupc.o"
cp "$WORK/libwupc/include/wupc/wupc.h" "$PFX/include/wupc/"

"$GCC" $CF -c "$WORK/libsicksaxis/libsicksaxis/sicksaxis.c" \
    -o "$WORK/sicksaxis.o"
"$AR" rcs "$PFX/lib/libsicksaxis.a" "$WORK/sicksaxis.o"
cp "$WORK/libsicksaxis/libsicksaxis/sicksaxis.h" "$PFX/include/"

echo "Built mbedTLS and controller libraries in $PFX"
