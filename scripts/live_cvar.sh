#!/bin/bash
# Change cvars in the running game (re-read once a second).
#   live_cvar.sh --gta4_draw_distance_scale=0.5 [--other=value ...]
# Only settings the title reads every frame take effect without a relaunch.
set -euo pipefail
. "$(dirname "$0")/env.sh"
[ $# -gt 0 ] || { echo "usage: live_cvar.sh --name=value [...]"; exit 1; }
printf '%s\n' "$@" | adbs shell "cat > $FILES/live_cvars.txt"
adbs shell sleep 2
adbs logcat -d -s LibertyRecomp:W | grep live-cvar | tail -n $#
