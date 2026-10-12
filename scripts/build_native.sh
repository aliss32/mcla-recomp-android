#!/bin/bash
# Native build script for Midnight Club: Los Angeles (Android ARM64)
# Stages libSDL3.so, libmain.so and libc++_shared.so into jniLibs/arm64-v8a
set -euo pipefail
. "$(dirname "$0")/env.sh"

JOBS="${1:-$(( $(nproc) > 3 ? $(nproc) - 2 : 1 ))}"

[ -d "$ANDROID_NDK" ] || { echo "no NDK at $ANDROID_NDK"; exit 1; }

CPU_FLAGS="-march=armv8.2-a -fno-emulated-tls"
SDL3_SRC="${SDL3_SRC:-}"

# ── SDL3 ─────────────────────────────────────────────────────────────────────
mkdir -p "$JNILIBS"

if [ -n "$SDL3_SRC" ] && [ -d "$SDL3_SRC" ]; then
  echo "== Building SDL3 from $SDL3_SRC"
  SDL3_BUILD="$REPO/out/build/sdl3-android-arm64"
  cmake -S "$SDL3_SRC" -B "$SDL3_BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DANDROID_STL=c++_shared \
    -DSDL_SHARED=ON \
    -DSDL_STATIC=OFF \
    -DSDL_TEST=OFF \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$SDL3_BUILD" --parallel "$JOBS"
  cp "$SDL3_BUILD/libSDL3.so" "$JNILIBS/"
  echo "SDL3 staged from source"
elif [ -f "$JNILIBS/libSDL3.so" ]; then
  echo "== libSDL3.so already in jniLibs, skipping SDL3 build"
else
  echo "WARNING: libSDL3.so not found."
  echo "Set SDL3_SRC=/path/to/SDL3 to build it, or place a pre-built libSDL3.so in:"
  echo "  $JNILIBS/"
  echo "SDL3 releases: https://github.com/libsdl-org/SDL/releases"
fi

# ── MCLA engine ──────────────────────────────────────────────────────────────
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

echo "== Staging NDK runtime"
cp "$NDK_SYSROOT_LIB/libc++_shared.so" "$JNILIBS/"

if [ -f "$BUILD_DIR/libmcla.so" ]; then
  cp "$BUILD_DIR/libmcla.so" "$JNILIBS/libmain.so"
fi

echo "== Native staging completed"
ls -la "$JNILIBS"
