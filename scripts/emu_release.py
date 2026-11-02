#!/usr/bin/env python3
"""Check dist/rg2_v003_cfw.frm (scripts/build_cfw_frm.py) offline; prints ALL PASSED or exits 1.

1. Byte checks, independent of the builder (own directory parser, plain checksum loop):
   RES = stock up to the old directory end (only the directory pointer and header
   +0x164..+0x16F differ), 0x00 up to the first CFW file, the new directory = the 121
   stock entries verbatim + one entry per scripts/rom_files.py file + a zero terminator
   ending at nUpdateTotalSize (<= 0x7E0000), every stock and CFW file byte-identical
   through it; header sizes (+0x168 = entry length = 0x200 + +0x164), bCheckRomSize 1,
   both nUpdateCsum values; APP differs only in the hook BL, the 132-byte stub range and
   nUpdateCsum; the nested container only in APP, the outer header only in its length
   words; `gr2frm.py verify` passes.
2. Stock code in Unicorn (ARM946, BE-32) on a simulated NAND (finding 29 §8.4): RES data
   blocks 0x62.. with its header in block 0xA1 sectors 0xFE/0xFF, APP data blocks 0x21..
   with its header in block 0x61. Only the I/O boundary is modelled (block -> sector,
   sector read/erase/program, container file stream, allocators, task id, logs, ctype);
   path handling, header checks, checksums, directory scans and the installer are the
   unmodified APP code.
   a. `up` on a stock camera: search 0xA004003A for every module type (H:/b01firm6 and 8
      served from the .frm) -> header check + full nUpdateCsum + ROM-size check pass;
      a flipped payload bit is refused; Destroy APP 0xA00402B0; install 0xA0040286(6),
      (8) -> the NAND holds the new RES and APP.
   b. Boot reads with the installed APP: stock LoadFile of every A: file (stock + CFW),
      FindFirst/FindNext of A:/cfw/fonts/ and A:/cfw/recipes/ (full paths, no ./..),
      fopen/fread of a recipe.
   c. The release stub as flashed, at the real hook site (like scripts/emu_loader.py), with
      the stock LoadFile reading the installed NAND: armed (shooting order; stage 1's own
      +8 hook entry is called), playback order, stage 1 disarming, skip key, file absent
      (stock RES), stage 1 without loader ABI, USB-charge boot (section 1 only). Any
      card/FAT access, a LoadFile other than A:/cfw/gr2_cfw.bin at section 3, a write
      into APP code or into the window outside LoadFile, or unbalanced registers fail.
   d. Stock reinstall from the CFW camera: the stock .frm passes search and installs;
      the NAND holds byte-identical stock RES and APP again, A:/cfw/* is gone, every stock
      A: file reads back (blocks past the stock end keep unreachable CFW bytes: reported).

Usage: python3 scripts/emu_release.py [--frm dist/rg2_v003_cfw.frm] [--stock fw/rg2_v003.frm] [--variant rom]
"""
import argparse
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE, UC_MODE_ARM,
                     UC_MODE_BIG_ENDIAN, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
                               UC_ARM_REG_R7, UC_ARM_REG_SP, UC_CPU_ARM_946)

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from emu_loader import EXPECTED_DIFF, PLAYBACK, SHOOTING, csum_plain, runs  # noqa: E402
from gr2frm import parse_container, unpack, walk  # noqa: E402
from rom_files import STAGE1, RomError, rom_files  # noqa: E402

STOCK_FRM = REPO_ROOT / "fw" / "rg2_v003.frm"
STOCK_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
RES, APP = "b01firm6", "b01firm8"
HDR = 0x200
SECT, BLOCK_SECTS = 0x200, 0x100
RES_DATA, RES_HDR = 0x62, 0xA1                 # NAND blocks (finding 29 §1)
APP_DATA, APP_HDR = 0x21, 0x61
RES_CAP = 0x7E0000
STOCK_DIR, STOCK_ENTRIES, STOCK_PAYLOAD = 0x785E00, 121, 0x787C80
M = 0xFFFFFFFF

APP_BASE = 0xA0020200
SEC0_CODE_END = 0xA00AFD74                     # section 0 code/vectors/veneers up to and incl. the stub
HOOK_SITE, HOOK_END = 0xA0022E58, 0xA0022E62
STATE = 0xA00AFD70
WINDOW, WINDOW_SIZE = 0xA0017000, 0x8000
LDR_MAGIC = 0x47524C31
ST = {0: "never ran", 1: "SKIP", 3: "NOFILE", 4: "NOABI", 5: "ARMED", 6: "disarmed by stage 1"}

