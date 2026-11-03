#!/usr/bin/env python3
"""Package the official and the developer zip (`make dist`, after `make loader rom`).

Each zip is an SD-card root (H:).

dist/gritty-v<VERSION>.zip, the release:
    rg2_v003.frm              the release firmware dist/rg2_v003_tty.frm (SHA-256 = the `output` line of
                              dist/rg2_v003_tty.manifest.txt, whose A: entries must be the current
                              scripts/rom_files.py set): stock 3.00 with TTY, its fonts and the rom stub
                              built in (stage 1/2 and fonts are not in the zip)
    tty/recipes/*.txt         recipes/*.txt (8.3 names)
    tty/recipes/licenses/*    recipes/licenses/* (licence texts of the film data)
    tty/fonts/licenses/*      fonts/licenses/* (OFL texts of the fonts built into the firmware; no .fnt files)
    tty/LICENSE.txt           LICENSE (MIT for TTY, third-party notes)
    rescue/backup.ttl         loader/flash/rescue/backup.ttl (ASCII + CRLF): the stock-reflash rescue card
                              script, inert until the user copies it to a card root (README "Recovery")

dist/gritty-v<VERSION>-debug.zip, for development and bug reports:
    rg2_v003.frm              the flashed debug loader (dist/rg2_v003_loader.frm, SHA-256 checked): stock
                              3.00 + the 132-byte stub that starts TTY from cfw/gr2_cfw.bin
    cfw/gr2_cfw.bin           bin/debug/gr2_cfw.bin (stage 1, loaded by the flashed loader). The one file
                              outside tty/: the reviewed debug loader opens exactly H:/cfw/gr2_cfw.bin
    tty/tty_main.bin          bin/debug/tty_main.bin (stage 2, loaded by stage 1)
    tty/fonts/*               fonts/ (overlay fonts from scripts/make_fonts.py, OFL licences)
    tty/recipes/*, tty/LICENSE.txt   as in the release zip
No Startup.ttl: on a camera with the loader it would load stage 1 a second time over the
running one (loader/flash/README.md §3). Also writes dist/SHA256SUMS.txt and dist/RELEASE_NOTES.md.

Checks before packaging: recipe files and the rescue script are ASCII with CRLF only, recipe
file names are 8.3, stage 1 fits the 32 KiB loader window, links at 0xA0017000 and carries valid
length/CRC self-check fields (scripts/stage1_crc.py), stage 2 has a valid header and carries the
current VERSION string (debug and rom builds), the debug .frm is the reviewed loader build.
"""

import hashlib
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
sys.path.insert(0, str(ROOT / "scripts"))
import stage1_crc  # noqa: E402

LOADER_FRM = DIST / "rg2_v003_loader.frm"
ROM_FRM = DIST / "rg2_v003_tty.frm"
ROM_MANIFEST = DIST / "rg2_v003_tty.manifest.txt"
RESCUE = ROOT / "loader" / "flash" / "rescue" / "backup.ttl"
WINDOW_BASE = 0xA0017000
WINDOW_SIZE = 0x8000
MAIN_MAGIC = 0x47524D31   # include/boot.h CFW_MAIN_MAGIC
LDR_MAGIC = 0x47524C31    # include/boot.h CFW_LDR_MAGIC (stage 1 +4, loader ABI v1)
RECIPES = ROOT / "recipes"
FONTS = ROOT / "fonts"
FONT_FILES = ("inm13.fnt", "insb15.fnt", "inm18.fnt", "insb18.fnt", "bcsb22.fnt")   # src/osd.c FONT_FILE
RECIPE_NAME = re.compile(r"^[a-z0-9_]{1,8}\.txt$")   # 8.3, lower case
ZIP_DATE = (2026, 1, 1, 0, 0, 0)  # fixed timestamp: reproducible zips


def fail(msg: str) -> None:
    sys.exit(f"package_sd: {msg}")


