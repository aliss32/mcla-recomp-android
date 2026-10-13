#!/bin/bash
# Shared environment for the LibertyRecomp Android scripts. Source it:
#   . os/android/scripts/env.sh
# Supports Git Bash on Windows and Linux/macOS CI; every path can be overridden.

_scripts_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export APP_DIR="$(cd "$_scripts_dir/.." && pwd)"
export REPO="$APP_DIR"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    if [ -z "${ANDROID_HOME:-}" ]; then
      if [ -n "${LOCALAPPDATA:-}" ]; then
        ANDROID_HOME="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk"
      else
        ANDROID_HOME="$HOME/Android/Sdk"
      fi
    fi
    ;;
  *)
    ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-${HOME}/Android/Sdk}}"
    ;;
esac
export ANDROID_HOME
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$ANDROID_HOME}"
export NDK_VER="${NDK_VER:-27.2.12479018}"
export ANDROID_NDK="${ANDROID_NDK:-$ANDROID_HOME/ndk/$NDK_VER}"
if [ -z "${JAVA_HOME:-}" ]; then
  if [ -n "${ANDROID_STUDIO_JBR:-}" ]; then
    JAVA_HOME="$ANDROID_STUDIO_JBR"
  elif [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* || "$(uname -s)" == CYGWIN* ]]; then
    if command -v java >/dev/null 2>&1; then
      _java_windows="$(cygpath -w "$(command -v java)")"
      _java_bin="$(dirname "$_java_windows")"
      JAVA_HOME="$(cygpath -u "$_java_bin")"
    else
      JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
    fi
  elif command -v java >/dev/null 2>&1; then
    _java_path="$(readlink -f "$(command -v java)")"
    JAVA_HOME="$(dirname "$_java_path")"
  else
    echo "ERROR: JAVA_HOME is unset and java was not found on PATH" >&2
    return 1 2>/dev/null || exit 1
  fi
fi
export JAVA_HOME
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) _host=windows-x86_64 ;;
  Darwin)               _host=darwin-x86_64 ;;
  *)                    _host=linux-x86_64 ;;
esac
export NDK_BIN="$ANDROID_NDK/toolchains/llvm/prebuilt/$_host/bin"
export NDK_SYSROOT_LIB="$ANDROID_NDK/toolchains/llvm/prebuilt/$_host/sysroot/usr/lib/aarch64-linux-android"

export BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
case "$BUILD_TYPE" in
  Release) _build_suffix=-release ;;
  *)       _build_suffix= ;;
esac
export BUILD_DIR="${BUILD_DIR:-$REPO/out/build/android-arm64$_build_suffix}"
export JNILIBS="$APP_DIR/app/src/main/jniLibs/arm64-v8a"
export ASSETS="$APP_DIR/app/src/main/assets"

export PKG="${PKG:-com.mcla}"
# The launcher; "--ez play true" makes it start the game at once.
export ACTIVITY="${ACTIVITY:-$PKG/.LauncherActivity}"
# The game runs in its own process.
export GAME_PROC="$PKG:game"
export FILES="/sdcard/Android/data/$PKG/files"
export GAME_DIR="$FILES/MclaRecomp/game"

export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"

# Whichever single device is attached, unless SERIAL is set.
if [ -z "${SERIAL:-}" ]; then
  _ready=$(adb devices 2>/dev/null | awk 'NR>1 && $2=="device" {print $1}')
  if [ "$(printf '%s\n' "$_ready" | grep -c .)" = "1" ]; then
    SERIAL="$_ready"
  fi
fi
export SERIAL="${SERIAL:-}"
# Git Bash rewrites arguments that look like POSIX paths (/sdcard/...) into
# Windows paths; adb must see them unchanged.
# ADB_TIMEOUT (seconds, optional) ends an adb call that hangs - run-as shells
# occasionally never return while the game is busy.
adbs() {
  local limit=()
  [ -n "${ADB_TIMEOUT:-}" ] && limit=(timeout "$ADB_TIMEOUT")
  if [ -n "$SERIAL" ]; then
    MSYS_NO_PATHCONV=1 "${limit[@]}" adb -s "$SERIAL" "$@"
  else
    MSYS_NO_PATHCONV=1 "${limit[@]}" adb "$@"
  fi
}
# Local file arguments for adbs (push/install) in a form adb.exe understands.
winpath() { cygpath -m "$1" 2>/dev/null || printf '%s\n' "$1"; }
