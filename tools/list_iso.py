#!/usr/bin/env python3
"""List the files inside a user-supplied Xbox 360 disc image (XDVDFS), no extraction."""
import os
import struct
import sys

SECTOR = 0x800
MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITION_OFFSETS = (0x0, 0xFD90000, 0x2080000, 0x18300000, 0x89D80000)
SKIP_DIRS = ("/$SystemUpdate",)


def find_partition(f, size):
    for base in PARTITION_OFFSETS:
        if base + 32 * SECTOR + 28 > size:
            continue
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            root_sector, root_size = struct.unpack("<II", f.read(8))
            return base, root_sector, root_size
    raise SystemExit("error: no XDVDFS volume found")


def walk(f, base, sector, dir_size, path, out):
    f.seek(base + sector * SECTOR)
    data = f.read(dir_size)
    off = 0
    while off + 14 <= len(data):
        left, right, sec, size, attr, name_len = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF:
            break
        name = data[off + 14:off + 14 + name_len].decode("latin-1")
        full = f"{path}/{name}"
        if attr & 0x10:
            if size:
                walk(f, base, sec, size, full, out)
        else:
            out.append((full, size))
        off = right * 4 if right else off + 14 + name_len + (4 - (14 + name_len) % 4)
        if right == 0 or off <= 0:
            off += 14 + name_len


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    iso = sys.argv[1]
    size = os.path.getsize(iso)
    out = []
    with open(iso, "rb") as f:
        base, root_sector, root_size = find_partition(f, size)
        walk(f, base, root_sector, root_size, "", out)
    seen = set()
    total = 0
    for path, fsize in sorted(set(out)):
        if path in seen:
            continue
        seen.add(path)
        total += fsize
        print(f"{fsize:>12} {path}")
    print(f"{len(seen)} files, {total / 2**30:.2f} GiB")


if __name__ == "__main__":
    main()