def check_text(path: Path) -> bytes:
    """ASCII with CRLF line endings only (camera TTL interpreter, TTY parsers)."""
    data = path.read_bytes()
    try:
        data.decode("ascii")
    except UnicodeDecodeError:
        fail(f"{path} is not ASCII")
    if b"\n" in data.replace(b"\r\n", b""):
        fail(f"{path} has LF without CR (needs CRLF)")
    if not data.endswith(b"\r\n"):
        fail(f"{path} must end with CRLF")
    return data


def collect_recipes() -> dict:
    """recipes/*.txt (8.3 names) -> tty/recipes/, recipes/licenses/* -> tty/recipes/licenses/.
    TTY lists only files directly in tty/recipes, so the licence texts are not
    mistaken for recipes."""
    members = {}
    if not RECIPES.is_dir():
        return members
    for path in sorted(RECIPES.iterdir()):
        if path.name.startswith(".") or path.is_dir():
            continue
        if not RECIPE_NAME.match(path.name):
            fail(f"{path.name}: recipe files must be 8.3 lower-case names ending in .txt")
        data = check_text(path)
        if b"\r\nname=" not in data or b"\r\nbase=" not in data:
            fail(f"{path.name}: a recipe needs name= and base=")
        members[f"tty/recipes/{path.name}"] = data
    licenses = RECIPES / "licenses"
    if members and not (licenses.is_dir() and any(licenses.iterdir())):
        fail("recipes/licenses/ (licence texts of the film data) is missing")
    for path in sorted(licenses.iterdir()) if licenses.is_dir() else []:
        if not path.name.startswith("."):
            members[f"tty/recipes/licenses/{path.name}"] = path.read_bytes()
    return members


def collect_font_licenses() -> dict:
    """fonts/licenses/* -> tty/fonts/licenses/ (OFL 1.1: the licence travels with every copy of the fonts,
    also the ones built into the release firmware)."""
    members = {f"tty/fonts/licenses/{p.name}": p.read_bytes()
               for p in sorted((FONTS / "licenses").iterdir()) if not p.name.startswith(".")}
    if not members:
        fail("fonts/licenses/ (OFL texts of the overlay fonts) is missing")
    return members


def collect_fonts() -> dict:
    """fonts/*.fnt -> tty/fonts/, fonts/licenses/* -> tty/fonts/licenses/."""
    members = {}
    for name in FONT_FILES:
        data = (FONTS / name).read_bytes() if (FONTS / name).exists() else b""
        if data[:4] != b"GFNT" or len(data) < 12 or int.from_bytes(data[8:12], "big") != len(data):
            fail(f"fonts/{name} missing or not a GFNT file: run scripts/make_fonts.py")
        members[f"tty/fonts/{name}"] = data
    return {**members, **collect_font_licenses()}


def check_payload(variant: str, version: str) -> tuple:
    bin_path = ROOT / "bin" / variant / "gr2_cfw.bin"
    elf_path = bin_path.with_suffix(".elf")
    main_path = ROOT / "bin" / variant / "tty_main.bin"
    if not bin_path.exists() or not elf_path.exists() or not main_path.exists():
        fail(f"{variant} binaries missing: run `make all` first")
    data = bin_path.read_bytes()
    if len(data) > WINDOW_SIZE:
        fail(f"{bin_path} is {len(data)} B, larger than the 0x8000 loader window")
    entry = subprocess.run(["arm-none-eabi-readelf", "-h", str(elf_path)],
                           capture_output=True, text=True, check=True).stdout
    if f"0x{WINDOW_BASE + 1:x}" not in entry and f"0x{WINDOW_BASE:x}" not in entry:
        fail(f"{elf_path} entry point is not at 0x{WINDOW_BASE:08X}")
    hdr, magic, hook = struct.unpack_from(">HxxII", data)
    if hdr >> 11 != 0x1C or magic != LDR_MAGIC or not (hook & 1) or \
            not WINDOW_BASE <= hook & ~1 < WINDOW_BASE + len(data):
        fail(f"{bin_path}: loader ABI v1 header (b over it, 'GRL1' at +4, Thumb hook in the image at +8) "
             "missing or wrong: rebuild")
    bad = stage1_crc.check(data)
    if bad:
        fail(f"{bin_path}: self-check fields (+12 length, +16 CRC-32): {bad}: rebuild")
    main = main_path.read_bytes()
    magic, image_size, _bss, _entry, nrelocs = struct.unpack_from(">5I", main)
    if magic != MAIN_MAGIC or len(main) != 32 + image_size + 4 * nrelocs:
        fail(f"{main_path} has a bad header or size: rebuild")
    if version.encode() not in main:
        fail(f"{main_path} does not contain version {version}: rebuild")
    return data, main


