#!/usr/bin/env python3
"""TTY's files on the A: drive of the release firmware (M11, loader/flash/README.md §5).

`rom_files(root)` -> {A: path: bytes}, in directory order (stage 1 first, so the
stub's lookup scans the fewest directory sectors):
    A:/cfw/gr2_cfw.bin         bin/<variant>/gr2_cfw.bin (stage 1, loader ABI v1, <= 0x8000 B:
                               the stub's LoadFile truncates silently; its length (+12)
                               and CRC-32 (+16) must match, scripts/stage1_crc.py)
    A:/cfw/gr2_main.bin        bin/<variant>/tty_main.bin (stage 2)
    A:/cfw/fonts/<name>.fnt    every fonts/*.fnt (GFNT)
The A: names are internal and unchanged since v1.0.x (the rom stub opens A:/cfw/gr2_cfw.bin), so
updates from v1.0.x keep the NAND layout.
No recipe files: the film recipes ship on the card (tty/recipes/ in the official zip,
scripts/package_sd.py). TTY still lists A:/cfw/recipes/ first (src/recipes.c), and an
absent A: recipe folder is normal. The variant is `rom` (CFW_ROM=1); scripts/build_cfw_frm.py
writes these files into RES and scripts/emu_release.py / tests/emu read them back.

A: rules (finding 29 §2): full path <= 31 characters, case-sensitive, unique, no empty
file (size 0 ends the directory), upper-case drive letter.

Run directly to list the set: python3 scripts/rom_files.py [--variant rom]
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from package_sd import LDR_MAGIC, MAIN_MAGIC, WINDOW_BASE, WINDOW_SIZE  # noqa: E402
from stage1_crc import check as stage1_check  # noqa: E402

STAGE1 = "A:/cfw/gr2_cfw.bin"
STAGE2 = "A:/cfw/gr2_main.bin"
FONT_DIR = "A:/cfw/fonts/"
NAME_MAX = 31


class RomError(Exception):
    pass


def rom_files(root: Path = ROOT, variant: str = "rom") -> dict[str, bytes]:
    root = Path(root)
    bindir = root / "bin" / variant
    try:
        boot = (bindir / "gr2_cfw.bin").read_bytes()
        main = (bindir / "tty_main.bin").read_bytes()
    except FileNotFoundError as ex:
        raise RomError(f"{ex.filename} missing: run make") from None
    if len(boot) > WINDOW_SIZE:
        raise RomError(f"stage 1 is {len(boot)} B, larger than the 0x8000 window LoadFile fills")
    _, magic, hook = struct.unpack_from(">HxxII", boot)
    if magic != LDR_MAGIC or not hook & 1 or not WINDOW_BASE <= hook & ~1 < WINDOW_BASE + len(boot):
        raise RomError("stage 1 lacks the loader ABI v1 header ('GRL1' at +4, Thumb hook at +8)")
    bad = stage1_check(boot)
    if bad:
        raise RomError(f"stage 1 self-check fields (+12 length, +16 CRC-32): {bad}: rebuild")
    magic, image_size, _bss, _entry, nrelocs = struct.unpack_from(">5I", main)
    if magic != MAIN_MAGIC or len(main) != 32 + image_size + 4 * nrelocs:
        raise RomError("stage 2 has a bad header or size")
    files = {STAGE1: boot, STAGE2: main}
    for path in sorted((root / "fonts").glob("*.fnt")):
        data = path.read_bytes()
        if data[:4] != b"GFNT" or len(data) < 12 or int.from_bytes(data[8:12], "big") != len(data):
            raise RomError(f"fonts/{path.name} is not a GFNT file")
        files[FONT_DIR + path.name] = data
    for name, data in files.items():
        if len(name) > NAME_MAX or not name.isascii() or not name.startswith("A:/cfw/") or "\\" in name:
            raise RomError(f"{name}: A: names are ASCII full paths of at most {NAME_MAX} characters")
        if not data:
            raise RomError(f"{name}: empty files cannot be stored on A: (size 0 ends the directory)")
    return files


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--variant", default="rom")
    a = ap.parse_args(argv)
    try:
        files = rom_files(ROOT, a.variant)
    except RomError as ex:
        print(f"rom_files: {ex}")
        return 1
    for name, data in files.items():
        print(f"{len(data):>8,d}  {name}")
    print(f"{sum(map(len, files.values())):>8,d}  {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
