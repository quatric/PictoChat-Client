#!/usr/bin/env bash
# Build the libraries PictoChat needs into ./portlibs/3ds:
#   - zlib, libpng and FreeType (the pictures and fonts). devkitPro ships these
#     as 3ds-zlib, 3ds-libpng and 3ds-freetype too; building them here means
#     the app builds without installing anything but devkitARM and libctru.
#   - mbedTLS (wss:// and https://), configured by config/mbedtls_3ds.h. The
#     3DS's own SSL service is too old for many servers, so TLS is done here.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/3ds"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/portlibs.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
GCC="$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc"
AR="$DEVKITPRO/devkitARM/bin/arm-none-eabi-ar"
CF="-O2 -Wall -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -ffunction-sections -D__3DS__ -I$DEVKITPRO/libctru/include"

ZLIB_VERSION=1.3.1
LIBPNG_VERSION=1.6.47
FREETYPE_VERSION=2.13.3
MBEDTLS_VERSION=3.6.7

fetch() { # url file sha256
    curl -sSLf -o "$2" "$1"
    echo "$3  $2" | sha256sum -c - >/dev/null 2>&1 || echo "$3  $2" | shasum -a 256 -c - >/dev/null \
        || { echo "Checksum mismatch for $1"; exit 1; }
}

# compile every listed source into one static library: lib name, flags, sources...
build_lib() {
    local name="$1" flags="$2"
    shift 2
    mkdir -p "$WORK/obj-$name"
    for src in "$@"; do
        "$GCC" $CF $flags -c "$src" -o "$WORK/obj-$name/$(basename "${src%.c}").o"
    done
    rm -f "$PFX/lib/lib$name.a"
    "$AR" rcs "$PFX/lib/lib$name.a" "$WORK/obj-$name"/*.o
}

mkdir -p "$PFX/lib" "$PFX/include"

# ── zlib ─────────────────────────────────────────────────────────────────────
fetch "https://github.com/madler/zlib/releases/download/v$ZLIB_VERSION/zlib-$ZLIB_VERSION.tar.gz" \
    "$WORK/zlib.tar.gz" 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
tar -xzf "$WORK/zlib.tar.gz" -C "$WORK"
Z="$WORK/zlib-$ZLIB_VERSION"
build_lib z "-DHAVE_UNISTD_H" $(for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast \
    inflate inftrees trees uncompr zutil; do echo "$Z/$f.c"; done)
cp "$Z/zlib.h" "$Z/zconf.h" "$PFX/include/"

# ── libpng ───────────────────────────────────────────────────────────────────
fetch "https://github.com/pnggroup/libpng/archive/refs/tags/v$LIBPNG_VERSION.tar.gz" \
    "$WORK/libpng.tar.gz" 631a4c58ea6c10c81f160c4b21fa8495b715d251698ebc2552077e8450f30454
tar -xzf "$WORK/libpng.tar.gz" -C "$WORK"
P="$WORK/libpng-$LIBPNG_VERSION"
cp "$P/scripts/pnglibconf.h.prebuilt" "$P/pnglibconf.h"
build_lib png16 "-I$PFX/include -I$P -DPNG_ARM_NEON_OPT=0" $(ls "$P"/png*.c | grep -v pngtest.c)
cp "$P/png.h" "$P/pngconf.h" "$P/pnglibconf.h" "$PFX/include/"

# ── FreeType: TrueType, CFF and WOFF; smooth and monochrome rendering ────────
fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VERSION.tar.xz" \
    "$WORK/freetype.tar.xz" 0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
tar -xJf "$WORK/freetype.tar.xz" -C "$WORK"
F="$WORK/freetype-$FREETYPE_VERSION"
cat > "$F/include/pc_ftmodule.h" <<'EOF'
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
EOF
build_lib freetype "-I$F/include -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<pc_ftmodule.h>" \
    "$F"/src/base/{ftsystem,ftinit,ftdebug,ftbase,ftbbox,ftglyph,ftbitmap,ftmm}.c \
    "$F"/src/autofit/autofit.c "$F"/src/truetype/truetype.c "$F"/src/cff/cff.c "$F"/src/psaux/psaux.c \
    "$F"/src/psnames/psnames.c "$F"/src/pshinter/pshinter.c "$F"/src/sfnt/sfnt.c "$F"/src/smooth/smooth.c "$F"/src/raster/raster.c \
    "$F"/src/gzip/ftgzip.c
rm -rf "$PFX/include/freetype2"
mkdir -p "$PFX/include/freetype2"
cp -r "$F/include/freetype" "$F/include/ft2build.h" "$PFX/include/freetype2/"

# ── mbedTLS ──────────────────────────────────────────────────────────────────
fetch "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2" \
    "$WORK/mbedtls.tar.bz2" "$(cat "$ROOT/scripts/mbedtls.sha256")"
tar -xjf "$WORK/mbedtls.tar.bz2" -C "$WORK"
MB="$WORK/mbedtls-$MBEDTLS_VERSION"
build_lib mbedtls "-Os -I$MB/include -I$MB/library -DMBEDTLS_CONFIG_FILE=\"$ROOT/config/mbedtls_3ds.h\"" "$MB"/library/*.c
rm -rf "$PFX/include/mbedtls" "$PFX/include/psa"
cp -r "$MB/include/mbedtls" "$MB/include/psa" "$PFX/include/"

echo "Built zlib, libpng, FreeType and mbedTLS in $PFX"