def release_frm(version: str) -> bytes:
    """dist/rg2_v003_tty.frm as `make rom` built and recorded it, from the current bin/rom and A: file set."""
    from flash_kit import manifest_sha          # here: flash_kit needs the manifest at import
    from rom_files import RomError, rom_files
    if not ROM_FRM.is_file():
        fail("dist/rg2_v003_tty.frm missing: run make rom")
    check_payload("rom", version)
    frm = ROM_FRM.read_bytes()
    if hashlib.sha256(frm).hexdigest() != manifest_sha():
        fail("dist/rg2_v003_tty.frm does not match the output SHA-256 of dist/rg2_v003_tty.manifest.txt: run make rom")
    try:
        files = rom_files(ROOT)
    except RomError as ex:
        fail(f"rom_files: {ex}")
    manifest = ROM_MANIFEST.read_text()
    listed = re.findall(r"^\s+0x[0-9A-F]+\s+[\d,]+\s+([0-9a-f]{64})\s+(A:/cfw/\S+)$", manifest, re.M)
    if sorted(listed) != sorted((hashlib.sha256(d).hexdigest(), n) for n, d in files.items()):
        fail("dist/rg2_v003_tty.manifest.txt lists other A: files than bin/rom + scripts/rom_files.py: run make rom")
    return frm


