#!/usr/bin/env python3
"""Probe an RPF7 archive header and directory entries (MCLA xarchive_*.rpf).

RPF7 layout (header at 0):
  dword0: magic 0x52504637 ('RPF7') or encrypted 0x0FEFFFFF / 0xF70EFFFF
  dword1: entry count
  dword2: directory size in dwords
  dword3: 0 if unencrypted, else key seed
  dword4..11: encryption AES key (or 0)
Entries (16 bytes each, after directories):
  name offset*0x800 (bits 0-17) | 0 (bits 18-31) for directories
  file size, file offset, etc.
"""
import os
import struct
import sys


def read_header(path):
    with open(path, "rb") as f:
        hdr = f.read(0x30)
    dwords = struct.unpack("<12I", hdr)
    print(f"header dwords: {[hex(d) for d in dwords]}")
    return dwords


def main():
    for p in sys.argv[1:]:
        print(f"== {p} ==")
        read_header(p)
        with open(p, "rb") as f:
            f.seek(0x30)
            blob = f.read(0x100)
            print("  first 0x100 after header:", blob[:64].hex())


if __name__ == "__main__":
    main()
