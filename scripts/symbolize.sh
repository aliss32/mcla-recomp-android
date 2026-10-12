#!/bin/bash
# Turn the native crash in the current logcat buffer into source lines,
# using the unstripped libraries from the build output.
set -euo pipefail
. "$(dirname "$0")/env.sh"
# ndk-stack takes one directory; gather the unstripped libraries in one place.
SYMS="$BUILD_DIR/symbols"
mkdir -p "$SYMS"
cp -u "$BUILD_DIR"/glue/gta4-recomp/libmain.so "$REX_OUT_DIR"/*.so "$SYMS/"
adbs logcat -d | "$ANDROID_NDK/ndk-stack" -sym "$SYMS"
