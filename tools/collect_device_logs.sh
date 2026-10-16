#!/usr/bin/env bash
# Collects MCLA Android device logs for the rexgpu-mcla-native pipeline
# debugging (UTF-8 clean; POSIX tools only). Counterpart of
# tools/collect_device_logs.ps1 for CI / Linux shells.
#
# Usage: ./tools/collect_device_logs.sh [output-dir]
set -u

ADB=${ADB:-adb}
PKG=files
REMOTE="/sdcard/Android/data/com.mcla.recomp/files"
TS=$(date +%Y%m%d_%H%M%S)
OUT=${1:-mcla_logs_$TS}
mkdir -p "$OUT"

echo "== pids before"
$ADB shell pidof com.mcla.recomp com.mcla.recomp:game 2>/dev/null | tee "$OUT/pid_before.txt"

echo "== pull files/ tree"
$ADB pull "$REMOTE" "$OUT/files" 2>&1 | tee "$OUT/pull_log.txt"

echo "== logcat main (threadtime)"
$ADB exec-out logcat -d -v threadtime > "$OUT/logcat_main.txt" 2>/dev/null

echo "== logcat crash buffer"
$ADB exec-out logcat -b crash -d > "$OUT/logcat_crash.txt" 2>/dev/null

echo "== tombstones / ANR (best effort, needs root)"
$ADB shell "su -c 'ls -la /data/tombstones/ | tail -5'" > "$OUT/tombstones_ls.txt" 2>/dev/null || true
$ADB shell "su -c 'ls -la /data/anr/ 2>/dev/null | tail -5'" > "$OUT/anr_ls.txt" 2>/dev/null || true

echo "== dropbox"
$ADB shell dumpsys dropbox --print 2>/dev/null > "$OUT/dropbox.txt" || true

echo "== pids after"
$ADB shell pidof com.mcla.recomp com.mcla.recomp:game 2>/dev/null | tee "$OUT/pid_after.txt"

echo "== census summary"
CENSUS=$(find "$OUT/files" -name mcla_frame_census.jsonl 2>/dev/null | head -1)
if [ -n "$CENSUS" ]; then
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$CENSUS" > "$OUT/census_summary.txt" <<'PY'
import json, sys, collections
path = sys.argv[1]
frames = 0
keys = collections.Counter()
swap_ptrs = collections.Counter()
last_resolve = collections.Counter()
skips = collections.Counter()
bytes_seen = 0
with open(path, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        bytes_seen += len(line)
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except Exception:
            keys["json_error"] += 1
            continue
        frames += 1
        for k in ("draws", "copies", "swaps", "clears"):
            if isinstance(obj.get(k), int):
                keys[k] += obj[k]
        for reason, count in (obj.get("skipped") or {}).items():
            skips[reason] += count
        sp = obj.get("swap_ptr") or obj.get("last_swap_ptr")
        if sp:
            swap_ptrs[sp] += 1
        lr = obj.get("last_resolve")
        if lr:
            last_resolve[lr] += 1
print(f"frames={frames} bytes={bytes_seen}")
print("counters:", dict(keys))
print("swap_ptrs:", dict(swap_ptrs))
print("last_resolve==swap frames:",
      sum(c for p, c in last_resolve.items() if p in swap_ptrs))
if skips:
    print("skipped reasons:")
    for reason, count in skips.most_common():
        print(f"  {reason}: {count}")
PY
    cat "$OUT/census_summary.txt"
  else
    echo "python3 not found; census summary skipped"
  fi
else
  echo "census jsonl not found in pulled tree"
fi

echo "Done: $OUT"
