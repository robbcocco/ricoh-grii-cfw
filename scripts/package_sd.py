#!/usr/bin/env python3
"""Package the release and debug zips (`make dist`, after `make loader`).

Each zip is an SD-card root (H:):
    rg2_v003.frm         the flashed debug loader (dist/rg2_v003_loader.frm, SHA-256 checked): stock
                         3.00 + the 132-byte stub that starts the CFW from cfw/ (until M11 replaces it
                         with the release firmware that carries the CFW itself)
    cfw/gr2_cfw.bin      (bin/<variant>/gr2_cfw.bin: stage 1, loaded by the flashed loader)
    cfw/gr2_main.bin     (bin/<variant>/gr2_main.bin: stage 2, loaded by stage 1)
    cfw/recipes/*        (recipes/: recipe files and their licence notice)
    cfw/fonts/*          (fonts/: overlay fonts from scripts/make_fonts.py, OFL licences)
    cfw/LICENSE.txt      (LICENSE: MIT for the CFW, third-party notes)
No Startup.ttl: on a camera with the loader it would load stage 1 a second time over the
running one (loader/flash/README.md §3). Also writes dist/SHA256SUMS.txt and dist/RELEASE_NOTES.md.

Checks before packaging: recipe files are ASCII with CRLF only, recipe file names are
8.3, stage 1 fits the 32 KiB loader window and links at 0xA0017000, stage 2 has a valid header
and carries the current VERSION string, the .frm is the reviewed loader build.
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
from flash_kit import LOADER_SHA  # noqa: E402  (the reviewed loader build)

LOADER_FRM = DIST / "rg2_v003_loader.frm"
VARIANTS = {
    "release": "Errors only in H:/cfw/cfw_log.txt. For everyday use.",
    "debug": "Every log line, the key trace and the `cfw status` dump at power-off. For testing and bug reports.",
}
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
    """ASCII with CRLF line endings only (camera TTL interpreter, CFW parsers)."""
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
    """recipes/*.txt (8.3 names) -> cfw/recipes/, recipes/licenses/* -> cfw/recipes/licenses/.
    The CFW lists only files directly in cfw/recipes, so the licence texts are not
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
        members[f"cfw/recipes/{path.name}"] = data
    licenses = RECIPES / "licenses"
    if members and not (licenses.is_dir() and any(licenses.iterdir())):
        fail("recipes/licenses/ (licence texts of the film data) is missing")
    for path in sorted(licenses.iterdir()) if licenses.is_dir() else []:
        if not path.name.startswith("."):
            members[f"cfw/recipes/licenses/{path.name}"] = path.read_bytes()
    return members

def collect_fonts() -> dict:
    """fonts/*.fnt -> cfw/fonts/, fonts/licenses/* -> cfw/fonts/licenses/."""
    members = {}
    for name in FONT_FILES:
        data = (FONTS / name).read_bytes() if (FONTS / name).exists() else b""
        if data[:4] != b"GFNT" or len(data) < 12 or int.from_bytes(data[8:12], "big") != len(data):
            fail(f"fonts/{name} missing or not a GFNT file: run scripts/make_fonts.py")
        members[f"cfw/fonts/{name}"] = data
    for path in sorted((FONTS / "licenses").iterdir()):
        if not path.name.startswith("."):
            members[f"cfw/fonts/licenses/{path.name}"] = path.read_bytes()
    return members



def check_payload(variant: str, version: str) -> tuple:
    bin_path = ROOT / "bin" / variant / "gr2_cfw.bin"
    elf_path = bin_path.with_suffix(".elf")
    main_path = ROOT / "bin" / variant / "gr2_main.bin"
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
    main = main_path.read_bytes()
    magic, image_size, _bss, _entry, nrelocs = struct.unpack_from(">5I", main)
    if magic != MAIN_MAGIC or len(main) != 32 + image_size + 4 * nrelocs:
        fail(f"{main_path} has a bad header or size: rebuild")
    if version.encode() not in main:
        fail(f"{main_path} does not contain version {version}: rebuild")
    return data, main


def write_zip(path: Path, members: dict) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in members.items():
            info = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def main() -> None:
    version = (ROOT / "VERSION").read_text().strip().lstrip("v")
    if not LOADER_FRM.is_file():
        fail("dist/rg2_v003_loader.frm missing: run make loader")
    frm = LOADER_FRM.read_bytes()
    if hashlib.sha256(frm).hexdigest() != LOADER_SHA:
        fail("dist/rg2_v003_loader.frm is not the reviewed loader build (scripts/flash_kit.py LOADER_SHA)")
    DIST.mkdir(exist_ok=True)
    for old in DIST.glob("gr2_cfw-v*.zip"):
        old.unlink()
    recipes = collect_recipes()
    fonts = collect_fonts()

    sums = []
    for variant in VARIANTS:
        boot, main_bin = check_payload(variant, version)
        zip_path = DIST / f"gr2_cfw-v{version}-{variant}.zip"
        members = {"rg2_v003.frm": frm, "cfw/gr2_cfw.bin": boot, "cfw/gr2_main.bin": main_bin, **fonts, **recipes,
                   "cfw/LICENSE.txt": (ROOT / "LICENSE").read_bytes()}
        write_zip(zip_path, members)
        sums.append((hashlib.sha256(zip_path.read_bytes()).hexdigest(), zip_path.name))
        print(f"packaged {zip_path.relative_to(ROOT)} (stage 2 {len(main_bin)} B, {len(recipes)} recipe files)")

    (DIST / "SHA256SUMS.txt").write_text("".join(f"{h}  {n}\n" for h, n in sums))

    lines = [
        f"# gr2_cfw v{version}",
        "",
        "Custom firmware for the Ricoh GR II (firmware 3.00). `rg2_v003.frm` is Ricoh's firmware 3.00 plus a "
        "132-byte loader that starts the CFW from the card's `cfw/` folder at power-on. Without the card, or with "
        "the shutter half-pressed while switching on, the camera boots stock.",
        "",
        "## Install",
        "1. Charge the battery to **full**. Format a card **in the camera** (FAT32 if 32 GB or smaller).",
        "2. Unzip **one** of the zips below into the card root. On macOS, remove `._*` files afterwards "
        "(`find /Volumes/<card> -name '._*' -delete`). The card must hold no other `rg2_v*.frm`.",
        "3. MENU → Set-up → *Confirm Firmware Version*: it shows **Execute?** with *New Version 3.00*. Choose **Yes**, "
        "wait for **Firmware update complete.**, press OK (the camera switches off), switch it on.",
        "4. In *Key Custom Options*, set *Effect Button Setting* (or *Effect Button Hold Setting*) to **CFW Menu** or "
        "**Recipes**.",
        "",
        "Updating the CFW later only needs the new `cfw/` folder; reflash only when `rg2_v003.frm` changes. "
        "Never put a `Startup.ttl` from older CFW versions on the card. Uninstall: flash Ricoh's official "
        "`rg2_v003.frm` the same way.",
        "",
        "## Licences",
        "The CFW is MIT-licensed (`cfw/LICENSE.txt`). `rg2_v003.frm` contains Ricoh's firmware, which is Ricoh's "
        "property and not covered by that licence; this project is not affiliated with Ricoh. "
        "The film recipes in `cfw/recipes/` are derived from [a third-party GR III project fits of "
        "[spektrafilm](https://github.com/andreavolpato/spektrafilm) looks by Andrea Volpato (CC BY-SA 4.0) and, for Classic Neg, of "
        "[FujifilmCameraProfiles](https://github.com/abpy/FujifilmCameraProfiles) by abpy (CC BY-NC-SA 4.0: non-commercial only). "
        "Attribution is in each recipe file; licence texts in `cfw/recipes/licenses/`. Film names are used descriptively; not affiliated with Kodak or Fujifilm. "
        "The overlay fonts in `cfw/fonts/` are rendered from Inter and Barlow Condensed (SIL Open Font License 1.1, `cfw/fonts/licenses/`).",
        "",
        "## Downloads",
        "| File | Build | SHA-256 |",
        "|---|---|---|",
    ]
    for (h, n), (variant, desc) in zip(sums, VARIANTS.items(), strict=True):
        lines.append(f"| `{n}` | {variant}: {desc} | `{h}` |")
    lines.append("")
    (DIST / "RELEASE_NOTES.md").write_text("\n".join(lines))


if __name__ == "__main__":
    main()
