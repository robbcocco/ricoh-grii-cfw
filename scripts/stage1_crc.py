#!/usr/bin/env python3
"""Write stage 1's self-check fields (include/boot.h) into a linked gr2_cfw.bin.

    stage1_crc.py <gr2_cfw.bin>

+12 = the image length (the file size), +16 = CRC-32 (IEEE, zlib.crc32) over bytes [0, 16) and
[20, length). Stage 1 recomputes both over the window at its first loader hook (src/boot.c) and
disarms on a mismatch: the stubs' LoadFile truncates a file silently at 0x8000 bytes. The Makefile
runs this right after objcopy for every variant; package_sd.py, the emulator and the release
firmware builder check the result with `check()`.
"""
import struct
import sys
import zlib
from pathlib import Path

LEN_OFF, CRC_OFF = 12, 16
WINDOW_SIZE = 0x8000
LDR_MAGIC = 0x47524C31   # "GRL1" at +4 (loader ABI v1)


def crc(data: bytes) -> int:
    return zlib.crc32(data[:CRC_OFF] + data[CRC_OFF + 4:])


def check(data: bytes) -> str | None:
    """None if the length and CRC fields match the bytes, else what is wrong."""
    if len(data) < CRC_OFF + 4 or struct.unpack_from(">I", data, 4)[0] != LDR_MAGIC:
        return "no loader ABI v1 header"
    if len(data) > WINDOW_SIZE:
        return f"{len(data)} B, larger than the 0x8000 window"
    length, stored = struct.unpack_from(">II", data, LEN_OFF)
    if length != len(data):
        return f"length field {length} != file size {len(data)}"
    if stored != crc(data):
        return f"CRC field {stored:#010x} != {crc(data):#010x}"
    return None


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = Path(sys.argv[1])
    data = bytearray(path.read_bytes())
    if len(data) < CRC_OFF + 4 or struct.unpack_from(">I", data, 4)[0] != LDR_MAGIC:
        sys.exit(f"stage1_crc: {path}: no loader ABI v1 header")
    if len(data) > WINDOW_SIZE:
        sys.exit(f"stage1_crc: {path}: {len(data)} B, larger than the 0x8000 window")
    if struct.unpack_from(">II", data, LEN_OFF) not in ((0, 0), (len(data), crc(bytes(data)))):
        sys.exit(f"stage1_crc: {path}: +12/+16 hold neither zeros (fresh objcopy) nor valid fields")
    struct.pack_into(">I", data, LEN_OFF, len(data))
    struct.pack_into(">I", data, CRC_OFF, crc(bytes(data)))
    path.write_bytes(bytes(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
