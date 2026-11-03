#!/usr/bin/env python3
"""Rasterise the overlay fonts into TTY's GFNT format (fonts/*.fnt).

Fonts (SIL Open Font License 1.1, licence texts in fonts/licenses/):
  Inter 4.1            https://github.com/rsms/inter/releases (extras/ttf)
  Barlow Condensed     https://github.com/google/fonts/tree/main/ofl/barlowcondensed

Usage: python3 scripts/make_fonts.py --inter DIR --barlow DIR   (needs Pillow)

GFNT (big-endian):
  0  'GFNT'
  4  u8 version (1), u8 line height, u8 ascent (baseline from line top), u8 glyph count
  8  u32 total file size
  12 glyph table, 8 bytes per glyph: u16 bitmap offset (from the bitmap area),
     u8 w, u8 h, s8 x offset, s8 y offset (from line top), u8 advance, u8 0
  .. bitmap area: 4-bit coverage, rows of ceil(w/2) bytes, high nibble first
Glyphs: ASCII 0x20..0x7E, then codes 0x80.. for EXTRA (see include/osd.h).
"""
import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
EXTRA = "\u2039\u203a\u25b2\u25bc\u25c0\u25b6\u00b7"   # ‹ › ▲ ▼ ◀ ▶ ·  -> 0x80..0x86
CHARS = [chr(c) for c in range(0x20, 0x7F)] + list(EXTRA)

# (file name, family, style file, pixel size)
FONTS = [
    ("inm13.fnt",  "inter",  "Inter-Medium.ttf",              13),
    ("insb15.fnt", "inter",  "Inter-SemiBold.ttf",            15),
    ("inm18.fnt",  "inter",  "Inter-Medium.ttf",              18),
    ("insb18.fnt", "inter",  "Inter-SemiBold.ttf",            18),
    ("bcsb22.fnt", "barlow", "BarlowCondensed-SemiBold.ttf",  22),
]


def rasterise(font: ImageFont.FreeTypeFont) -> bytes:
    ascent, descent = font.getmetrics()
    line_h = ascent + descent
    table, bitmaps = [], bytearray()
    for ch in CHARS:
        adv = int(round(font.getlength(ch)))
        pad = 8
        img = Image.new("L", (adv + 2 * pad, line_h + 2 * pad), 0)
        ImageDraw.Draw(img).text((pad, pad + ascent), ch, font=font, fill=255, anchor="ls")
        box = img.getbbox()
        if box is None:
            table.append((len(bitmaps), 0, 0, 0, 0, adv))
            continue
        x0, y0, x1, y1 = box
        w, h = x1 - x0, y1 - y0
        if w > 255 or h > 255:
            raise SystemExit(f"glyph {ch!r} too large")
        px = img.crop(box).load()
        offset = len(bitmaps)
        for y in range(h):
            row = bytearray((w + 1) // 2)
            for x in range(w):
                v = (px[x, y] * 15 + 127) // 255
                row[x // 2] |= v << (4 if x % 2 == 0 else 0)
            bitmaps += row
        table.append((offset, w, h, x0 - pad, y0 - pad, adv))
    if len(bitmaps) > 0xFFFF:
        raise SystemExit("bitmap area exceeds 64 KiB")
    head_size = 12 + 8 * len(table)
    out = bytearray(b"GFNT")
    out += struct.pack(">BBBBI", 1, line_h, ascent, len(table), head_size + len(bitmaps))
    for off, w, h, xo, yo, adv in table:
        out += struct.pack(">HBBbbBB", off, w, h, xo, yo, adv, 0)
    out += bitmaps
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--inter", required=True, type=Path, help="directory with Inter-*.ttf")
    ap.add_argument("--barlow", required=True, type=Path, help="directory with BarlowCondensed-*.ttf")
    args = ap.parse_args()
    dirs = {"inter": args.inter, "barlow": args.barlow}
    out_dir = ROOT / "fonts"
    out_dir.mkdir(exist_ok=True)
    for name, family, style, size in FONTS:
        data = rasterise(ImageFont.truetype(str(dirs[family] / style), size))
        (out_dir / name).write_bytes(data)
        print(f"{name}: {len(data)} B, line {data[5]} px")


if __name__ == "__main__":
    main()
