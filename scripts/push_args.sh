#!/bin/bash
# Push a cvar profile read by LibertyActivity on the next launch.
#   push_args.sh <file>     one --cvar=value per line, '#' comments allowed
#   push_args.sh --clear    remove it
set -euo pipefail
. "$(dirname "$0")/env.sh"
case "${1:-}" in
  "") echo "usage: push_args.sh <file> | --clear"; exit 1 ;;
  --clear) adbs shell rm -f "$FILES/args.txt"; echo "args cleared" ;;
  *)
    adbs shell mkdir -p "$FILES"
    adbs push "$(winpath "$1")" "$FILES/args.txt" >/dev/null
    echo "pushed $1:"; grep -v '^\s*#' "$1" | sed '/^\s*$/d; s/^/  /'
    ;;
esac
