#!/usr/bin/env python3
"""Census .jsonl: swap_ptr vs last_resolve dest comparison.

Usage: python check_swap.py census.jsonl [--mask 0x1FFFFFFF] [--shift 0]

Compares each frame's swap_ptr with the destination address of the last
resolve recorded in that frame. Read the output as:
- Most frames without draws: the base class skipped draws because
  LoadShader returned nullptr; fix that first.
- Every frame mismatched with a constant difference: an address-space
  transform (mask/shift), not a bug; try --mask / --shift.
- Some frames mismatched: broken render frames; inspect their resolve_log
  ordering and dfmt/endian/swap fields.
"""
import argparse
import json
import re
import sys

DEST_RE = re.compile(r"dest 0x([0-9A-Fa-f]+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--mask", type=lambda x: int(x, 0), default=0xFFFFFFFF)
    ap.add_argument("--shift", type=int, default=0)
    args = ap.parse_args()

    total = bad = no_resolve = no_draws = 0

    with open(args.path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                frame = json.loads(line)
            except json.JSONDecodeError as e:
                print(f"broken line: {e}", file=sys.stderr)
                continue
            total += 1
            if frame.get("draws", 0) == 0:
                no_draws += 1
            swap = int(frame["swap_ptr"], 16) & args.mask
            m = DEST_RE.search(frame.get("last_resolve", ""))
            if not m:
                no_resolve += 1
                continue
            dest = (int(m.group(1), 16) << args.shift) & args.mask
            if dest != swap:
                bad += 1
                if bad <= 20:
                    print(
                        f"frame {frame['frame']}: swap_ptr=0x{swap:08X} "
                        f"last resolve dest=0x{dest:08X} "
                        f"(draws={frame.get('draws')}, "
                        f"copies={frame.get('copies')})"
                    )

    print(f"\n{total} frames, {bad} mismatched, {no_resolve} without "
          f"resolve, {no_draws} without draws")
    return 0


if __name__ == "__main__":
    main()
