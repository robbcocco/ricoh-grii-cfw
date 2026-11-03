#!/usr/bin/env python3
"""Build tty_main.bin (stage 2, include/boot.h) from two links of the same objects.

    mkmain.py <main.elf linked at 0> <main_hi.elf linked at DELTA> <DELTA> <out.bin>

Every aligned 32-bit word that differs between the two images by exactly DELTA
is an absolute address into the image (literal pool entry, pointer table):
its offset goes into the relocation table and stage 1 adds the load address.
Any other difference (unaligned, or by another amount) is an error, so code that
is not position-independent fails the build instead of the camera.
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

MAGIC = 0x47524D31   # "GRM1"
HDR = ">8I"


def fail(msg: str) -> None:
    sys.exit(f"mkmain: {msg}")


def raw(elf: str) -> bytes:
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "img.bin"
        subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", elf, str(out)], check=True)
        return out.read_bytes()


def symbols(elf: str) -> dict:
    """Symbol values from the ELF symbol table (Thumb functions keep bit 0)."""
    out = subprocess.run(["arm-none-eabi-readelf", "-sW", elf], capture_output=True, text=True,
                         check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[0].endswith(":") and parts[0] != "Num:":
            syms[parts[7]] = int(parts[1], 16)
    return syms


def main() -> None:
    if len(sys.argv) != 5:
        fail(__doc__.strip().splitlines()[2].strip())
    elf0, elf1, delta, out = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), sys.argv[4]
    img0, img1 = raw(elf0), raw(elf1)
    syms = symbols(elf0)
    if syms.get("__main_start__") != 0:
        fail(f"{elf0} is not linked at 0")
    image_size = syms["__image_end__"]
    bss_size = syms["__bss_end__"] - syms["__bss_start__"]
    entry, hook = syms["cfw_main"], syms["cfw_hook"]
    if len(img0) != len(img1) or len(img0) > image_size or image_size % 4 or bss_size % 4:
        fail(f"unexpected image sizes ({len(img0)}, {len(img1)}, {image_size}, bss {bss_size})")
    if syms["__bss_start__"] != image_size:
        fail(".bss does not follow the image")
    if not entry & 1 or not hook & 1:
        fail("cfw_main / cfw_hook is not a Thumb function")
    img0 = img0.ljust(image_size, b"\0")
    img1 = img1.ljust(image_size, b"\0")
    relocs = []
    for off in range(0, image_size, 4):
        a, b = struct.unpack_from(">I", img0, off)[0], struct.unpack_from(">I", img1, off)[0]
        if a == b:
            continue
        if (b - a) & 0xFFFFFFFF != delta or a >= image_size + bss_size:
            fail(f"word at 0x{off:X} differs by 0x{(b - a) & 0xFFFFFFFF:X} (value 0x{a:X}): "
                 "not a plain absolute address into the image")
        relocs.append(off)
    hdr = struct.pack(HDR, MAGIC, image_size, bss_size, entry, len(relocs), hook, 0, 0)
    Path(out).write_bytes(hdr + img0 + b"".join(struct.pack(">I", r) for r in relocs))
    print(f"mkmain: {out}: image {image_size} B, bss {bss_size} B, {len(relocs)} relocations")


if __name__ == "__main__":
    main()
