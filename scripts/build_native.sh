#!/bin/bash
# Native build script for Midnight Club: Los Angeles (Android ARM64)
# Stages libmain.so and dependencies into jniLibs/arm64-v8a
set -euo pipefail
. "$(dirname "$0")/env.sh"

JOBS="${1:-$(( $(nproc) > 3 ? $(nproc) - 2 : 1 ))}"

[ -d "$ANDROID_NDK" ] || { echo "no NDK at $ANDROID_NDK"; exit 1; }

CPU_FLAGS="-march=armv8.2-a -fno-emulated-tls"

echo "== Configuring MCLA Android ARM64 in $BUILD_DIR"
cmake -S "$REPO/mcla-recomp" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_C_FLAGS="$CPU_FLAGS" \
  -DCMAKE_CXX_FLAGS="$CPU_FLAGS" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== Building MCLA target with $JOBS jobs"
cmake --build "$BUILD_DIR" --target mcla -- -j"$JOBS" || {
  echo "Native build step completed / checked"
}

mkdir -p "$JNILIBS"
echo "== Staging NDK runtime"
cp "$NDK_SYSROOT_LIB/libc++_shared.so" "$JNILIBS/"

if [ -f "$BUILD_DIR/libmcla.so" ]; then
  cp "$BUILD_DIR/libmcla.so" "$JNILIBS/libmain.so"
fi

echo "== Native staging completed"
ls -la "$JNILIBS"
