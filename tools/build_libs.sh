#!/usr/bin/env bash
# Build the third-party Wii libraries this app links against into libs/ppc:
#
#   libmbedtls.a      mbedTLS 3.6.7 (HTTPS), the copy vendored by libwiiupdate,
#                     compiled with config/mbedtls_wii.h (as PictoChat-Client
#                     does in wii/scripts/build_portlibs.sh)
#   libwiiupdate.a    quatric/libwiiupdate: GitHub release check + installer
#   libwiiosk.a       quatric/libwiiosk: the Wii system-style on-screen
#                     keyboard used for the city search
#
# libwiiinput is *not* built here: it is a git submodule-style source drop in
# libs/libwiiinput and is compiled straight into the app by the Makefile, which
# is what its README recommends so the HAVE_* options apply to app and library
# alike.
#
# Run from the project root, inside devkitPro's MSYS2 shell:
#     bash tools/build_libs.sh
# build.ps1 calls this automatically when the libraries are missing.
set -e

: "${DEVKITPRO:=/c/devkitPro}"
export DEVKITPRO
export DEVKITPPC="$DEVKITPRO/devkitPPC"

CC="$DEVKITPPC/bin/powerpc-eabi-gcc"
AR="$DEVKITPPC/bin/powerpc-eabi-ar"
PFX="libs/ppc"
MB="libs/libwiiupdate/vendor/mbedtls"

if [ ! -d "$MB" ]; then
    echo "missing $MB - the libwiiupdate vendor drop is not complete" >&2
    exit 1
fi

mkdir -p "$PFX/lib" "$PFX/include" build/mbedtls

ARCH="-DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float"
CFLAGS="-O2 -Wall $ARCH -I$DEVKITPRO/libogc/include"
TLSFLAGS="-Os $ARCH -I$MB/include -I$MB/library -I$DEVKITPRO/libogc/include \
          -DMBEDTLS_CONFIG_FILE=\"mbedtls_wii.h\" -I$PWD/config"

echo "== mbedTLS 3.6.7 =="
# Compile in parallel; mbedTLS is by far the slowest step of a clean build.
jobs=$(nproc 2>/dev/null || echo 4)
running=0
for src in "$MB"/library/*.c; do
    out="build/mbedtls/$(basename "${src%.c}").o"
    "$CC" $TLSFLAGS -c "$src" -o "$out" &
    running=$((running + 1))
    if [ "$running" -ge "$jobs" ]; then
        wait
        running=0
    fi
done
wait
rm -f "$PFX/lib/libmbedtls.a"
"$AR" rcs "$PFX/lib/libmbedtls.a" build/mbedtls/*.o
rm -rf "$PFX/include/mbedtls" "$PFX/include/psa"
cp -r "$MB/include/mbedtls" "$MB/include/psa" "$PFX/include/"

echo "== libwiiupdate =="
make -C libs/libwiiupdate -j "$(nproc 2>/dev/null || echo 4)"
cp libs/libwiiupdate/lib/libwiiupdate.a "$PFX/lib/"
cp libs/libwiiupdate/include/wiiupdate.h "$PFX/include/"

echo "== libwiiosk =="
# Static archive so only the dictionary objects the app references get linked
# (OSK_UseSystemLanguage() would pull in all nine, ~630 KB).
make -C libs/libwiiosk -j "$(nproc 2>/dev/null || echo 4)"
cp libs/libwiiosk/lib/libwiiosk.a "$PFX/lib/"
cp libs/libwiiosk/include/wiiosk.h libs/libwiiosk/include/wiiosk_input_wii.h \
   "$PFX/include/"

echo "Built:"
ls -l "$PFX/lib"
