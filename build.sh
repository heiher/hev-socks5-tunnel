#!/bin/bash

set -e

XCFRAMEWORK_DIR="./apple_xcframework"

# buildStatic iphoneos -mios-version-min=15.0 arm64
buildStatic()
{
     echo "build for $1, $2, min version $3"

     local MIN_VERSION="-m$1-version-min=$3"
     make PP="xcrun --sdk $1 --toolchain $1 clang" \
          CC="xcrun --sdk $1 --toolchain $1 clang" \
          CFLAGS="-arch $2 $MIN_VERSION" \
          LFLAGS="-arch $2 $MIN_VERSION -Wl,-Bsymbolic-functions" static

     local OUTPUT_DIR="$XCFRAMEWORK_DIR/$1-$2"
     mkdir -p $OUTPUT_DIR
     local OUTPUT_ARCH_FILE="$OUTPUT_DIR/libhev-socks5-tunnel.a"

     libtool -static -o $OUTPUT_ARCH_FILE \
                   bin/libhev-socks5-tunnel.a \
                   third-part/lwip/bin/liblwip.a \
                   third-part/yaml/bin/libyaml.a \
                   third-part/hev-task-system/bin/libhev-task-system.a
     make clean
}

mergeStatic()
{
     echo "merge for $1, $2, $3"
     local FIRST_LIB_FILE="$XCFRAMEWORK_DIR/$1-$2/libhev-socks5-tunnel.a"
     local SECOND_LIB_FILE="$XCFRAMEWORK_DIR/$1-$3/libhev-socks5-tunnel.a"
     local OUTPUT_DIR="$XCFRAMEWORK_DIR/$1-$2-$3"
     mkdir -p $OUTPUT_DIR
     local OUTPUT_ARCH_FILE="$OUTPUT_DIR/libhev-socks5-tunnel.a"
     lipo -create \
          -arch $2 $FIRST_LIB_FILE \
          -arch $3 $SECOND_LIB_FILE \
          -output $OUTPUT_ARCH_FILE
}

rm -rf $XCFRAMEWORK_DIR
rm -rf HevSocks5Tunnel.xcframework
mkdir $XCFRAMEWORK_DIR

buildStatic iphoneos arm64 15.0
buildStatic iphonesimulator x86_64 15.0
buildStatic iphonesimulator arm64 15.0
mergeStatic iphonesimulator x86_64 arm64

# keep same with flutter
buildStatic macosx x86_64 10.14
buildStatic macosx arm64 10.14
mergeStatic macosx x86_64 arm64

buildStatic appletvos arm64 17.0
buildStatic appletvsimulator x86_64 17.0
buildStatic appletvsimulator arm64 17.0
mergeStatic appletvsimulator x86_64 arm64

INCLUDE_DIR="$XCFRAMEWORK_DIR/include"
mkdir -p $INCLUDE_DIR
cp ./src/hev-main.h $INCLUDE_DIR
cp ./module.modulemap $INCLUDE_DIR
xcodebuild -create-xcframework \
    -library ./apple_xcframework/iphoneos-arm64/libhev-socks5-tunnel.a -headers $INCLUDE_DIR \
    -library ./apple_xcframework/iphonesimulator-x86_64-arm64/libhev-socks5-tunnel.a -headers $INCLUDE_DIR \
    -library ./apple_xcframework/macosx-x86_64-arm64/libhev-socks5-tunnel.a -headers $INCLUDE_DIR \
    -library ./apple_xcframework/appletvos-arm64/libhev-socks5-tunnel.a -headers $INCLUDE_DIR \
    -library ./apple_xcframework/appletvsimulator-x86_64-arm64/libhev-socks5-tunnel.a -headers $INCLUDE_DIR \
    -output ./HevSocks5Tunnel.xcframework

rm -rf ./apple_xcframework
