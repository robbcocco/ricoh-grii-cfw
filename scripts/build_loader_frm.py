#!/usr/bin/env python3
"""Build dist/rg2_v003_loader.frm: stock rg2_v003.frm + the M10 flashed loader.

Flashing experiment, outside the CFW build: not run by make, `make dist` or CI.
Read loader/flash/README.md (user-side test plan, recovery) before flashing.

Steps:
  1. Pre-flight safety gate (tools/verify_safety.py) and stock SHA-256.
  2. Assemble loader/flash/ldr_stub.S at its APP addresses (arm-none-eabi-as/ld).
  3. Check the container: exactly b01firm6 (RES, type 2) and b01firm8 (APP, type 1);
     never IPL (type 3) or BAK (type 0). Only b01firm8 is patched.
  4. Assert the stock bytes at both patch sites (hook BL, 132 B of 0xFF), that both
     lie in section 0 (loaded by the IPL), and every BL target in the stub.
  5. Patch same-length, repack with gr2frm (recomputes nUpdateCsum), then verify:
     b01firm6 byte-identical, b01firm8 differs only in the patch ranges and the
     nUpdateCsum field, round trip, header sizes, checksum, `gr2frm.py verify`.
  6. Write the .frm and a diff report (dist/rg2_v003_loader.diff.txt).

Usage: python3 scripts/build_loader_frm.py [--stock FRM] [--out FRM]
"""
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LOCAL_TOOLS = REPO_ROOT / "tools"
PARENT_TOOLS = REPO_ROOT.parent / "the reverse-engineering notes" / "tools"
TOOLS = LOCAL_TOOLS if LOCAL_TOOLS.exists() else PARENT_TOOLS
sys.path.insert(0, str(TOOLS))

from gr2frm import (CSUM_OFF, IMAGE_HDR, RTOS_BASE, nupdate_csum, parse_container,  # noqa: E402
                    repack, unpack, update_header, walk)
from verify_safety import check_safety  # noqa: E402

STOCK_FRM = REPO_ROOT.parent / "the reverse-engineering notes" / "recovery_kit" / "rg2_v003.frm"
STOCK_FRM_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
STUB_SRC = REPO_ROOT / "loader" / "flash" / "ldr_stub.S"
STUB_LD = REPO_ROOT / "loader" / "flash" / "ldr_stub.ld"
APP, RES = "b01firm8", "b01firm6"
APP_CSUM_STOCK = 0x826A5E33
RES_CSUM_STOCK = 0xA3A80234

HOOK_ADDR = 0xA0022E5C
HOOK_STOCK = bytes.fromhex("F01CFE51")         # bl 0xA003FB02 (Section_Load)
STUB_ADDR, STUB_SIZE = 0xA00AFCF0, 0x84         # unreferenced 0xFF run (finding 22 §3.2)
STATE_ADDR = 0xA00AFD70                         # ldr_state, last word of the range
SECTION_LOAD = 0xA003FB02
STUB_CALLS = {                                  # BL targets the stub must contain
    0xA00408D8: "Port_Get",
    0xA002D350: "File_MountMedia",
    0xA002CD2C: "LoadFile",
    0xA003FB02: "Section_Load",
}


def fail(msg):
    print(f"[FAIL] {msg}")
    sys.exit(1)


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def off(addr):
    """APP image offset (with the 0x200 update header) of a RAM address."""
    return addr - RTOS_BASE


# ── stub assembly ─────────────────────────────────────────────────────────
def elf_sections(elf):
    """{name: (addr, bytes)} for the allocated sections of a 32-bit big-endian ELF."""
    shoff, = struct.unpack_from(">I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", elf, 0x2E)
    hdrs = [struct.unpack_from(">10I", elf, shoff + i * shentsize) for i in range(shnum)]
    strtab = hdrs[shstrndx]
    names = elf[strtab[4]:strtab[4] + strtab[5]]
    out = {}
    for name_off, _type, flags, addr, offset, size, *_ in hdrs:
        if flags & 2:                                   # SHF_ALLOC
            name = names[name_off:names.index(b"\0", name_off)].decode()
            out[name] = (addr, elf[offset:offset + size])
    return out


