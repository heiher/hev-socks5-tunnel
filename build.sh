#!/bin/bash

# buildStatic iphoneos -mios-version-min=16.0 arm64
buildStatic()
{
	echo "build for '$1', '$2', '$3'"
     make PP="xcrun --sdk $1 --toolchain $1 clang" \
          CC="xcrun --sdk $1 --toolchain $1 clang" \
          CFLAGS="-arch $3 $2" \
          LFLAGS="-arch $3 $2 -Wl,-Bsymbolic-functions" static

     local OUTPUT_ARCH_FILE="libhev-socks5-tunnel-$1-$3.a"

     libtool -static -o libhev-socks5-tunnel-$1-$3.a \
                   bin/libhev-socks5-tunnel.a \
                   third-part/lwip/bin/liblwip.a \
                   third-part/yaml/bin/libyaml.a \
                   third-part/hev-task-system/bin/libhev-task-system.a
     make clean
}

buildStatic iphoneos -mios-version-min=16.0 arm64
buildStatic iphonesimulator -mios-version-min=16.0 x86_64
buildStatic iphonesimulator -mios-version-min=16.0 arm64
buildStatic macosx -mmacosx-version-min=13.3 x86_64
buildStatic macosx -mmacosx-version-min=13.3 arm64

libtool -static -o libhev-socks5-tunnel-ios.a \
               libhev-socks5-tunnel-iphoneos-arm64.a \
               libhev-socks5-tunnel-iphonesimulator-x86_64.a \
               libhev-socks5-tunnel-iphonesimulator-arm64.a

libtool -static -o libhev-socks5-tunnel-macosx.a \
               libhev-socks5-tunnel-macosx-arm64.a \
               libhev-socks5-tunnel-macosx-x86_64.a

rm -rf libhev-socks5-tunnel-macosx-arm64.a
rm -rf libhev-socks5-tunnel-macosx-x86_64.a
rm -rf libhev-socks5-tunnel-iphoneos-arm64.a
rm -rf libhev-socks5-tunnel-iphonesimulator-x86_64.a
rm -rf libhev-socks5-tunnel-iphonesimulator-arm64.a