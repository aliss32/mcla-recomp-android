#!/bin/bash
# Native build script for Midnight Club: Los Angeles (Android ARM64)
# Stages libSDL3.so, libmain.so, librexruntime.so, librexgpu-xenos.so and libc++_shared.so into jniLibs/arm64-v8a
set -euo pipefail
. "$(dirname "$0")/env.sh"

JOBS="${1:-$(( $(nproc) > 3 ? $(nproc) - 2 : 1 ))}"

[ -d "$ANDROID_NDK" ] || { echo "no NDK at $ANDROID_NDK"; exit 1; }

CPU_FLAGS="-march=armv8.2-a -fno-emulated-tls -fexperimental-library"
SDL3_SRC="${SDL3_SRC:-}"
REXSDK_DIR="${REXSDK_DIR:-/tmp/rexglue-sdk}"
# Android patches are applied to the pinned SDK checkout by the CI workflow via
# scripts/patch_rexglue_android.py; for local builds run it once on $REXSDK_DIR
# after the mcla/minmax patches.
mkdir -p "$JNILIBS"

# Configure and build MCLA native
echo "== Configuring MCLA Android ARM64 in $BUILD_DIR"
cmake -S "$REPO/mcla-recomp" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_C_FLAGS="$CPU_FLAGS" \
  -DCMAKE_CXX_FLAGS="$CPU_FLAGS" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared \
  -DREXGLUE_USE_VULKAN=ON \
  -DREXGLUE_USE_D3D12=OFF \
  -DREXGLUE_ENABLE_TRACY=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DREXSDK_DIR="$REXSDK_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== Building MCLA target with $JOBS jobs"
cmake --build "$BUILD_DIR" --target mcla rexgpu-xenos -- -j"$JOBS"

echo "== Staging native libraries"
cp "$NDK_SYSROOT_LIB/libc++_shared.so" "$JNILIBS/"

# Find and stage all output .so files into jniLibs (the SDK links its shared
# libs into $REXSDK_DIR/out, outside $BUILD_DIR)
find "$BUILD_DIR" "$REXSDK_DIR/out" -name "*.so" -exec cp -f {} "$JNILIBS/" \;

echo "== Native staging completed"
ls -la "$JNILIBS"
