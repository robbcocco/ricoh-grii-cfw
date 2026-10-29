#!/usr/bin/env python3
"""Ricoh GR II Boot Screen Image Converter (Pure Python stdlib, zero dependencies).

Converts between:
- Ricoh GR II BRP: 640x480, 16-bit RGB565 Big-Endian (614,400 bytes raw)
- Standard 24-bit Windows BMP (readable natively by macOS Finder, Preview, Photoshop)
"""

import sys
import struct
import argparse

W = 640
H = 480
BPP = 2
BRP_SIZE = W * H * BPP # 614,400 bytes
def be565_to_rgb888(val):
    r5 = (val >> 11) & 0x1F
    g6 = (val >> 5) & 0x3F
    b5 = val & 0x1F
    # Standard bit replication for exact round-trip
    r = (r5 << 3) | (r5 >> 2)
    g = (g6 << 2) | (g6 >> 4)
    b = (b5 << 3) | (b5 >> 2)
    return r, g, b

def rgb888_to_be565(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5
def brp_to_bmp(brp_path, bmp_path):
    with open(brp_path, "rb") as f:
        data = f.read()
    if len(data) != BRP_SIZE:
        raise ValueError(f"Invalid BRP size {len(data)} (expected {BRP_SIZE})")

    # BMP rows are stored bottom-to-top, padded to 4-byte multiple
    row_bytes = W * 3
    padding = (4 - (row_bytes % 4)) % 4
    image_size = (row_bytes + padding) * H
    file_size = 54 + image_size

    # 54-byte BMP header
    header = bytearray(54)
    struct.pack_into("<2sIHHI", header, 0, b"BM", file_size, 0, 0, 54)
    struct.pack_into("<IIIHHIIIIII", header, 14, 40, W, H, 1, 24, 0, image_size, 2835, 2835, 0, 0)

    # Decode pixels
    bmp_body = bytearray()
    for y in reversed(range(H)): # Bottom-to-top
        row = bytearray()
        for x in range(W):
            off = (y * W + x) * 2
            val = struct.unpack_from(">H", data, off)[0]
            r, g, b = be565_to_rgb888(val)
            row.extend((b, g, r)) # BMP is BGR
        row.extend(b"\0" * padding)
        bmp_body.extend(row)

    with open(bmp_path, "wb") as f_out:
        f_out.write(header + bmp_body)
    print(f"[OK] Converted {brp_path} -> {bmp_path} ({len(header) + len(bmp_body):,} bytes)")

def bmp_to_brp(bmp_path, brp_path):
    with open(bmp_path, "rb") as f:
        data = f.read()

    if data[:2] != b"BM":
        raise ValueError("Not a valid BMP file")

    offset = struct.unpack_from("<I", data, 10)[0]
    w, h, planes, bpp = struct.unpack_from("<IIHH", data, 18)

    if w != W or h != H or bpp != 24:
        raise ValueError(f"BMP must be exactly {W}x{H} 24-bit (got {w}x{h} {bpp}-bit)")

    row_bytes = W * 3
    padding = (4 - (row_bytes % 4)) % 4

    brp_out = bytearray(BRP_SIZE)
    # Read rows bottom-to-top
    for y in range(H):
        bmp_y = H - 1 - y
        row_start = offset + bmp_y * (row_bytes + padding)
        for x in range(W):
            px_off = row_start + x * 3
            b, g, r = data[px_off], data[px_off+1], data[px_off+2]
            val = rgb888_to_be565(r, g, b)
            struct.pack_into(">H", brp_out, (y * W + x) * 2, val)

    with open(brp_path, "wb") as f_out:
        f_out.write(brp_out)
    print(f"[OK] Converted {bmp_path} -> {brp_path} ({len(brp_out):,} bytes, 640x480 RGB565 BE)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert between Ricoh GR II BRP and standard BMP")
    sub = parser.add_subparsers(dest="cmd")
    p1 = sub.add_parser("decode", help="Convert BRP -> BMP")
    p1.add_argument("brp", help="Input .brp file")
    p1.add_argument("bmp", help="Output .bmp file")

    p2 = sub.add_parser("encode", help="Convert BMP -> BRP")
    p2.add_argument("bmp", help="Input .bmp file")
    p2.add_argument("brp", help="Output .brp file")

    args = parser.parse_args()
    if args.cmd == "decode":
        brp_to_bmp(args.brp, args.bmp)
    elif args.cmd == "encode":
        bmp_to_brp(args.bmp, args.brp)
    else:
        parser.print_help()
