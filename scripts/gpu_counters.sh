#!/bin/bash
# Adreno 6xx block utilisation while the game runs (needs tools/enable_gpu_counters.sh
# run as root once per boot). Busy counters are printed as a share of GPU cycles.
#   gpu_counters.sh [seconds]
set -euo pipefail
. "$(dirname "$0")/env.sh"
SECS="${1:-5}"
TOOL=/data/local/tmp/kgslperf
adbs push "$(winpath "$APP_DIR/tools/kgslperf")" "$TOOL" >/dev/null
adbs shell chmod 755 "$TOOL"
# group:countable (a6xx). KGSL groups: CP 0, RBBM 1, PC 2, VFD 3, HLSQ 4, VPC 5,
# TSE 6, RAS 7, UCHE 8, TP 9, SP 10, RB 11, LRZ 25.
run() { adbs shell "$TOOL $SECS $*" | tr -d '\r'; }
OUT="$(run 1:0=GPU_CYCLES 0:2=CP_BUSY 0:1=CP_BUSY_GFX_IDLE 2:0=PC_BUSY 3:0=VFD_BUSY \
  4:0=HLSQ_BUSY 5:0=VPC_BUSY 6:0=TSE_BUSY 7:0=RAS_BUSY 8:0=UCHE_BUSY 9:0=TP_BUSY \
  10:0=SP_BUSY 10:1=SP_ALU_WORKING 10:2=SP_EFU_WORKING 10:4=SP_STALL_TP 10:5=SP_STALL_UCHE \
  10:12=SP_VS_WAVE_CYCLES 10:10=SP_FS_WAVE_CYCLES 11:0=RB_BUSY 25:0=LRZ_BUSY)"
echo "$OUT" | awk '
  NR == 1 { base = $2; printf "%-22s %8.1f M/s\n", $1, $2 / 1e6; next }
  { printf "%-22s %6.1f%%\n", $1, base ? 100 * $2 / base : 0 }'
