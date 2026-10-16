#!/usr/bin/env python3
"""Parse MCLA xarchive_*.rpf (RPF3, unencrypted) and list entries.

RPF3 header (16 bytes, little-endian):
  [0] magic 'RPF3' (0x33465052)
  [1] entry count (total entries, files+dirs)
  [2] directory block size in dwords? (observed entryCount-5)
  [3] 0xFFFFFFFF sentinel
Entries: 16 bytes each starting at 0x10:
  dirs:  nameOffset*0x800 in dword0 bits 0-17 | (dirSize? <<18)? then dword1 = child entry index
  files: dword1 = file size, dword2 = file offset * 0x800, dword3 = 0 (uncompressed)
Names block follows the entry table, null-terminated strings at nameOffset*0x800.
"""
import os
import struct
import sys


def parse(path, limit=0):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        hdr = f.read(16)
        magic, count, dword2, sentinel = struct.unpack("<4I", hdr)
        print(f"magic={magic:#x} count={count} dword2={dword2} sentinel={sentinel:#x} size={size:#x}")
        entries = []
        for i in range(count):
            raw = f.read(16)
            if len(raw) < 16:
                break
            entries.append(struct.unpack("<4I", raw))
        # names region: after entries, page-aligned
        names_base = f.tell()
        f.seek(0, 2)
        names_size = f.tell() - names_base
        names = f.read(names_size) if names_size < (1 << 26) else b""

        def name_at(off):
            off = off & 0xFFFFFF
            if off >= len(names):
                return f"<names?{off:#x}>"
            end = names.find(b"\0", off)
            return names[off:end].decode("latin-1", "replace")

        n = 0
        for i, e in enumerate(entries):
            if limit and n >= limit:
                break
            d0, d1, d2, d3 = e
            # heuristic: dir entries have dword1 as child index chain (huge) and d2/d3 == 0
            is_dir = d1 == 0xFFFFFFFF or (d2 == 0 and d3 == 0 and d1 >= count)
            if is_dir:
                nm = name_at(d0)
                print(f"[{i:6}] DIR   {nm}")
            else:
                nm = name_at(d0)
                print(f"[{i:6}] FILE  {nm}  size={d1:#x} off={d2 * 0x800:#x}")
                n += 1


if __name__ == "__main__":
    for p in sys.argv[1:]:
        parse(p, limit=int(os.environ.get("LIMIT", "60")))
