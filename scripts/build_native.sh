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

# Android's surface and SDL windowing changes are kept in a patch of their
# own so the PS5 patch stays untouched; apply it to the pinned checkout once.
ANDROID_UI_PATCH="$REPO/mcla-recomp/patches/rexglue-v0.10.0-android-ui.patch"
if [ -d "$REXSDK_DIR/.git" ] && [ ! -f "$REXSDK_DIR/src/ui/surface_android.cpp" ]; then
  echo "== Applying Android UI patch to $REXSDK_DIR"
  git -C "$REXSDK_DIR" apply "$ANDROID_UI_PATCH"
fi

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

# Find and stage all output .so files into jniLibs
find "$BUILD_DIR" -name "*.so" -exec cp -f {} "$JNILIBS/" \;

echo "== Native staging completed"
ls -la "$JNILIBS"
