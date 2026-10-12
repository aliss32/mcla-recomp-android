#!/bin/bash
# Follow the game's log, or save it.
#   logs.sh            follow LibertyRecomp + SDL + crash output
#   logs.sh --dump     write the current buffer to logs/<timestamp>.txt
#   logs.sh --pull     also pull the runtime's own log files from the device
set -euo pipefail
. "$(dirname "$0")/env.sh"
FILTER="LibertyRecomp:V SDL:V SDL/APP:V rex.fs:V DEBUG:V libc:V AndroidRuntime:E vulkan:V Adreno-*:W *:F"
OUT="$APP_DIR/logs/$(date +%Y%m%d-%H%M%S)"

case "${1:-}" in
  --dump)
    mkdir -p "$OUT"
    adbs logcat -d -v threadtime $FILTER > "$OUT/logcat.txt"
    echo "$OUT/logcat.txt"
    ;;
  --pull)
    mkdir -p "$OUT"
    adbs logcat -d -v threadtime $FILTER > "$OUT/logcat.txt"
    adbs pull "$FILES/LibertyRecomp/saves/logs" "$OUT/" 2>/dev/null || true
    adbs shell "ls /data/tombstones" >/dev/null 2>&1 && adbs bugreport "$OUT/bugreport.zip" >/dev/null 2>&1 || true
    echo "$OUT"
    ;;
  *)
    adbs logcat -v threadtime $FILTER
    ;;
esac
