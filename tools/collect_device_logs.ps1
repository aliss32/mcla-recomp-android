# Collects MCLA Android device logs for the rexgpu-mcla-native pipeline
# debugging. UTF-8 only: adb exec-out / cmd /c redirection (never PowerShell
# '>' redirection, which writes UTF-16 and corrupts binary pulls).
#
# Usage:  .\tools\collect_device_logs.ps1 [output-dir]
# Output: <output-dir>\mcla_logs_<timestamp>\ with:
#   - pulled files/ tree (census jsonl, runtime logs, launch report, debug_logs)
#   - logcat -d -v threadtime (full buffer)
#   - logcat -b crash -d
#   - pidof com.mcla.recomp:game before/after
#   - tombstones / ANR traces if readable (requires adb root; best effort)
#   - census summary: json.loads every line of mcla_frame_census.jsonl and
#     prints per-frame counters + failure keys (python3 if available)

param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Continue"
$ADB = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $ADB)) {
    $ADB = "adb"  # fall back to PATH
}

if ([string]::IsNullOrEmpty($OutputDir)) {
    $OutputDir = Join-Path $PWD ("mcla_logs_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Collect($name, $scriptblock) {
    Write-Host "== $name"
    try {
        & $scriptblock
    }
    catch {
        Write-Host "   (failed: $_)"
    }
}

# UTF-8 safe text capture: adb exec-out piped through cmd /c redirection.
# PowerShell redirection re-encodes to UTF-16; this avoids it.
function ExecOutToUtf8File($adbArgs, $outPath) {
    $cmd = "/c `"`"$ADB`" $adbArgs > `"$outPath`"`""
    Start-Process -FilePath "cmd.exe" -ArgumentList $cmd -Wait -NoNewWindow
}

Write-Host "Collecting to: $OutputDir"

# 1. Process alive? (before)
$pidsBefore = (& $ADB shell "pidof com.mcla.recomp com.mcla.recomp:game" 2>$null) | Out-String
$pidsBefore.Trim() | Out-File (Join-Path $OutputDir "pid_before.txt") -Encoding utf8
Write-Host "pids: $pidsBefore"

# 2. Pull the whole files/ tree (census, runtime logs, debug_logs, launch report)
$pkg = "/sdcard/Android/data/com.mcla.recomp/files"
Collect "adb pull files/ tree" {
    & $ADB pull $pkg (Join-Path $OutputDir "files") 2>&1 |
    Out-File (Join-Path $OutputDir "pull_log.txt") -Encoding utf8
}

# 3. Full logcat (main buffer), threadtime for timestamps+tid
ExecOutToUtf8File "logcat -d -v threadtime" (Join-Path $OutputDir "logcat_main.txt")

# 4. Crash buffer
ExecOutToUtf8File "logcat -b crash -d" (Join-Path $OutputDir "logcat_crash.txt")

# 5. Tombstones and ANR traces (requires root on userdebug; best effort)
Collect "tombstones" {
    & $ADB shell "su -c 'ls -la /data/tombstones/ | tail -5'" 2>$null |
    Out-File (Join-Path $OutputDir "tombstones_ls.txt") -Encoding utf8
    & $ADB shell "su -c 'cat /data/tombstones/tombstone_00'" 2>$null |
    Out-File (Join-Path $OutputDir "tombstone_00.txt") -Encoding utf8
}
Collect "ANR traces" {
    & $ADB shell "su -c 'ls -la /data/anr/ 2>/dev/null | tail -5'" 2>$null |
    Out-File (Join-Path $OutputDir "anr_ls.txt") -Encoding utf8
}

# 6. Dropbox entries (ANR/crash summaries)
Collect "dropbox" {
    & $ADB shell "dumpsys dropbox --print 2>/dev/null" |
    Out-File (Join-Path $OutputDir "dropbox.txt") -Encoding utf8
}

# 7. pids after (did the game die during collection?)
$pidsAfter = (& $ADB shell "pidof com.mcla.recomp com.mcla.recomp:game" 2>$null) | Out-String
$pidsAfter.Trim() | Out-File (Join-Path $OutputDir "pid_after.txt") -Encoding utf8

# 8. Census summary (python3, best effort)
$census = Get-ChildItem -Path $OutputDir -Recurse -Filter "mcla_frame_census.jsonl" -ErrorAction SilentlyContinue |
Select-Object -First 1 -ExpandProperty FullName
if ($census) {
    $py = (Get-Command python3 -ErrorAction SilentlyContinue) ? "python3" :
    ((Get-Command python -ErrorAction SilentlyContinue) ? "python" : $null)
    if ($py) {
        $summary = Join-Path $OutputDir "census_summary.txt"
        $code = @'
import json, sys, collections
path = sys.argv[1]
frames = 0
keys = collections.Counter()
swap_ptrs = collections.Counter()
last_resolve = collections.Counter()
bytes_seen = 0
with open(path, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        bytes_seen += len(line)
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except Exception as e:
            keys["json_error"] += 1
            continue
        frames += 1
        for k in ("draws", "copies", "swaps", "clears", "skipped"):
            if k in obj:
                keys[k] += obj[k] if isinstance(obj[k], int) else len(obj[k])
        sp = obj.get("swap_ptr") or obj.get("last_swap_ptr")
        if sp: swap_ptrs[sp] += 1
        lr = obj.get("last_resolve")
        if lr: last_resolve[lr] += 1
print(f"frames={frames} bytes={bytes_seen}")
print("counters:", dict(keys))
print("swap_ptrs:", dict(swap_ptrs))
print("last_resolve==swap frames:",
      sum(c for ptr, c in last_resolve.items() if ptr in swap_ptrs))
skips = collections.Counter()
with open(path, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        try: obj = json.loads(line)
        except Exception: continue
        for reason, count in (obj.get("skipped") or {}).items():
            skips[reason] += count
if skips:
    print("skipped reasons:")
    for reason, count in skips.most_common():
        print(f"  {reason}: {count}")
'@
        $codeFile = Join-Path $env:TEMP "mcla_census_summary.py"
        $code | Out-File $codeFile -Encoding utf8
        & $py $codeFile $census | Out-File $summary -Encoding utf8
        Get-Content $summary
    }
    else {
        Write-Host "python not found; census summary skipped"
    }
}
else {
    Write-Host "census jsonl not found in pulled tree"
}

Write-Host ""
Write-Host "Done. Bundle:"
Write-Host "  Compress-Archive -Path $OutputDir -DestinationPath $OutputDir.zip"