# stock functions (b01firm8, fw 3.00; finding 29)
LOADFILE, FOPEN, FREAD = 0xA002CD2C, 0xA002C70E, 0xA002CA98
FINDFIRST, FINDNEXT = 0xA002EF58, 0xA002F354
UP_SEARCH, UP_DESTROY_APP, UP_INSTALL = 0xA004003A, 0xA00402B0, 0xA0040286
PORT_GET, SECTION_LOAD = 0xA00408D8, 0xA003FB02
CARD = {0xA002D350: "File_MountMedia", 0xA0065324: "FAT layer"}

RAM, RAM_SIZE = 0xA0000000, 0x01000000
RET, STACK = 0xB0000000, 0xB0100000
SCRATCH = 0xB0200000
BUF = 0xA0A00000                               # LoadFile destination for whole files (< 6 MiB)


class Fail(Exception):
    pass


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


# ── 1. byte checks ────────────────────────────────────────────────────────
def parse_dir(img):
    """A: directory like the camera's readers: [(64-byte entry, name, payload offset, size)]."""
    d, total = u32(img, HDR), u32(img, 0x164)
    out = []
    for i in range(1024):
        e = img[HDR + d + 64 * i:HDR + d + 64 * (i + 1)]
        if len(e) < 64 or d + 64 * (i + 1) > total:
            raise Fail("A: directory runs past nUpdateTotalSize")
        off, size = u32(e, 0x20), u32(e, 0x24)
        if size == 0:
            if e != bytes(64) or d + 64 * (i + 1) != total:
                raise Fail("A: directory terminator is not 64 zero bytes at the payload end")
            return out
        if b"\0" not in e[:32] or off + size > total:
            raise Fail(f"A: entry {i}: unterminated name or data past the payload")
        out.append((e, e[:32].split(b"\0")[0].decode("latin1"), off, size))
    raise Fail("A: directory has no terminator within 1024 entries")


