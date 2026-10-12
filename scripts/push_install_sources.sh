#!/bin/bash
# Copy the installation sources to the device and preselect them, so the
# in-app installer runs on the next launch without a file picker.
#
#   push_install_sources.sh <disc.iso> [<title update: default.xexp or STFS package>]
#
# Both land in files/install/ (the app reads them by path, no storage
# permission needed). The cvars go into files/args.txt next to whatever
# debug profile is already there. After a successful install the sources can
# be deleted with: push_install_sources.sh --clean
set -euo pipefail
. "$(dirname "$0")/env.sh"
DEST="$FILES/install"

if [ "${1:-}" = "--clean" ]; then
  adbs shell rm -rf "$DEST"
  echo "removed $DEST"
  exit 0
fi
[ -n "${1:-}" ] || { echo "usage: push_install_sources.sh <disc.iso> [<title update>] | --clean"; exit 1; }

ISO="$1"
TU="${2:-}"
adbs shell mkdir -p "$DEST"

remote_size() { adbs shell "stat -c %s '$1' 2>/dev/null" | tr -d '\r'; }

push_if_needed() {
  local src="$1" dst="$2"
  local size
  size=$(stat -c %s "$src")
  if [ "$(remote_size "$dst")" = "$size" ]; then
    echo "already on device: $dst"
  else
    echo "pushing $(basename "$src") ($((size / 1048576)) MiB) -> $dst"
    adbs push "$(winpath "$src")" "$dst"
  fi
}

push_if_needed "$ISO" "$DEST/game.iso"
ARGS_TMP="$(mktemp)"
adbs shell "cat '$FILES/args.txt' 2>/dev/null" | tr -d '\r' | grep -v '^--install_' > "$ARGS_TMP" || true
echo "--install_game_source=$DEST/game.iso" >> "$ARGS_TMP"
if [ -n "$TU" ]; then
  case "$TU" in
    *.xexp) TU_DST="$DEST/default.xexp" ;;
    *)      TU_DST="$DEST/title_update" ;;
  esac
  push_if_needed "$TU" "$TU_DST"
  echo "--install_update_source=$TU_DST" >> "$ARGS_TMP"
fi
adbs push "$(winpath "$ARGS_TMP")" "$FILES/args.txt" >/dev/null
rm -f "$ARGS_TMP"
echo "args.txt now:"
adbs shell cat "$FILES/args.txt" | sed 's/^/  /'