def assemble_stub():
    with tempfile.TemporaryDirectory() as tmp:
        obj, elf = Path(tmp) / "ldr_stub.o", Path(tmp) / "ldr_stub.elf"
        for cmd in (["arm-none-eabi-as", "-mbig-endian", "-march=armv5te", "-mthumb-interwork",
                     "-o", str(obj), str(STUB_SRC)],
                    ["arm-none-eabi-ld", "-EB", "-T", str(STUB_LD), "-o", str(elf), str(obj)]):
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                fail(f"{cmd[0]}: {r.stderr.strip()}")
        secs = elf_sections(elf.read_bytes())
        listing = subprocess.run(["arm-none-eabi-objdump", "-d", "-j", ".ldr_hook", "-j", ".ldr_stub",
                                  str(elf)], capture_output=True, text=True).stdout
    return secs, listing


def thumb_bl(addr, code):
    """(target, is_blx) of the Thumb BL/BLX pair at addr, or None."""
    h1, h2 = struct.unpack(">HH", code)
    if h1 & 0xF800 != 0xF000 or h2 & 0xE800 != 0xE800:
        return None
    o = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
    if o & 0x400000:
        o -= 0x800000
    t = addr + 4 + o
    blx = h2 & 0xF800 == 0xE800
    return (t & ~3 if blx else t) & 0xFFFFFFFF, blx


def stub_calls(stub):
    """Every BL/BLX in the stub's code part (up to its literal pool)."""
    calls, i = [], 0
    while i < len(stub) - 3:
        bl = thumb_bl(STUB_ADDR + i, stub[i:i + 4])
        if bl:
            calls.append((STUB_ADDR + i, *bl))
            i += 4
        else:
            i += 2
            if stub[i - 2:i] == b"\xbd\xf8":            # pop {r3-r7, pc}: end of code
                break
    return calls


# ── build ─────────────────────────────────────────────────────────────────
def build_patches():
    secs, listing = assemble_stub()
    if set(secs) != {".ldr_hook", ".ldr_stub"}:
        fail(f"unexpected sections {sorted(secs)}")
    hook_addr, hook = secs[".ldr_hook"]
    stub_addr, stub = secs[".ldr_stub"]
    if (hook_addr, len(hook)) != (HOOK_ADDR, 4) or (stub_addr, len(stub)) != (STUB_ADDR, STUB_SIZE):
        fail("stub sections not at 0xA0022E5C/4 B and 0xA00AFCF0/0x84 B")
    if thumb_bl(HOOK_ADDR, hook) != (STUB_ADDR, False):
        fail(f"hook {hook.hex()} is not `bl 0x{STUB_ADDR:08X}`")
    calls = stub_calls(stub)
    if any(blx for _, _, blx in calls) or sorted({t for _, t, _ in calls}) != sorted(STUB_CALLS):
        fail("stub BL targets: " + ", ".join(f"0x{a:08X}->0x{t:08X}{' (BLX)' if x else ''}"
                                              for a, t, x in calls))
    if [t for _, t, _ in calls].count(SECTION_LOAD) != 1:
        fail("the stub must call Section_Load exactly once")
    if stub[STATE_ADDR - STUB_ADDR:] != b"\0\0\0\0":
        fail("ldr_state must start as 0")
    return [("hook", HOOK_ADDR, HOOK_STOCK, hook), ("stub", STUB_ADDR, b"\xff" * STUB_SIZE, stub)], \
        calls, listing