def check_bytes(stock_frm, frm, files):
    si, ni = unpack(stock_frm), unpack(frm)
    if sorted(ni) != sorted([APP, RES]):
        raise Fail(f"images {sorted(ni)}")
    sr, nr, sa, na = si[RES], ni[RES], si[APP], ni[APP]
    total = len(nr) - HDR
    hdr_runs = runs(sr[:HDR], nr[:HDR])
    if any(lo < 0x164 or hi > 0x170 for lo, hi in hdr_runs):
        raise Fail("RES header differs outside +0x164..+0x16F")
    root_total, root = parse_container(frm)
    res_entry = next(e for e in root if e.name == RES)
    if (u32(nr, 0x164), u32(nr, 0x168), res_entry.length, u32(nr, 0x17C)) != (total, len(nr), len(nr), 1) \
            or total > RES_CAP:
        raise Fail(f"RES sizes: total 0x{u32(nr, 0x164):X} file 0x{u32(nr, 0x168):X} entry 0x{res_entry.length:X} "
                   f"image 0x{len(nr):X} bCheckRomSize {u32(nr, 0x17C)} (cap 0x{RES_CAP:X})")
    if nr[HDR + 4:HDR + STOCK_PAYLOAD] != sr[HDR + 4:] or u32(sr, HDR) != STOCK_DIR:
        raise Fail("RES stock bytes changed")
    stock_dir, new_dir = parse_dir(sr), parse_dir(nr)
    if len(stock_dir) != STOCK_ENTRIES or [e for e, *_ in new_dir[:STOCK_ENTRIES]] != [e for e, *_ in stock_dir]:
        raise Fail("new directory does not start with the 121 stock entries verbatim")
    cfw = new_dir[STOCK_ENTRIES:]
    if [n for _, n, _, _ in cfw] != list(files):
        raise Fail(f"CFW entries {[n for _, n, _, _ in cfw]} != rom_files {list(files)}")
    first = cfw[0][2]
    if first % SECT or first < STOCK_PAYLOAD or any(nr[HDR + STOCK_PAYLOAD:HDR + first]):
        raise Fail("first CFW file not 0x200-aligned after a 0x00 gap behind the stock payload")
    for e, name, off, size in new_dir:
        if name in files and (nr[HDR + off:HDR + off + size] != files[name] or e[0x34] != 0x01
                              or len(name) > 31 or off % SECT):
            raise Fail(f"{name}: data, attribute, name or alignment wrong")
    for (_, name, off, size), (_, _, soff, ssize) in zip(new_dir, stock_dir, strict=False):
        if nr[HDR + off:HDR + off + size] != sr[HDR + soff:HDR + soff + ssize]:
            raise Fail(f"{name}: does not read back as stock")
    for name, img in ((RES, nr), (APP, na)):
        if csum_plain(img) != u32(img, 0x16C):
            raise Fail(f"{name}: nUpdateCsum 0x{u32(img, 0x16C):08X} != plain sum")
    app_runs = runs(sa, na)
    if len(na) != len(sa) or any(not any(a <= lo and hi <= b for a, b in EXPECTED_DIFF) for lo, hi in app_runs) \
            or any(not any(a <= lo < b for lo, _ in app_runs) for a, b in EXPECTED_DIFF):
        raise Fail("APP differs outside (or not in all of) the hook, the stub and nUpdateCsum")
    if STAGE1.encode() + b"\0" not in na[EXPECTED_DIFF[2][0]:EXPECTED_DIFF[2][1]]:
        raise Fail(f"the stub does not carry {STAGE1}")
    (_, sn), (ne, nn) = [next((e, raw) for _, e, raw, nested in walk(f) if nested) for f in (stock_frm, frm)]
    ae = next(e for _, e, _, nested in walk(nn) if not nested)
    if len(sn) != len(nn) or any(lo < ae.offset or hi > ae.offset + ae.length for lo, hi in runs(sn, nn)):
        raise Fail("nested container differs outside the APP image")
    end = 0x800 + res_entry.length
    pad = frm[end:ne.offset]
    if res_entry.sector != 4 or ne.offset != -(-end // SECT) * SECT or set(pad) - {0xFF} or \
            root_total != len(frm) or ne.offset + ne.length != len(frm):
        raise Fail("outer container layout: RES at sector 4, APP container at the next sector, 0xFF padding")
    if any(not (0x20 <= lo and hi <= 0x24 or 0x44 <= lo and hi <= 0x48 or 0x80 <= lo and hi <= 0x84)
           for lo, hi in runs(stock_frm[:0x800], frm[:0x800])):
        raise Fail("outer header differs outside the total length, the RES length and the APP-container sector")
    print(f"[OK] bytes: RES stock up to 0x{STOCK_PAYLOAD:X} (pointer 0x{STOCK_DIR:X} -> 0x{u32(nr, HDR):X}), "
          f"{len(cfw)} CFW files from 0x{first:X}, directory {len(new_dir)} entries + terminator ending at "
          f"0x{total:X} (cap 0x{RES_CAP:X}); header sizes, bCheckRomSize 1, both nUpdateCsum = plain sum")
    print(f"[OK] bytes: APP differs only in {', '.join(f'0x{lo:X}-0x{hi - 1:X}' for lo, hi in app_runs)}; "
          "nested container only in APP; outer header only in its length words")
    return si, ni, new_dir


# ── 2. stock code on a simulated NAND ────────────────────────────────────
class Nand:
    def __init__(self):
        self.s = {}

    def put(self, img, data_block, hdr_block):
        for i in range(0, len(img) - HDR, SECT):
            self.s[(data_block << 8) + i // SECT] = img[HDR + i:HDR + i + SECT].ljust(SECT, b"\xff")
        self.s[(hdr_block << 8) + 0xFE] = img[:HDR]

    def module(self, data_block, hdr_block):
        """Header + payload of the module installed in these blocks (nUpdateTotalSize bytes)."""
        hdr = self.s.get((hdr_block << 8) + 0xFE, b"\xff" * SECT)
        total = u32(hdr, 0x164)
        n = -(-total // SECT)
        return hdr + b"".join(self.s.get((data_block << 8) + i, b"\xff" * SECT) for i in range(n))[:total]

    def blocks(self, lo, hi):
        return sorted({k >> 8 for k in self.s if lo <= k >> 8 < hi})


class Stock:
    """APP code (sections 0-14 at their load addresses, bss zero) + I/O models."""

    def __init__(self, app, nand, files=None):
        self.nand, self.files = nand, files or {}
        self.pos, self.erases, self.programs, self.reads, self.opens = {}, [], [], [], []
        self.heap, self.err = 0xA0900000, None
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_ARM_946)
        uc.mem_map(RAM, RAM_SIZE)
        sec15 = u32(app, 0x20 + 15 * 20)                       # section 15 goes to TCM, not here
        uc.mem_write(APP_BASE, app[:HDR + sec15])
        uc.mem_write(WINDOW, b"\xff" * WINDOW_SIZE)            # IPL copy's erased tail
        uc.mem_map(RET, 0x1000)
        uc.mem_write(RET, b"\x46\xc0" * 0x800)                 # Thumb nops: return landing
        uc.mem_map(STACK - 0x40000, 0x40000)
        uc.mem_map(SCRATCH, 0x10000)
        uc.mem_write(SCRATCH + 0x100, bytes(0x98 if 0x20 <= c < 0x7F else 0 for c in range(256)))
        uc.mem_write(SCRATCH, struct.pack(">I", SCRATCH + 0x100))
        self.models = {
            0xA005FD06: self.m_block_sector, 0xA00441F6: self.m_read, 0xA0044054: self.m_erase,
            0xA0044102: self.m_program, 0xA0044134: self.m_zero, 0xA0044438: self.m_zero,
            0xA0043F18: self.m_map, 0xA00550A4: self.m_open, 0xA0055252: self.m_fread,
            0xA005527E: self.m_stat, 0xA0055294: self.m_zero, 0xA0024D6E: self.m_malloc,
            0xA0024E0E: self.m_zero, 0xA0024C3C: self.m_pool_alloc, 0xA0024D76: self.m_zero,
            0xA0027360: self.m_zero, 0xA00273A4: self.m_zero,      # semaphore wait/signal (kernel)
            0xA00267BA: self.m_tid, 0xA0039DE4: self.m_zero, 0xA0039CBC: self.m_zero, 0xA009EA54: self.m_ctype,
        }
        for name_addr in CARD:
            self.models[name_addr] = self.m_card
        for a in self.models:
            uc.hook_add(UC_HOOK_CODE, self._model, begin=a, end=a)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._invalid)

    # plumbing
    def r(self, n):
        return self.uc.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[n])

    def ret(self, v):
        self.uc.reg_write(UC_ARM_REG_R0, v & M)
        self.uc.reg_write(UC_ARM_REG_PC, self.uc.reg_read(UC_ARM_REG_LR))

    def fail(self, msg):
        self.err = self.err or Fail(msg)
        self.uc.emu_stop()

    def _model(self, uc, addr, size, _):
        try:
            self.models[addr]()
        except Fail as ex:
            self.fail(str(ex))

    def _invalid(self, uc, access, addr, size, value, _):
        self.fail(f"invalid access 0x{addr:08X} at pc 0x{uc.reg_read(UC_ARM_REG_PC):08X}")
        return False

    def cstr(self, a, n=64):
        return bytes(self.uc.mem_read(a, n)).split(b"\0")[0].decode("latin1")

    def arg(self, s):
        self.uc.mem_write(SCRATCH + 0x800, s.encode() + b"\0")
        return SCRATCH + 0x800

    def call(self, fn, *args, timeout_s=120):
        uc = self.uc
        for i, v in enumerate(args):
            uc.reg_write((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i], v & M)
        uc.reg_write(UC_ARM_REG_SP, STACK - 0x1000)            # >= 0x200 B slack: FindFirst copies 0x200 B
        uc.reg_write(UC_ARM_REG_LR, RET | 1)
        try:
            uc.emu_start(fn | 1, RET, timeout=timeout_s * 1_000_000)   # no count: it disables the fast path
        except UcError as ex:
            raise self.err or Fail(f"0x{fn:08X}: {ex} at 0x{uc.reg_read(UC_ARM_REG_PC):08X}") from ex
        if self.err:
            raise self.err
        if uc.reg_read(UC_ARM_REG_PC) != RET:
            raise Fail(f"0x{fn:08X} did not return (pc 0x{uc.reg_read(UC_ARM_REG_PC):08X})")
        return s32(uc.reg_read(UC_ARM_REG_R0))

    # I/O models (finding 29 §8.4)
    def m_block_sector(self):                                  # 0xA005FD06(block, &sector)
        self.uc.mem_write(self.r(1), struct.pack(">I", (self.r(0) << 8) & M))
        self.ret(0)

    def m_read(self):                                          # 0xA00441F6(sector, n, dst, spare)
        sec, n, dst, spare = (self.r(i) for i in range(4))
        self.reads.append((sec, n))
        if dst != M:
            self.uc.mem_write(dst, b"".join(self.nand.s.get(sec + i, b"\xff" * SECT) for i in range(n)))
        if spare:
            self.uc.mem_write(spare, b"\xff" * (3 * n))
        self.ret(0)

    def m_erase(self):                                         # 0xA0044054(block)
        blk = self.r(0)
        self.erases.append(blk)
        for s in range(BLOCK_SECTS):
            self.nand.s.pop((blk << 8) + s, None)
        self.ret(0)

    def m_program(self):                                       # 0xA0044102(sector, n, src, spare)
        sec, n, src = self.r(0), self.r(1), self.r(2)
        self.programs.append((sec, n))
        if src != M:
            data = bytes(self.uc.mem_read(src, n * SECT))
            for i in range(n):
                if sec + i in self.nand.s:
                    raise Fail(f"program of sector 0x{sec + i:X} without an erase")
                self.nand.s[sec + i] = data[i * SECT:(i + 1) * SECT]
        self.ret(0)

    def m_map(self):                                           # 0xA0043F18: logical = physical
        self.uc.mem_write(self.r(1), struct.pack(">I", self.r(0)))
        self.ret(0)

    def m_open(self):                                          # container stream open(path, ...)
        path = self.cstr(self.r(0))
        self.opens.append(path)
        d = int(path[-1]) if path.startswith("H:/b01firm") and path[-1].isdigit() else -1
        if d in self.files:
            self.pos[0x100 + d] = 0
            return self.ret(0x100 + d)
        self.ret(0)

    def m_fread(self):
        h, buf, n = self.r(0), self.r(1), self.r(2)
        img, p = self.files[h - 0x100], self.pos[h]
        chunk = img[p:p + n]
        self.uc.mem_write(buf, chunk)
        self.pos[h] = p + len(chunk)
        self.ret(len(chunk))

    def m_stat(self):                                          # size at +0x214 / +0x21C
        size = len(self.files[self.r(0) - 0x100])
        self.uc.mem_write(self.r(1) + 0x210, struct.pack(">IIII", 0, size, 0, size))
        self.ret(0)

    def m_malloc(self):                                        # Memory_Alloc(size)
        p = self.heap
        self.heap += (self.r(0) + 0x1F) & ~0xF
        self.ret(p)

    def m_pool_alloc(self):                                    # pool alloc(pool, size)
        p = self.heap
        self.heap += (self.r(1) + 0x1F) & ~0xF
        self.ret(p)

    def m_zero(self):
        self.ret(0)

    def m_tid(self):
        self.ret(0x1234)

    def m_ctype(self):
        self.ret(SCRATCH)

    def m_card(self):
        raise Fail(f"card access: {CARD[self.uc.reg_read(UC_ARM_REG_PC) & ~1]}")

    # stock file API
    def load(self, path, size=0x600000, dst=BUF):
        n = self.call(LOADFILE, self.arg(path), dst, size)
        return n, bytes(self.uc.mem_read(dst, n)) if n > 0 else b""

    def find(self, prefix):
        out, rec = [], SCRATCH + 0x1000
        h = self.call(FINDFIRST, self.arg(prefix), rec)
        while h:
            out.append(self.cstr(rec))
            if self.call(FINDNEXT, h, rec) != 1:
                break
        return out