def write_zip(path: Path, members: dict) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in members.items():
            info = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def changes(version: str) -> list:
    """The `## v<version>` section of CHANGELOG.md as release-note lines; every release needs one."""
    text = (ROOT / "CHANGELOG.md").read_text()
    m = re.search(rf"^## v{re.escape(version)}\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m or not m.group(1).strip():
        fail(f"CHANGELOG.md has no section '## v{version}'")
    return ["## Changes", *m.group(1).strip().splitlines(), ""]


def main() -> None:
    from flash_kit import LOADER_SHA            # the reviewed loader build
    version = (ROOT / "VERSION").read_text().strip().lstrip("v")
    if not LOADER_FRM.is_file():
        fail("dist/rg2_v003_loader.frm missing: run make loader")
    loader = LOADER_FRM.read_bytes()
    if hashlib.sha256(loader).hexdigest() != LOADER_SHA:
        fail("dist/rg2_v003_loader.frm is not the reviewed loader build (scripts/flash_kit.py LOADER_SHA)")
    rom = release_frm(version)
    DIST.mkdir(exist_ok=True)
    for old in [*DIST.glob("gr2_cfw-v*.zip"), *DIST.glob("gritty-v*.zip")]:   # also stale v1.0.x names
        old.unlink()
    recipes = collect_recipes()
    if not recipes:
        fail("recipes/ holds no recipe files")
    license_txt = (ROOT / "LICENSE").read_bytes()
    boot, main_bin = check_payload("debug", version)

    zips = {
        f"gritty-v{version}.zip": ("release firmware: TTY is built into `rg2_v003.frm`; `tty/` holds the film "
                                   "recipes, `rescue/` the rescue card script. For everyday use.",
                                   {"rg2_v003.frm": rom, **recipes, **collect_font_licenses(),
                                    "tty/LICENSE.txt": license_txt, "rescue/backup.ttl": check_text(RESCUE)}),
        f"gritty-v{version}-debug.zip": ("debug loader + debug TTY on the card (every log line, key trace, `tty "
                                         "status` at power-off). For development and bug reports only.",
                                         {"rg2_v003.frm": loader, "cfw/gr2_cfw.bin": boot, "tty/tty_main.bin": main_bin,
                                          **collect_fonts(), **recipes, "tty/LICENSE.txt": license_txt}),
    }
    sums = []
    for name, (desc, members) in zips.items():
        zip_path = DIST / name
        write_zip(zip_path, members)
        sums.append((hashlib.sha256(zip_path.read_bytes()).hexdigest(), name, desc))
        print(f"packaged {zip_path.relative_to(ROOT)} ({len(members)} files)")

    (DIST / "SHA256SUMS.txt").write_text("".join(f"{h}  {n}\n" for h, n, _ in sums))

    lines = [
        f"# GR II TTY v{version}",
        "",
        "Custom firmware for the Ricoh GR II (firmware 3.00), TTY for short. `rg2_v003.frm` in "
        f"`gritty-v{version}.zip` is Ricoh's firmware 3.00 with TTY built in: it starts with the camera, "
        "with or without a card. Hold the shutter half-pressed while switching on to start stock once. "
        "Site: https://robbcocco.github.io/gritty/",
        "",
        "## Install",
        "1. Charge the battery to **full**. Format a card **in the camera** (FAT32 if 32 GB or smaller).",
        f"2. Unzip `gritty-v{version}.zip` and copy **only `rg2_v003.frm`** to the card root. On macOS, remove "
        "`._*` files afterwards (`find /Volumes/<card> -name '._*' -delete`). The card must hold no other "
        "`rg2_v*.frm`.",
        "3. MENU → Set-up → *Confirm Firmware Version*: it shows **Execute?** with *New Version 3.00*. Choose **Yes**, "
        "wait for **Firmware update complete.**, press OK (the camera switches off), switch it on.",
        "4. Copy the zip's `tty/` folder (the film recipes) to the card root; `rg2_v003.frm` can go.",
        "5. In *Key Custom Options*, set *Effect Button Setting* (or *Effect Button Hold Setting*) to **TTY Menu** or "
        "**Recipes**.",
        "",
        "Updating: install a newer `rg2_v003.frm` the same way; recipe files never need a reflash. Uninstall: flash "
        "Ricoh's official `rg2_v003.frm` the same way. `rescue/backup.ttl` is the rescue card script for a camera "
        "that no longer starts (README, *Recovery*); never leave it in a card root.",
        "",
        "Updating from v1.0.x: flash the new `rg2_v003.frm` as usual, then move `cfw/recipes/` to `tty/recipes/` "
        "(and `cfw/export/`, `cfw/config.txt` if you want them) and delete `cfw/`. There is no fallback: TTY reads "
        "only `tty/`.",
        "",
        "## Licences",
        "TTY is MIT-licensed (`tty/LICENSE.txt`). `rg2_v003.frm` contains Ricoh's firmware, which is Ricoh's "
        "property and not covered by that licence; this project is not affiliated with Ricoh. "
        "The film recipes in `tty/recipes/` are fitted to [spektrafilm](https://github.com/andreavolpato/spektrafilm) "
        "film simulations by Andrea Volpato (CC BY-SA 4.0); Rangefinder and the Mono recipes are the project's own "
        "designs (CC BY 4.0). "
        "Attribution is in each recipe file; licence texts in `tty/recipes/licenses/`. Film names are used "
        "descriptively; not affiliated with Kodak or Fujifilm. "
        "The overlay fonts built into the firmware are rendered from Inter and Barlow Condensed (SIL Open Font "
        "License 1.1, `tty/fonts/licenses/`).",
        "",
    ]
    lines += changes(version)
    lines += [
        "## Downloads",
        "| File | Contents | SHA-256 |",
        "|---|---|---|",
    ]
    lines += [f"| `{n}` | {desc} | `{h}` |" for h, n, desc in sums]
    lines.append("")
    (DIST / "RELEASE_NOTES.md").write_text("\n".join(lines))


if __name__ == "__main__":
    main()
