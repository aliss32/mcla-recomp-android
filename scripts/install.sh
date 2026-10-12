#!/bin/bash
# Install the APK and (unless --no-launch) start it with a fresh logcat.
# usage: install.sh [debug|release] [--no-launch]
set -euo pipefail
. "$(dirname "$0")/env.sh"
VARIANT=debug
LAUNCH=1
for arg in "$@"; do
  case "$arg" in
    --no-launch) LAUNCH=0 ;;
    *) VARIANT="$arg" ;;
  esac
done
APK="$APP_DIR/app/build/outputs/apk/$VARIANT/app-$VARIANT.apk"
[ -f "$APK" ] || { echo "no APK at $APK - run build_apk.sh $VARIANT"; exit 1; }

adbs install -r -g "$(winpath "$APK")"
if [ "$LAUNCH" = "1" ]; then
  adbs shell am force-stop "$PKG"
  adbs logcat -c
  adbs shell am start -n "$ACTIVITY" --ez play true
fi