def up(nand, running_app, res, app, label):
    """The stock `up` sequence (finding 29 §5) for a RES + APP pair served as H:/b01firm6/8."""
    st = Stock(running_app, nand, {6: res, 8: app})
    found = {t: st.call(UP_SEARCH, t, 1) for t in (3, 0, 2, 1)}
    if found != {3: -1, 0: -1, 2: 6, 1: 8}:
        raise Fail(f"{label}: up search per type {found}, expected only RES (6) and APP (8)")
    bad = bytearray(res)
    bad[HDR + 0x100] ^= 1
    copy = Nand()
    copy.s = dict(nand.s)
    if Stock(running_app, copy, {6: bytes(bad)}).call(UP_SEARCH, 2, 1) != -1:
        raise Fail(f"{label}: a RES with one flipped payload bit (stale nUpdateCsum) was accepted")
    st.call(UP_DESTROY_APP)
    if st.erases != [APP_HDR]:
        raise Fail(f"{label}: Destroy APP erased {st.erases}")
    for idx in (6, 8):
        st.erases, st.programs = [], []
        if st.call(UP_INSTALL, idx) != 0:
            raise Fail(f"{label}: install b01firm{idx} failed")
        data, hdr = (RES_DATA, RES_HDR) if idx == 6 else (APP_DATA, APP_HDR)
        img = res if idx == 6 else app
        nblk = -(-(len(img) - HDR) // (SECT * BLOCK_SECTS))
        if st.erases != [hdr] + list(range(data, data + nblk)):
            raise Fail(f"{label}: b01firm{idx} erased {[hex(b) for b in st.erases]}")
        if nand.module(data, hdr) != img:
            raise Fail(f"{label}: NAND does not hold b01firm{idx} after the install")
    print(f"[OK] {label}: up search -> RES 6 + APP 8 (header, nUpdateCsum, ROM size), flipped bit refused; "
          f"Destroy APP; install RES (blocks 0x{RES_DATA:X}-0x{RES_DATA + nblk_of(res) - 1:X} + header 0x{RES_HDR:X}) "
          f"and APP; NAND holds both images")


def nblk_of(img):
    return -(-(len(img) - HDR) // (SECT * BLOCK_SECTS))


def check_reads(nand, files, stock_res, label, cfw=True):
    app = nand.module(APP_DATA, APP_HDR)
    st = Stock(app, nand)
    ok = 0
    for _, name, off, size in parse_dir(stock_res):
        n, data = st.load(name)
        if n != size or data != stock_res[HDR + off:HDR + off + size]:
            raise Fail(f"{label}: stock LoadFile({name}) = {n}, data {'ok' if n == size else 'short'}")
        ok += 1
    for name, data in files.items():
        n, got = st.load(name)
        if cfw and (n, got) != (len(data), data):
            raise Fail(f"{label}: stock LoadFile({name}) = {n}, not the file")
        if not cfw and n != -1:
            raise Fail(f"{label}: LoadFile({name}) = {n} after the stock reinstall")
    if not cfw:
        print(f"[OK] {label}: stock LoadFile of all {ok} stock A: files byte-identical; "
              f"A:/cfw/* -> -1 ({len(files)} files)")
        return st
    for prefix in ("A:/cfw/fonts/", "A:/cfw/recipes/"):
        want = [n for n in files if n.startswith(prefix)]
        got = st.find(prefix)
        if got != want:
            raise Fail(f"{label}: FindFirst/Next({prefix}) = {got}, expected {want}")
    recipe = next(n for n in files if n.startswith("A:/cfw/recipes/"))
    fh, chunks = st.call(FOPEN, st.arg(recipe), 0), []
    while fh:
        n = st.call(FREAD, fh, BUF, 1000)
        if n <= 0:
            break
        chunks.append(bytes(st.uc.mem_read(BUF, n)))
    if b"".join(chunks) != files[recipe]:
        raise Fail(f"{label}: fopen/fread({recipe}) chunks {[len(c) for c in chunks]} do not match")
    print(f"[OK] {label}: stock LoadFile of all {ok} stock + {len(files)} CFW A: files byte-identical; "
          f"FindFirst/Next lists A:/cfw/fonts/ and A:/cfw/recipes/ as full paths; fopen/fread({recipe}) "
          f"in {len(chunks)} chunks")
    return st


# ── 2c. the release stub at the hook site ────────────────────────────────
class StubBoot(Stock):
    def __init__(self, app, nand, *, skip=False, order=SHOOTING, disarm_at=None, hook=None):
        super().__init__(app, nand)
        self.skip, self.order, self.disarm_at = skip, order, disarm_at
        self.events, self.loaded, self.file_len, self.cur = [], set(), 0, None
        self.app_code = bytes(self.uc.mem_read(APP_BASE, SEC0_CODE_END - APP_BASE))
        self.models[PORT_GET] = self.m_port_get
        self.models[SECTION_LOAD] = self.m_section_load
        self.models[LOADFILE] = self.m_loadfile_entry
        self.hook = hook
        for a in (PORT_GET, SECTION_LOAD, LOADFILE) + ((hook & ~1,) if hook else ()):
            self.uc.hook_add(UC_HOOK_CODE, self._model, begin=a & ~1, end=a & ~1)
        if hook:
            self.models[hook & ~1] = self.m_stage1_hook
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._write)

    def _write(self, uc, access, addr, size, value, _):
        if APP_BASE <= addr < SEC0_CODE_END and not (STATE <= addr < STATE + 4):
            self.fail(f"write into APP code 0x{addr:08X} at pc 0x{uc.reg_read(UC_ARM_REG_PC):08X}")
        if WINDOW <= addr < WINDOW + WINDOW_SIZE and not self.in_loadfile:
            self.fail(f"window write 0x{addr:08X} outside LoadFile")

    in_loadfile = False

    def m_port_get(self):
        pin = self.r(0)
        self.events.append(("Port_Get", pin))
        if pin != 0x27:
            raise Fail(f"Port_Get pin 0x{pin:X}, expected RL1 0x27")
        self.ret(0 if self.skip else 1)

    def m_section_load(self):
        n, arg = self.r(0), self.r(1)
        self.events.append(("Section_Load", n))
        if (n, arg) != (self.cur, 0):
            raise Fail(f"Section_Load({n}, {arg}) during section {self.cur}")
        self.loaded.add(n)
        self.ret(0x5EC00000 | n)

    def m_loadfile_entry(self):                                # record, then run the stock LoadFile
        path, dst, size = self.cstr(self.r(0)), self.r(1), self.r(2)
        self.events.append(("LoadFile", path))
        if (path, dst, size) != (STAGE1, WINDOW, WINDOW_SIZE) or self.cur != 3 or 3 not in self.loaded:
            raise Fail(f"LoadFile({path!r}, 0x{dst:08X}, 0x{size:X}) during section {self.cur}")
        self.in_loadfile = True                                # window writes allowed until the section ends

    def m_stage1_hook(self):
        n, state = self.r(0), self.r(1)
        self.events.append(("stage1_hook", n))
        if state != STATE or u32(self.uc.mem_read(STATE, 4), 0) != 5:
            raise Fail(f"stage1_hook r1=0x{state:08X}")
        if [e for e in self.events if e[0] == "Section_Load"][-1] != ("Section_Load", n) or \
                ("stage1_hook", n) in self.events[:-1]:
            raise Fail(f"stage1_hook({n}) not once, after Section_Load({n})")
        if n == self.disarm_at:
            self.uc.mem_write(STATE, struct.pack(">I", 6))
        self.ret(0)

    def section(self, n):
        self.cur, uc = n, self.uc
        uc.reg_write(UC_ARM_REG_CPSR, (uc.reg_read(UC_ARM_REG_CPSR) & ~0x1F) | 0x10)   # USR: the boot task
        regs = {UC_ARM_REG_R4: 0x44444444, UC_ARM_REG_R5: n, UC_ARM_REG_R6: 0x66666666,
                UC_ARM_REG_R7: 0xA00C22E4 + n * 0x18, UC_ARM_REG_SP: STACK - 0x1000}
        for reg, v in regs.items():
            uc.reg_write(reg, v)
        uc.reg_write(UC_ARM_REG_LR, 0xDEAD0001)
        try:
            uc.emu_start(HOOK_SITE | 1, HOOK_END, timeout=60_000_000)
        except UcError as ex:
            raise self.err or Fail(f"section {n}: {ex} at 0x{uc.reg_read(UC_ARM_REG_PC):08X}") from ex
        self.in_loadfile = False
        if self.err:
            raise self.err
        if uc.reg_read(UC_ARM_REG_PC) != HOOK_END:
            raise Fail(f"section {n}: did not return to the hook site")
        if any(uc.reg_read(r) != v for r, v in regs.items()) or uc.reg_read(UC_ARM_REG_R0) != regs[UC_ARM_REG_R7]:
            raise Fail(f"section {n}: r4-r7/SP not preserved or the caller's movs r0,r7 did not run")

    def run(self):
        for n in self.order:
            self.section(n)
        if bytes(self.uc.mem_read(APP_BASE, SEC0_CODE_END - APP_BASE))[:STATE - APP_BASE] != \
                self.app_code[:STATE - APP_BASE]:
            raise Fail("APP code changed")
        return self


def stub_cases(nand_cfw, app, stock_res, files):
    stage1 = files[STAGE1]
    hook = u32(stage1, 8)
    no_abi = stage1[:4] + b"\0\0\0\0" + stage1[8:]
    nand_stock_res = Nand()
    nand_stock_res.put(stock_res, RES_DATA, RES_HDR)
    nand_stock_res.put(app, APP_DATA, APP_HDR)
    nand_no_abi = Nand()
    from build_cfw_frm import build_res                         # same layout, stage 1 without the magic
    nand_no_abi.put(build_res(stock_res, {**files, STAGE1: no_abi})[0], RES_DATA, RES_HDR)
    nand_no_abi.put(app, APP_DATA, APP_HDR)
    ff = b"\xff" * WINDOW_SIZE
    loaded = lambda f: f + ff[len(f):]                          # noqa: E731
    cases = [
        (f"A  armed: A:/cfw/gr2_cfw.bin at section 3, stage 1's +8 hook 0x{hook:08X} after each later load",
         nand_cfw, dict(hook=hook), 5, 1, SHOOTING[3:], loaded(stage1)),
        ("F  playback boot order", nand_cfw, dict(hook=hook, order=PLAYBACK), 5, 1, PLAYBACK[2:], loaded(stage1)),
        ("G  stage 1 disarms at section 5 (self-check failed)", nand_cfw, dict(hook=hook, disarm_at=5), 6, 1,
         [3, 5], loaded(stage1)),
        ("C  shutter half-press held -> stock, A: not read", nand_cfw, dict(hook=hook, skip=True), 1, 0, [], ff),
        ("B  A: without the CFW files (stock RES) -> stock", nand_stock_res, dict(hook=hook), 3, 1, [], ff),
        ("E  stage 1 without loader ABI -> loaded, never called", nand_no_abi, dict(hook=hook), 4, 1, [],
         loaded(no_abi)),
        ("I  USB-charge boot (section 1 only) -> undecided, nothing read", nand_cfw, dict(hook=hook, order=[1]), 0,
         0, [], ff),
    ]
    ok = True
    for name, nand, kw, status, nload, hooks, window in cases:
        try:
            emu = StubBoot(app, nand, **kw).run()
        except Fail as ex:
            print(f"[FAIL] stub {name}: {ex}")
            ok = False
            continue
        ev = emu.events
        state = u32(emu.uc.mem_read(STATE, 4), 0)
        got_hooks = [e[1] for e in ev if e[0] == "stage1_hook"]
        n_load = sum(e[0] == "LoadFile" for e in ev)
        n_port = sum(e[0] == "Port_Get" for e in ev)
        win = bytes(emu.uc.mem_read(WINDOW, WINDOW_SIZE))
        loads = [e[1] for e in ev if e[0] == "Section_Load"]
        good = (state == status and got_hooks == hooks and n_load == nload and win == window
                and loads == emu.order and n_port == (0 if emu.order == [1] else 1))
        print(f"[{'OK' if good else 'FAIL'}] stub {name}")
        print(f"       ldr_state={state} ({ST.get(state)}); Port_Get x{n_port}, LoadFile x{n_load} "
              f"({len(emu.reads)} NAND reads, {sum(n for _, n in emu.reads)} sectors); hooks {got_hooks or 'none'}; "
              f"window {'0xFF' if win == ff else 'holds the A: file'}")
        ok &= good
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--frm", type=Path, default=REPO_ROOT / "dist" / "rg2_v003_cfw.frm")
    ap.add_argument("--stock", type=Path, default=STOCK_FRM)
    ap.add_argument("--variant", default="rom", help="bin/<variant>/ the .frm was built from")
    a = ap.parse_args(argv)
    if not a.frm.exists():
        print(f"[FAIL] {a.frm} missing: run scripts/build_cfw_frm.py first")
        return 1
    stock_frm, frm = a.stock.read_bytes(), a.frm.read_bytes()
    if hashlib.sha256(stock_frm).hexdigest() != STOCK_SHA256:
        print(f"[FAIL] {a.stock} is not the factory rg2_v003.frm")
        return 1
    ok = True
    try:
        files = rom_files(REPO_ROOT, a.variant)
        si, ni, _ = check_bytes(stock_frm, frm, files)
        v = subprocess.run([sys.executable, str(TOOLS / "gr2frm.py"), "verify", str(a.frm)],
                           capture_output=True, text=True, check=False)
        if v.returncode:
            raise Fail("gr2frm.py verify failed:\n" + v.stdout)
        print("[OK] gr2frm.py verify exit 0")

        nand = Nand()                                           # a camera on stock 3.00
        nand.put(si[RES], RES_DATA, RES_HDR)
        nand.put(si[APP], APP_DATA, APP_HDR)
        seed = dict(nand.s)
        up(nand, si[APP], ni[RES], ni[APP], "CFW install on stock")
        check_reads(nand, files, si[RES], "boot reads after the install")
        ok &= stub_cases(nand, ni[APP], si[RES], files)
        cfw_end = RES_DATA + nblk_of(ni[RES])
        up(nand, ni[APP], si[RES], si[APP], "stock reinstall on the CFW camera")
        check_reads(nand, files, si[RES], "boot reads after the stock reinstall", cfw=False)
        stale = sorted({k >> 8 for k in nand.s if nand.s[k] != seed.get(k)})
        expect = list(range(RES_DATA + nblk_of(si[RES]), cfw_end))
        if stale != expect or any(k not in nand.s for k in seed):
            raise Fail(f"NAND after the stock reinstall differs from stock in blocks {[hex(b) for b in stale]}")
        print(f"[OK] stock reinstall: NAND byte-identical to stock except block(s) "
              f"{', '.join(f'0x{b:X}' for b in stale) or 'none'} past the stock RES end, which keep unreachable "
              "CFW bytes (never erased by up, finding 29 §4)")
    except (Fail, RomError) as ex:
        print(f"[FAIL] {ex}")
        ok = False
    print("ALL PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
