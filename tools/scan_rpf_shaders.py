#!/usr/bin/env python3
"""Scan RPF-like archives for Xenos shader containers (magic 0x0A102A / '10 2A' header).

MCLA stores its data in xarchive_*.rpf files. This tool scans them for the
2008-format shader containers (10 2A 11 00/01) and the 2005-format ones
(10 2A 0E) and reports counts/offsets, without extracting.

Usage: python scan_rpf_shaders.py <file>...
"""
import os
import struct
import sys

MAGICS = {
    0x00112A10: "2008 container (10 2A 11 00)",
    0x000E2A10: "2005 container (10 2A 0E)",
}


def scan(path):
    size = os.path.getsize(path)
    hits = []
    with open(path, "rb") as f:
        offset = 0
        buf = b""
        CH = 1 << 20
        prev_tail = b""
        while offset < size:
            f.seek(offset)
            chunk = f.read(CH)
            if not chunk:
                break
            data = prev_tail + chunk
            base = offset - len(prev_tail)
            for magic, kind in MAGICS.items():
                needle = struct.pack("<I", magic)
                pos = 0
                while True:
                    i = data.find(needle, pos)
                    if i < 0 or i + 4 > len(data):
                        break
                    hits.append((base + i, kind))
                    pos = i + 4
            prev_tail = chunk[-3:]
            offset += CH
            if len(hits) > 200000:
                break
    return hits


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for p in sys.argv[1:]:
        print(f"scanning {p} ...")
        hits = scan(p)
        print(f"  {len(hits)} hits")
        for off, kind in hits[:20]:
            print(f"    {off:#x}  {kind}")


if __name__ == "__main__":
    main()
