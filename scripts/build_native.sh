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
cmake --build "$BUILD_DIR" --target mcla rexgpu-xenos rexgpu-mcla-native -- -j"$JOBS"

echo "== Staging native libraries"
cp "$NDK_SYSROOT_LIB/libc++_shared.so" "$JNILIBS/"

# Find and stage all output .so files into jniLibs (the SDK links its shared
# libs into $REXSDK_DIR/out, outside $BUILD_DIR)
find "$BUILD_DIR" "$REXSDK_DIR/out" -name "*.so" -exec cp -f {} "$JNILIBS/" \;

# Stage the title resources the Java host copies out of the APK into
# REX_RESOURCES_DIR on first launch: font atlases, button prompts and the RPF
# AES key. Mirrors the macOS bundle's Resources/ layout.
LIBERTY_LIB="${LIBERTY_RECOMP_LIB:-$REPO/../_liberty_ref/LibertyRecompLib}"
if [ -d "$LIBERTY_LIB" ]; then
  echo "== Staging title resources into $ASSETS/Resources"
  rm -rf "$ASSETS/Resources"
  mkdir -p "$ASSETS/Resources"
  # Font atlases (base + episode variants).
  mkdir -p "$ASSETS/Resources/font_atlases"
  cp -f "$LIBERTY_LIB/font_atlases/font1.png" \
        "$LIBERTY_LIB/font_atlases/font2.png" \
        "$LIBERTY_LIB/font_atlases/font3.png" \
        "$LIBERTY_LIB/font_atlases/manifest.json" "$ASSETS/Resources/font_atlases/"
  for _ep in tlad tbogt; do
    if [ -d "$LIBERTY_LIB/font_atlases/$_ep" ]; then
      mkdir -p "$ASSETS/Resources/font_atlases/$_ep"
      cp -f "$LIBERTY_LIB/font_atlases/$_ep/font"*.png \
            "$LIBERTY_LIB/font_atlases/$_ep/manifest.json" \
            "$ASSETS/Resources/font_atlases/$_ep/" 2>/dev/null || true
    fi
  done
  # Button prompts (each variant dir holding buttons_360.xtd).
  if [ -d "$LIBERTY_LIB/private/button_prompts" ]; then
    for _variant in "$LIBERTY_LIB/private/button_prompts"/*/; do
      if [ -f "$_variant/buttons_360.xtd" ]; then
        _name=$(basename "$_variant")
        mkdir -p "$ASSETS/Resources/button_prompts/$_name"
        cp -f "$_variant/buttons_360.xtd" "$ASSETS/Resources/button_prompts/$_name/"
      fi
    done
  fi
  # RPF AES key.
  if [ -f "$LIBERTY_LIB/aes_key.bin" ]; then
    cp -f "$LIBERTY_LIB/aes_key.bin" "$ASSETS/Resources/"
  fi
  ls -laR "$ASSETS/Resources" | head -40
else
  echo "WARN: LibertyRecompLib not found at $LIBERTY_LIB - assets/Resources not staged"
fi

echo "== Native staging completed"
ls -la "$JNILIBS"
