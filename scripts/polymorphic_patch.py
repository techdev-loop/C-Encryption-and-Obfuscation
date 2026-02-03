#!/usr/bin/env python3
"""
Optional polymorphic patch: change EXE hash/identity per user/download.
Run after build: reads the built EXE, patches a small block of bytes at a fixed
offset (or in a padding region), writes a new file. Use different output
filename per user (e.g. cat_clicker_<user_id>.exe) so hash and name vary.

Usage:
  python polymorphic_patch.py path/to/cat_clicker.exe [output.exe] [--offset N]

If output is omitted, writes to cat_clicker_patched.exe in the same dir.
Offset defaults to a safe position (e.g. end of file minus 64); override with --offset.
"""

import argparse
import os
import secrets
import sys


def main():
    ap = argparse.ArgumentParser(description="Patch EXE with random bytes for polymorphic download.")
    ap.add_argument("exe", help="Input EXE path")
    ap.add_argument("output", nargs="?", help="Output EXE path (default: <exe_dir>/cat_clicker_patched.exe)")
    ap.add_argument("--offset", type=int, default=None, help="Byte offset to patch (default: 64 bytes before EOF)")
    args = ap.parse_args()

    if not os.path.isfile(args.exe):
        print(f"Error: not a file: {args.exe}", file=sys.stderr)
        return 1

    with open(args.exe, "rb") as f:
        data = bytearray(f.read())

    n = len(data)
    patch_len = 32
    if args.offset is not None:
        offset = args.offset
        if offset < 0 or offset + patch_len > n:
            print("Error: offset out of range", file=sys.stderr)
            return 1
    else:
        # Find a 32-byte run of zeros in the first 256KB (common .rdata padding)
        search_limit = min(256 * 1024, n - patch_len)
        offset = None
        for i in range(0, search_limit):
            if data[i : i + patch_len] == b"\x00" * patch_len:
                offset = i
                break
        if offset is None:
            print("Error: no 32-byte zero block found; use --offset N", file=sys.stderr)
            return 1
    patch = secrets.token_bytes(patch_len)
    data[offset : offset + patch_len] = patch

    out_path = args.output
    if not out_path:
        out_path = os.path.join(os.path.dirname(os.path.abspath(args.exe)), "cat_clicker_patched.exe")

    with open(out_path, "wb") as f:
        f.write(data)

    print(f"Wrote {out_path} (patched {patch_len} bytes at offset {offset})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
