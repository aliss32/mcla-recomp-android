sen
#!/bin/bash
# Quick staging check: emulate the resources part of build_native.sh
set -euo pipefail
REPO=/mnt/c/Users/aliha/CodeGPT/mcla-android-port/MclaAndroidPort
ASSETS=$REPO/app/src/main/assets
JNILIBS=$REPO/app/src/main/jniLibs/arm64-v8a
BUILD_DIR=$REPO/out/build/android-arm64
REXSDK_DIR=/mnt/c/Users/aliha/CodeGPT/mcla-android-port/_sdktest/rexglue-sdk
LIBERTY_LIB=${LIBERTY_RECOMP_LIB:-/mnt/c/Users/aliha/CodeGPT/mcla-android-port/_liberty_ref/LibertyRecompLib}

echo "== Staging title resources into $ASSETS/Resources"
rm -rf "$ASSETS/Resources"
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
if [ -d "$LIBERTY_LIB/private/button_prompts" ]; then
  for _variant in "$LIBERTY_LIB/private/button_prompts"/*/; do
    if [ -f "$_variant/buttons_360.xtd" ]; then
      _name=$(basename "$_variant")
      mkdir -p "$ASSETS/Resources/button_prompts/$_name"
      cp -f "$_variant/buttons_360.xtd" "$ASSETS/Resources/button_prompts/$_name/"
    fi
  done
fi
if [ -f "$LIBERTY_LIB/aes_key.bin" ]; then
  cp -f "$LIBERTY_LIB/aes_key.bin" "$ASSETS/Resources/"
fi
echo "files staged:"
find "$ASSETS/Resources" -type f | wc -l
