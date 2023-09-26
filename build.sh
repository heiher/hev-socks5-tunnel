#!/bin/bash

# buildStatic iphoneos -mios-version-min=15.0 arm64
buildStatic()
{
     echo "build for '$1', '$2', '$3'"
     make PP="xcrun --sdk $1 --toolchain $1 clang" \
          CC="xcrun --sdk $1 --toolchain $1 clang" \
          CFLAGS="-arch $3 $2" \
          LFLAGS="-arch $3 $2 -Wl,-Bsymbolic-functions" static

     local OUTPUT_ARCH_FILE="libhev-socks5-tunnel-$1-$3.a"

     libtool -static -o $OUTPUT_ARCH_FILE \
                   bin/libhev-socks5-tunnel.a \
                   third-part/lwip/bin/liblwip.a \
                   third-part/yaml/bin/libyaml.a \
                   third-part/hev-task-system/bin/libhev-task-system.a
     make clean
}

mergeStatic()
{
     local AMD_LIB_FILE="libhev-socks5-tunnel-$1-x86_64.a"
     local ARM_LIB_FILE="libhev-socks5-tunnel-$1-arm64.a"
     local OUTPUT_LIB_FILE="libhev-socks5-tunnel-$1.a"
     lipo -create \
	-arch x86_64 $AMD_LIB_FILE \
	-arch arm64  $ARM_LIB_FILE \
	-output $OUTPUT_LIB_FILE
}

buildStatic iphoneos -mios-version-min=15.0 arm64
buildStatic iphonesimulator -miphonesimulator-version-min=15.0 x86_64
buildStatic iphonesimulator -miphonesimulator-version-min=15.0 arm64
mergeStatic iphonesimulator

buildStatic macosx -mmacosx-version-min=12.0 x86_64
buildStatic macosx -mmacosx-version-min=12.0 arm64
mergeStatic macosx

cp module.modulemap include/
rm -rf HevSocks5Tunnel.xcframework
xcodebuild -create-xcframework \
    -library libhev-socks5-tunnel-iphoneos-arm64.a -headers include \
    -library libhev-socks5-tunnel-iphonesimulator.a -headers include \
    -library libhev-socks5-tunnel-macosx.a -headers include \
    -output HevSocks5Tunnel.xcframework

rm -rf include/module.modulemap
rm -rf libhev-socks5-tunnel-*.a