def check_container(frm, images):
    leaves = [(p, e, d) for p, e, d, nested in walk(frm) if not nested]
    names = sorted(e.name for _, e, _ in leaves)
    if names != sorted([APP, RES]):
        fail(f"container images {names}, expected only {RES} + {APP}")
    for name, typ in ((RES, 2), (APP, 1)):
        h = update_header(images[name])
        if h["type"] != typ:
            fail(f"{name}: update type {h['type']}, expected {typ}")
        if h["type"] in (0, 3):
            fail(f"{name}: BAK/IPL module present")
    if update_header(images[APP])["nUpdateCsum"] != APP_CSUM_STOCK or \
            update_header(images[RES])["nUpdateCsum"] != RES_CSUM_STOCK:
        fail("stock nUpdateCsum values differ from finding 15")
    # section table: sec 0 = {payload off 0, load 0xA0020400, size 0xA46C4}
    foff, load, size = struct.unpack_from(">3I", images[APP], 0x20)
    if (foff, load, size) != (0, 0xA0020400, 0xA46C4):
        fail(f"APP section 0 is {foff:#x}/{load:#x}/{size:#x}, expected 0/0xA0020400/0xA46C4")
    return load, load + size, leaves


def diff_ranges(a, b):
    out, i = [], 0
    while i < len(a):
        if a[i] != b[i]:
            j = i
            while j < len(a) and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stock", type=Path, default=STOCK_FRM)
    ap.add_argument("--out", type=Path, default=REPO_ROOT / "dist" / "rg2_v003_loader.frm")
    a = ap.parse_args(argv)

    print(">>> pre-flight safety gate")
    if not check_safety():
        fail("safety gate failed; resolve before building any .frm")

    frm = a.stock.read_bytes()
    if sha256(frm) != STOCK_FRM_SHA256:
        fail(f"{a.stock} is not the factory rg2_v003.frm")
    images = unpack(frm)
    sec0_lo, sec0_hi, _ = check_container(frm, images)
    print(f"[OK] stock {a.stock.name}: {RES} (RES) + {APP} (APP), no IPL/BAK; "
          f"section 0 0x{sec0_lo:08X}-0x{sec0_hi:08X}")

    patches, calls, listing = build_patches()
    app = bytearray(images[APP])
    for name, addr, stock, new in patches:
        if not (sec0_lo <= addr and addr + len(new) <= sec0_hi):
            fail(f"{name} 0x{addr:08X} is outside section 0")
        if len(new) != len(stock):
            fail(f"{name}: length {len(new)} != {len(stock)}")
        cur = bytes(app[off(addr):off(addr) + len(stock)])
        if cur != stock:
            fail(f"{name} @0x{addr:08X}: stock bytes {cur.hex()} != expected {stock.hex()}")
        app[off(addr):off(addr) + len(new)] = new
    if thumb_bl(HOOK_ADDR, HOOK_STOCK) != (SECTION_LOAD, False):
        fail("stock hook does not decode to bl Section_Load")
    print("[OK] stock bytes asserted; patches applied in place (same length)")

    out = repack(frm, {APP: bytes(app)})
    new_images = unpack(out)

    # ── verification of the output ──
    if new_images[RES] != images[RES]:
        fail("RES changed")
    if len(out) != len(frm) or out[:0x800] != frm[:0x800]:
        fail("container size or header changed")
    allowed = [(CSUM_OFF, CSUM_OFF + 4)] + [(off(ad), off(ad) + len(n)) for _, ad, _, n in patches]
    app_diff = diff_ranges(images[APP], new_images[APP])
    for lo, hi in app_diff:
        if not any(alo <= lo and hi <= ahi for alo, ahi in allowed):
            fail(f"unexpected APP change at 0x{lo:X}-0x{hi:X}")
    app_entry = next(e for p, e, d, n in walk(out) if not n and e.name == APP)
    nested = next(e for p, e, d, n in walk(out) if n)      # rg2_v001up container
    app_abs = nested.offset + app_entry.offset
    cont_diff = diff_ranges(frm, out)
    for lo, hi in cont_diff:
        if not any(app_abs + alo <= lo and hi <= app_abs + ahi for alo, ahi in allowed):
            fail(f"unexpected container change at 0x{lo:X}-0x{hi:X}")
    if repack(out, new_images) != out:
        fail("round trip not byte-identical")
    csum_new = nupdate_csum(new_images[APP])
    for name, img in new_images.items():
        h = update_header(img)
        if h["nUpdateCsum"] != nupdate_csum(img):
            fail(f"{name}: nUpdateCsum mismatch")
        if h["nUpdateFileSize"] != len(img) or h["nUpdateTotalSize"] != len(img) - IMAGE_HDR:
            fail(f"{name}: header sizes inconsistent")
    if update_header(new_images[APP])["nUpdateCsum"] != csum_new or csum_new == APP_CSUM_STOCK:
        fail("APP nUpdateCsum not recomputed")
    if len(parse_container(out)[1]) != 2:
        fail("container must keep 2 root entries")

    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(out)
    v = subprocess.run([sys.executable, str(TOOLS / "gr2frm.py"), "verify", str(a.out)],
                       capture_output=True, text=True)
    print(v.stdout.rstrip())
    if v.returncode:
        a.out.unlink()
        fail("gr2frm.py verify failed; output removed")

    # ── report ──
    rep = [
        "rg2_v003_loader.frm - M10 flashed loader (NOT FLASHED; read loader/flash/README.md first)",
        f"built by scripts/build_loader_frm.py from loader/flash/ldr_stub.S",
        "",
        f"stock   {a.stock.name}  {len(frm):,} B  sha256 {sha256(frm)}",
        f"output  {a.out.name}  {len(out):,} B  sha256 {sha256(out)}",
        "",
        f"{RES} (RES, type 2): byte-identical, nUpdateCsum 0x{RES_CSUM_STOCK:08X} unchanged",
        f"{APP} (APP, type 1): nUpdateCsum 0x{APP_CSUM_STOCK:08X} -> 0x{csum_new:08X} (recomputed by gr2frm.repack)",
        "no IPL (type 3) / BAK (type 0) module in the container",
        "",
        "APP changes (image offset = RAM - 0xA0020200; section 0 = 0xA0020400-0xA00C4AC3, loaded by the IPL):",
        f"  0x{CSUM_OFF:06X}  header      nUpdateCsum   4 B  {APP_CSUM_STOCK:08X} -> {csum_new:08X}",
    ]
    for name, addr, stock, new in patches:
        rep.append(f"  0x{off(addr):06X}  0x{addr:08X}  {name:12s} {len(new):3d} B")
        for k in range(0, len(new), 16):
            rep.append(f"      -{stock[k:k + 16].hex()}\n      +{new[k:k + 16].hex()}")
    rep += [
        f"  hook: bl 0x{SECTION_LOAD:08X} (Section_Load) -> bl 0x{STUB_ADDR:08X} (ldr_stub)",
        "  stub calls: " + ", ".join(f"0x{ad:08X} bl {STUB_CALLS[t]} 0x{t:08X}" for ad, t, _ in calls),
        f"  ldr_state 0x{STATE_ADDR:08X} = 0 in flash",
        "",
        "byte-level diff:",
        f"  {APP}: {sum(h - l for l, h in app_diff)} bytes differ in {len(app_diff)} runs, all inside the ranges above",
        f"  container: {sum(h - l for l, h in cont_diff)} bytes differ, all inside the {APP} payload "
        f"(container offset 0x{app_abs:X})",
        "",
        "gr2frm.py verify:",
        *("  " + line for line in v.stdout.rstrip().splitlines()),
        "",
        "stub listing (arm-none-eabi-objdump):",
        *("  " + line for line in listing.strip().splitlines()[2:]),
    ]
    report = a.out.with_suffix(".diff.txt")
    report.write_text("\n".join(rep) + "\n")
    print(f"[OK] wrote {a.out} ({len(out):,} B, sha256 {sha256(out)})")
    print(f"[OK] wrote {report}")
    print("Not flashed. Follow loader/flash/README.md (stock same-version reflash first).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
