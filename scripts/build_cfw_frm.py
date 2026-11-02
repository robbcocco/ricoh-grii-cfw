#!/usr/bin/env python3
"""Build dist/rg2_v003_cfw.frm: the M11 release firmware (`make rom`).

stock rg2_v003.frm + loader/flash/rom_stub.S in APP + the CFW files (scripts/rom_files.py,
bin/rom/) appended to the A: drive in RES (finding 29 §8, loader/flash/README.md §8).
Builds and checks only; flashing is a user step (README.md §6.4, P-steps) after
`make check`. The output contains Ricoh's firmware: never publish it.

Steps:
  1. Stock SHA-256 of the input (default fw/rg2_v003.frm from `make fw`); container =
     RES (type 2) + APP (type 1) only; stock A: directory = 121 entries at payload
     0x785E00 ending at the image end (0x787C80).
  2. APP: assemble rom_stub.S (ldr_stub.ld placement), assert the stock bytes at the hook
     BL and the 132-byte 0xFF range and the stub's BL targets (Port_Get, LoadFile,
     Section_Load), patch in place (same length), like build_loader_frm.py.
  3. RES (stock bytes never move): payload 0x787C80-0x787DFF 0x00; from 0x787E00 the CFW
     files, each 0x200-aligned and 0x00-padded; then the directory = the 121 stock
     entries verbatim + one entry per CFW file (FAT date/time 2026-01-01 00:00 x3,
     attribute 0x01) + a 64-byte zero terminator; the u32 at payload 0 points at it.
     Header: +0x164 nUpdateTotalSize = payload size (<= 0x7E0000, data stays in blocks
     0x62-0xA0), +0x168 nUpdateFileSize = 0x200 + +0x164, nUpdateCsum recomputed;
     bCheckRomSize (+0x17C) stays 1, every other header byte stock.
  4. Container via gr2frm.repack (resizes the RES entry, moves the nested APP container,
     new total length), then verify: round trip, `gr2frm.py verify`, header sizes and
     checksums, RES = stock up to the old directory, every stock and CFW file read back
     through the new directory, APP diff = hook + stub + nUpdateCsum, the nested container
     differs only in APP, the outer header only in its length words.
  5. Write the .frm and dist/rg2_v003_cfw.manifest.txt (header fields, container layout,
     A: entries with offsets/sizes/SHA-256, APP diff runs, room left, stub listing).
scripts/emu_release.py then checks the output independently (byte checks + stock code).

Usage: python3 scripts/build_cfw_frm.py [--stock FRM] [--out FRM] [--variant rom]
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from build_loader_frm import (APP, HOOK_ADDR, HOOK_STOCK, RES, SECTION_LOAD, STATE_ADDR,  # noqa: E402
                              STOCK_FRM, STOCK_FRM_SHA256, STUB_ADDR, build_patches, check_container,
                              diff_ranges, fail, off, sha256, thumb_bl)
from gr2frm import (AFS_DIR_PTR, AFS_ENTRY, CSUM_OFF, IMAGE_HDR, afs_entries, nupdate_csum,  # noqa: E402
                    parse_container, repack, unpack, update_header, walk, with_csum)
from rom_files import STAGE1, RomError, rom_files  # noqa: E402

ROM_STUB = REPO_ROOT / "loader" / "flash" / "rom_stub.S"
ROM_STUB_CALLS = {0xA00408D8: "Port_Get", 0xA002CD2C: "LoadFile", 0xA003FB02: "Section_Load"}

STOCK_DIR, STOCK_ENTRIES, STOCK_PAYLOAD = 0x785E00, 121, 0x787C80   # finding 29 §2
CFW_START = 0x787E00                    # first CFW file: payload offset, 0x200-aligned
RES_CAP = 0x7E0000                      # nUpdateTotalSize cap: blocks 0x62-0xA0 (finding 29 §5)
APP_CAP = 0x800000
DIR_MAX = 1024                          # 0x80 directory sectors
FAT_STAMP = struct.pack(">HH", (2026 - 1980) << 9 | 1 << 5 | 1, 0) * 3   # 2026-01-01 00:00, reproducible
ATTR_RO = 0x01
HDR_SIZES = (0x164, 0x170)              # nUpdateTotalSize, nUpdateFileSize, nUpdateCsum


def align(n, a=0x200):
    return -(-n // a) * a


def stock_dir(res):
    """The stock A: directory: (payload offset, [64-byte entries]); asserts finding 29 §2."""
    d = struct.unpack_from(">I", res, AFS_DIR_PTR)[0]
    total = struct.unpack_from(">I", res, 0x164)[0]
    ents = [res[IMAGE_HDR + d + i * AFS_ENTRY:IMAGE_HDR + d + (i + 1) * AFS_ENTRY] for i in range(STOCK_ENTRIES + 1)]
    if (d, total, len(res)) != (STOCK_DIR, STOCK_PAYLOAD, IMAGE_HDR + STOCK_PAYLOAD) or \
            any(struct.unpack_from(">I", e, 0x24)[0] == 0 for e in ents[:-1]) or ents[-1] != bytes(AFS_ENTRY) or \
            d + (STOCK_ENTRIES + 1) * AFS_ENTRY != total or any(res[IMAGE_HDR + 4:IMAGE_HDR + 0x200]):
        fail("stock RES A: directory is not 121 entries at payload 0x785E00 ending at the image end")
    return d, ents[:-1]


def build_res(stock_res, files):
    """Stock RES + `files` ({A: path: bytes}) appended + new directory (module doc, step 3).
    -> (image, [(name, payload offset, size)], directory payload offset)."""
    _, entries = stock_dir(stock_res)
    res = bytearray(stock_res)
    res += bytes(IMAGE_HDR + CFW_START - len(res))
    placed = []
    for name, data in files.items():
        o = len(res) - IMAGE_HDR
        res += data
        res += bytes(align(len(res)) - len(res))
        placed.append((name, o, len(data)))
    d = len(res) - IMAGE_HDR
    res += b"".join(entries)
    for name, o, size in placed:
        res += name.encode("ascii").ljust(32, b"\0") + struct.pack(">II", o, size) + FAT_STAMP + bytes([ATTR_RO]) \
            + bytes(11)
    res += bytes(AFS_ENTRY)
    total = len(res) - IMAGE_HDR
    struct.pack_into(">I", res, AFS_DIR_PTR, d)
    struct.pack_into(">II", res, 0x164, total, len(res))
    return with_csum(bytes(res)), placed, d


def check_files(files, stock_names):
    if len(files) + STOCK_ENTRIES > DIR_MAX:
        fail(f"{len(files)} CFW files + {STOCK_ENTRIES} stock entries exceed {DIR_MAX} directory entries")
    for name in files:
        if any(s.startswith(name) or name.startswith(s) for s in stock_names) or \
                any(s.startswith("A:/cfw") for s in stock_names):
            fail(f"{name} collides with a stock A: name or prefix")
    if STAGE1 not in files or len(files[STAGE1]) > 0x8000:
        fail(f"{STAGE1} missing or larger than the stub's 0x8000 LoadFile limit")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stock", type=Path, default=STOCK_FRM)
    ap.add_argument("--out", type=Path, default=REPO_ROOT / "dist" / "rg2_v003_cfw.frm")
    ap.add_argument("--variant", default="rom", help="bin/<variant>/ for the CFW binaries")
    a = ap.parse_args(argv)

    frm = a.stock.read_bytes()
    if sha256(frm) != STOCK_FRM_SHA256:
        fail(f"{a.stock} is not the factory rg2_v003.frm")
    images = unpack(frm)
    sec0_lo, sec0_hi, _ = check_container(frm, images)
    stock_res, stock_app = images[RES], images[APP]
    _, stock_entries = stock_dir(stock_res)
    stock_afs = afs_entries(stock_res)
    print(f"[OK] stock {a.stock.name}: {RES} (RES) + {APP} (APP), no IPL/BAK; A: = {len(stock_afs)} files, "
          f"directory at payload 0x{STOCK_DIR:X}, payload 0x{STOCK_PAYLOAD:X}")

    try:
        files = rom_files(REPO_ROOT, a.variant)
    except RomError as ex:
        fail(str(ex))
    check_files(files, [n for n, _, _ in stock_afs])

    # ── APP: the release stub ──
    patches, calls, listing = build_patches(ROM_STUB, ROM_STUB_CALLS)
    stub = patches[1][3]
    if STAGE1.encode() + b"\0" not in stub:
        fail(f"rom_stub.S does not load {STAGE1}")
    app = bytearray(stock_app)
    for name, addr, stock, new in patches:
        if not (sec0_lo <= addr and addr + len(new) <= sec0_hi) or len(new) != len(stock):
            fail(f"{name} 0x{addr:08X} is outside section 0 or changes length")
        cur = bytes(app[off(addr):off(addr) + len(stock)])
        if cur != stock:
            fail(f"{name} @0x{addr:08X}: stock bytes {cur.hex()} != expected {stock.hex()}")
        app[off(addr):off(addr) + len(new)] = new
    if thumb_bl(HOOK_ADDR, HOOK_STOCK) != (SECTION_LOAD, False):
        fail("stock hook does not decode to bl Section_Load")
    app = with_csum(bytes(app))
    print("[OK] APP: stock bytes asserted; rom_stub.S patched in place (same length)")

    # ── RES: CFW files appended to A: ──
    res, placed, new_dir = build_res(stock_res, files)
    total = len(res) - IMAGE_HDR
    if total > RES_CAP:
        fail(f"RES nUpdateTotalSize 0x{total:X} > cap 0x{RES_CAP:X}")
    print(f"[OK] RES: {len(placed)} CFW files from payload 0x{CFW_START:X}, directory at 0x{new_dir:X}, "
          f"payload 0x{total:X} (cap 0x{RES_CAP:X}, {RES_CAP - total:,} B left)")

    out = repack(frm, {APP: app, RES: res})
    new_images = unpack(out)

    # ── verification of the output ──
    nr, na = new_images[RES], new_images[APP]
    if nr != res or na != app:
        fail("images do not round-trip through the container")
    if repack(out, new_images) != out:
        fail("round trip not byte-identical")
    for name, img in new_images.items():
        h = update_header(img)
        if h["nUpdateCsum"] != nupdate_csum(img):
            fail(f"{name}: nUpdateCsum mismatch")
        if h["nUpdateFileSize"] != len(img) or h["nUpdateTotalSize"] != len(img) - IMAGE_HDR:
            fail(f"{name}: header sizes inconsistent")
        if (h["bExecVerify"], h["bCheckHeader"], h["bCompareCsum"], h["bCheckRomSize"]) != (1, 1, 1, 1):
            fail(f"{name}: +0x170..+0x17C must stay 1")
    hdr_diff = diff_ranges(stock_res[:IMAGE_HDR], nr[:IMAGE_HDR])
    if any(not (HDR_SIZES[0] <= lo and hi <= HDR_SIZES[1]) for lo, hi in hdr_diff):
        fail("RES header differs outside +0x164..+0x16F")
    if nr[IMAGE_HDR + 4:IMAGE_HDR + STOCK_PAYLOAD] != stock_res[IMAGE_HDR + 4:] or \
            any(nr[IMAGE_HDR + STOCK_PAYLOAD:IMAGE_HDR + CFW_START]):
        fail("RES stock bytes changed")
    afs = afs_entries(nr)
    if len(afs) != STOCK_ENTRIES + len(files) or [n for n, _, _ in afs[:STOCK_ENTRIES]] != \
            [n for n, _, _ in stock_afs]:
        fail("new A: directory is not the stock entries + the CFW entries")
    dir_bytes = nr[IMAGE_HDR + new_dir:]
    if dir_bytes[:STOCK_ENTRIES * AFS_ENTRY] != b"".join(stock_entries) or dir_bytes[-AFS_ENTRY:] != bytes(AFS_ENTRY):
        fail("stock entries not verbatim or terminator missing")
    for (name, o, size), (_, so, ssize) in zip(afs, stock_afs, strict=False):
        if nr[o:o + size] != stock_res[so:so + ssize]:
            fail(f"{name} does not read back as stock")
    for name, o, size in afs[STOCK_ENTRIES:]:
        if nr[o:o + size] != files[name] or (o - IMAGE_HDR) % 0x200:
            fail(f"{name} does not read back or is not 0x200-aligned")
    app_diff = diff_ranges(stock_app, na)
    allowed = [(CSUM_OFF, CSUM_OFF + 4)] + [(off(ad), off(ad) + len(n)) for _, ad, _, n in patches]
    if any(not any(alo <= lo and hi <= ahi for alo, ahi in allowed) for lo, hi in app_diff):
        fail("APP differs outside the hook, the stub and nUpdateCsum")
    if len(na) - IMAGE_HDR > APP_CAP:
        fail("APP payload over its cap")
    (_, n_stock), (_, n_new) = [next((e, raw) for _, e, raw, nested in walk(f) if nested) for f in (frm, out)]
    nested_diff = diff_ranges(n_stock, n_new)
    app_entry = next(e for _, e, _, nested in walk(n_new) if not nested and e.name == APP)
    if len(n_stock) != len(n_new) or any(not (app_entry.offset <= lo and hi <= app_entry.offset + app_entry.length)
                                         for lo, hi in nested_diff):
        fail("nested container differs outside the APP image")
    total_new, root = parse_container(out)
    if len(root) != 2 or [e.name for e in root] != [RES, "rg2_v001up"] or total_new != len(out):
        fail("outer container must keep RES + rg2_v001up")
    hdr_out = diff_ranges(frm[:0x800], out[:0x800])
    if any(not (lo >= 0x20 and hi <= 0x24 or lo >= 0x44 and hi <= 0x48 or lo >= 0x80 and hi <= 0x84)
           for lo, hi in hdr_out):
        fail("outer container header differs outside total length, RES length and APP-container sector")

    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(out)
    v = subprocess.run([sys.executable, str(TOOLS / "gr2frm.py"), "verify", str(a.out)],
                       capture_output=True, text=True, check=False)
    print(v.stdout.rstrip())
    if v.returncode:
        a.out.unlink()
        fail("gr2frm.py verify failed; output removed")

    # ── manifest ──
    hs, hn = update_header(stock_res), update_header(nr)
    ha_s, ha_n = update_header(stock_app), update_header(na)
    rep = [
        "rg2_v003_cfw.frm - M11 release firmware (NOT FLASHED; loader/flash/README.md §8 and §6.4 first)",
        "built by scripts/build_cfw_frm.py from loader/flash/rom_stub.S, bin/"
        f"{a.variant}/ and scripts/rom_files.py; checked by scripts/emu_release.py",
        "",
        f"stock   {a.stock.name}  {len(frm):,} B  sha256 {sha256(frm)}",
        f"output  {a.out.name}  {len(out):,} B  sha256 {sha256(out)}",
        "",
        "container (sector = 512 B; layout rebuilt by gr2frm.repack, padding 0xFF):",
        f"  total length      0x{len(frm):X} -> 0x{len(out):X}",
        *(f"  {e.name:16s}  sector {e.sector:5d} len 0x{e.length:X}" for e in root),
        "",
        f"{RES} (RES, type 2) update header (all other header bytes stock):",
        f"  nUpdateTotalSize  0x{hs['nUpdateTotalSize']:X} -> 0x{hn['nUpdateTotalSize']:X} "
        f"(cap 0x{RES_CAP:X}: {RES_CAP - hn['nUpdateTotalSize']:,} B left for more A: files)",
        f"  nUpdateFileSize   0x{hs['nUpdateFileSize']:X} -> 0x{hn['nUpdateFileSize']:X} (= 0x200 + total)",
        f"  nUpdateCsum       0x{hs['nUpdateCsum']:08X} -> 0x{hn['nUpdateCsum']:08X}",
        f"  bExecVerify/bCheckHeader/bCompareCsum/bCheckRomSize  {hn['bExecVerify']}/{hn['bCheckHeader']}/"
        f"{hn['bCompareCsum']}/{hn['bCheckRomSize']} (stock)",
        f"  payload 0x0       directory pointer 0x{STOCK_DIR:X} -> 0x{new_dir:X}",
        f"  payload 0x4-0x{STOCK_PAYLOAD - 1:X}  stock bytes (old directory at 0x{STOCK_DIR:X} unused)",
        f"  payload 0x{STOCK_PAYLOAD:X}-0x{CFW_START - 1:X}  0x00",
        f"  payload 0x{CFW_START:X}-0x{new_dir - 1:X}  CFW files (0x200-aligned, 0x00 padding)",
        f"  payload 0x{new_dir:X}-0x{hn['nUpdateTotalSize'] - 1:X}  directory: {STOCK_ENTRIES} stock entries "
        f"verbatim + {len(placed)} CFW entries + terminator",
        "",
        "A: CFW entries (payload offset, size, sha256):",
        *(f"  0x{o:06X}  {size:7,d}  {sha256(files[n])}  {n}" for n, o, size in placed),
        f"  {sum(s for _, _, s in placed):,} B in {len(placed)} files",
        "",
        f"{APP} (APP, type 1): nUpdateCsum 0x{ha_s['nUpdateCsum']:08X} -> 0x{ha_n['nUpdateCsum']:08X}; "
        "length and sizes stock",
        "APP diff runs (image offset = RAM - 0xA0020200):",
        *(f"  0x{lo:06X}-0x{hi - 1:06X}  {hi - lo:3d} B" for lo, hi in app_diff),
        f"  hook 0x{HOOK_ADDR:08X}: bl 0x{SECTION_LOAD:08X} (Section_Load) -> bl 0x{STUB_ADDR:08X} (rom stub)",
        "  stub calls: " + ", ".join(f"0x{ad:08X} bl {ROM_STUB_CALLS[t]} 0x{t:08X}" for ad, t, _ in calls),
        f"  ldr_state 0x{STATE_ADDR:08X} = 0 in flash",
        "",
        "gr2frm.py verify:",
        *("  " + line for line in v.stdout.rstrip().splitlines()),
        "",
        "stub listing (arm-none-eabi-objdump):",
        *("  " + line for line in listing.strip().splitlines()[2:]),
    ]
    manifest = a.out.with_suffix(".manifest.txt")
    manifest.write_text("\n".join(rep) + "\n")
    print(f"[OK] wrote {a.out} ({len(out):,} B, sha256 {sha256(out)})")
    print(f"[OK] wrote {manifest}")
    print("Not flashed. Follow loader/flash/README.md §8.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
