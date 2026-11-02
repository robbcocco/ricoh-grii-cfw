#!/usr/bin/env python3
"""Check dist/rg2_v003_loader.frm (scripts/build_loader_frm.py) offline.

1. Byte check, independent of the builder: the patched APP differs from stock only
   in nUpdateCsum (0x16C), the hook BL (0xA0022E5C) and the 132-byte stub range
   (0xA00AFCF0); RES and every other container byte are identical; nUpdateCsum is
   recomputed here with the plain formula of finding 15 §7.1; `gr2frm.py verify`
   passes.
2. Unicorn (ARM946, BE-32): the patched APP image is mapped at 0xA0020200 and the
   real hook site 0xA0022E58..0xA0022E62 (`movs r1,#0; movs r0,r5; bl <patched>;
   movs r0,r7`) runs once per section load, in the root init task's order, so the
   stub executes exactly as flashed and the caller's own `movs r0,r7` shows the
   stub's result is dead. Port_Get, File_MountMedia, LoadFile, the stock section
   loader and the stage-1 hook are Python models; any other execution, any write
   outside the stack and ldr_state, an unbalanced register/stack state, or a card
   access before Section_Load(3) (lens start, finding 33) fails the run.

Scenarios: CFW with loader ABI (model) / file absent / skip key held / no card /
stage 1 without loader ABI (pre-M10b) / playback boot order / stage 1 disarming the
stub / USB-charge boot (section 1 only) / the current bin/release/gr2_cfw.bin (its
+8 hook entry is called).

Usage: python3 scripts/emu_loader.py [--frm dist/rg2_v003_loader.frm] [--stock fw/rg2_v003.frm]
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE, UC_MODE_ARM,
                     UC_MODE_BIG_ENDIAN, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R4,
                               UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_SP, UC_CPU_ARM_946)

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS))
from gr2frm import unpack, update_header, walk  # noqa: E402

STOCK_FRM = REPO_ROOT / "fw" / "rg2_v003.frm"
APP_BASE = 0xA0020200
HOOK_SITE, HOOK_END = 0xA0022E58, 0xA0022E62          # movs r1,#0; movs r0,r5; bl <patched>; movs r0,r7
STUB, STUB_END, STATE = 0xA00AFCF0, 0xA00AFD74, 0xA00AFD70
WINDOW, WINDOW_SIZE = 0xA0017000, 0x8000
STAGE1_HOOK = WINDOW + 0x100                          # entry in the model stage 1 (word +8)
LDR_MAGIC = 0x47524C31
CFW_PATH = "H:/cfw/gr2_cfw.bin"
FN = {0xA00408D8: "Port_Get", 0xA002D350: "File_MountMedia", 0xA002CD2C: "LoadFile",
      0xA003FB02: "Section_Load", STAGE1_HOOK: "stage1_hook"}
EXPECTED_DIFF = [(0x16C, 0x170), (0x2C5C, 0x2C60), (0x8FAF0, 0x8FB74)]   # APP image offsets
RAM_BASE, RAM_SIZE = 0xA0000000, 0x01000000
STACK_BASE, STACK_SIZE = 0xB0000000, 0x10000
STACK_TOP = STACK_BASE + STACK_SIZE - 0x100
SHOOTING = [1, 4, 2, 3, 5, 10, 11, 8, 9, 6, 12, 13]   # root init task 0xA0020944, shooting mode
PLAYBACK = [1, 2, 3, 8, 9, 10, 11, 6, 4, 5, 12, 13]   # notify(1) == 1 branch
ST = {0: "never ran", 1: "SKIP", 2: "NOMOUNT", 3: "NOFILE", 4: "NOABI", 5: "ARMED", 6: "disarmed by stage 1"}
M = 0xFFFFFFFF


class Fail(Exception):
    pass


# ── 1. byte-level check ───────────────────────────────────────────────────
def csum_plain(img):
    load, total = struct.unpack_from(">I", img, 0x20)[0], struct.unpack_from(">I", img, 0x164)[0]
    return sum(b * (((load + i) % 6) + 2) for i, b in enumerate(img[0x200:0x200 + total])) & M


def runs(a, b):
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


def check_bytes(stock_frm, frm_path):
    frm = frm_path.read_bytes()
    stock = stock_frm.read_bytes()
    si, pi = unpack(stock), unpack(frm)
    if set(si) != set(pi) or set(pi) != {"b01firm6", "b01firm8"}:
        raise Fail(f"images {sorted(pi)}")
    if pi["b01firm6"] != si["b01firm6"]:
        raise Fail("RES b01firm6 differs from stock")
    app_runs = runs(si["b01firm8"], pi["b01firm8"])
    for lo, hi in app_runs:
        if not any(a <= lo and hi <= b for a, b in EXPECTED_DIFF):
            raise Fail(f"APP differs outside the documented ranges at 0x{lo:X}-0x{hi:X}")
    for a, b in EXPECTED_DIFF:
        if not any(a <= lo < b for lo, _ in app_runs):
            raise Fail(f"documented range 0x{a:X}-0x{b - 1:X} is not patched")
    nested = next(e for _, e, _, n in walk(frm) if n)                     # rg2_v001up
    app_abs = nested.offset + next(e for _, e, _, n in walk(frm) if not n and e.name == "b01firm8").offset
    cont_runs = runs(stock, frm)
    ndiff = sum(h - l for l, h in cont_runs)
    if cont_runs != [(app_abs + lo, app_abs + hi) for lo, hi in app_runs]:
        raise Fail("container differs in bytes that are not APP patch bytes")
    calc, hdr = csum_plain(pi["b01firm8"]), update_header(pi["b01firm8"])["nUpdateCsum"]
    if calc != hdr:
        raise Fail(f"APP nUpdateCsum 0x{hdr:08X} != plain sum 0x{calc:08X}")
    v = subprocess.run([sys.executable, str(TOOLS / "gr2frm.py"), "verify", str(frm_path)],
                       capture_output=True, text=True, check=False)
    if v.returncode:
        raise Fail("gr2frm.py verify failed:\n" + v.stdout)
    print(f"[OK] bytes: RES identical; APP differs in {len(app_runs)} runs "
          + ", ".join(f"0x{lo:X}-0x{hi - 1:X}" for lo, hi in app_runs)
          + f" (all documented); container: {ndiff} bytes, all in the APP patch bytes")
    print(f"[OK] nUpdateCsum 0x{hdr:08X} = plain sum (finding 15 §7.1); gr2frm.py verify exit 0")
    return pi["b01firm8"]


# ── 2. emulation ──────────────────────────────────────────────────────────
class Emu:
    def __init__(self, app, *, card=True, skip=False, file=None, order=SHOOTING, disarm_at=None, hook=STAGE1_HOOK):
        self.card, self.skip, self.file, self.order, self.disarm_at = card, skip, file, order, disarm_at
        self.events, self.err, self.loaded = [], None, set()
        self.fn = {a: n for a, n in FN.items() if n != "stage1_hook"}
        self.fn[hook & ~1] = "stage1_hook"                     # the model stands in for stage 1's hook code
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_ARM_946)
        uc.mem_map(RAM_BASE, RAM_SIZE)
        uc.mem_map(STACK_BASE, STACK_SIZE)
        uc.mem_write(WINDOW, b"\xff" * WINDOW_SIZE)            # IPL copy's erased tail
        uc.mem_write(APP_BASE, app)
        uc.hook_add(UC_HOOK_BLOCK, self._block)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._write)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._invalid)

    def r(self, n):
        return self.uc.reg_read(UC_ARM_REG_R0 + n)

    def u32(self, a):
        return struct.unpack(">I", self.uc.mem_read(a, 4))[0]

    def cstr(self, a):
        return bytes(self.uc.mem_read(a, 64)).split(b"\0")[0].decode("latin1")

    def _fail(self, msg):
        self.err = self.err or Fail(msg)
        self.uc.emu_stop()

    def _block(self, uc, addr, size, _):
        name = self.fn.get(addr)
        if name:
            try:
                ret = getattr(self, "_" + name)()
            except Fail as ex:
                return self._fail(str(ex))
            uc.reg_write(UC_ARM_REG_R0, ret & M)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
            return
        if not (HOOK_SITE <= addr < HOOK_END or STUB <= addr < STATE):
            self._fail(f"unexpected execution at 0x{addr:08X}")

    def _write(self, uc, access, addr, size, value, _):
        if not (STACK_BASE <= addr < STACK_BASE + STACK_SIZE or (addr == STATE and size == 4)):
            self._fail(f"write to 0x{addr:08X} (size {size}) at pc 0x{uc.reg_read(UC_ARM_REG_PC):08X}")

    def _invalid(self, uc, access, addr, size, value, _):
        self._fail(f"invalid access 0x{addr:08X} at pc 0x{uc.reg_read(UC_ARM_REG_PC):08X}")
        return False

    # firmware models
    def _Port_Get(self):
        pin = self.r(0)
        self.events.append(("Port_Get", pin))
        if pin != 0x27:
            raise Fail(f"Port_Get pin 0x{pin:X}, expected RL1 0x27")
        return 0 if self.skip else 1                            # active low

    def _card_access(self, name):
        # Every SD sector read calls FUN_a00f9888 in section 1 (finding 28), and a card session
        # before JobSystem step 4 (between sections 4 and 2) delays the lens start and makes the
        # lens bounce (F4, finding 33): the card waits for Section_Load(3).
        if not {1, 3} <= self.loaded:
            raise Fail(f"{name} before Section_Load(3): it must follow the lens start (finding 33)")

    def _File_MountMedia(self):
        path = self.cstr(self.r(0))
        self.events.append(("File_MountMedia", path))
        self._card_access("File_MountMedia")
        if path != CFW_PATH:
            raise Fail(f"mount path {path!r}")
        return 0 if self.card else -1

    def _LoadFile(self):
        path, dst, size = self.cstr(self.r(0)), self.r(1), self.r(2)
        self.events.append(("LoadFile", path, dst, size))
        self._card_access("LoadFile")
        if (path, dst, size) != (CFW_PATH, WINDOW, WINDOW_SIZE):
            raise Fail(f"LoadFile({path!r}, 0x{dst:08X}, 0x{size:X})")
        if not self.card or self.file is None:
            return -1
        data = self.file[:size]
        self.uc.mem_write(dst, data)
        return len(data)

    def _Section_Load(self):
        n, arg = self.r(0), self.r(1)
        self.events.append(("Section_Load", n))
        if (n, arg) != (self.cur, 0):
            raise Fail(f"Section_Load({n}, {arg}) during section {self.cur}")
        self.loaded.add(n)
        return 0x5EC00000 | n

    def _stage1_hook(self):
        n, state = self.r(0), self.r(1)
        self.events.append(("stage1_hook", n))
        if state != STATE or self.u32(STATE) != 5:
            raise Fail(f"stage1_hook r1=0x{state:08X}, state {self.u32(STATE)}")
        last_load = [e for e in self.events if e[0] == "Section_Load"][-1]
        if last_load != ("Section_Load", n) or ("stage1_hook", n) in self.events[:-1]:
            raise Fail(f"stage1_hook({n}) not once, after Section_Load({n})")
        if n == self.disarm_at:
            self.uc.mem_write(STATE, struct.pack(">I", 6))
        return 0

    def section(self, n):
        """Run the hook site for one section load, like FUN_a0022e28(n)."""
        self.cur = n
        uc = self.uc
        uc.reg_write(UC_ARM_REG_CPSR, (uc.reg_read(UC_ARM_REG_CPSR) & ~0x1F) | 0x10)   # USR (banks SP first)
        regs = {UC_ARM_REG_R4: 0x44444444, UC_ARM_REG_R5: n, UC_ARM_REG_R6: 0x66666666,
                UC_ARM_REG_R7: 0xA00C22E4 + n * 0x18, UC_ARM_REG_SP: STACK_TOP}
        for reg, v in regs.items():
            uc.reg_write(reg, v)
        uc.reg_write(UC_ARM_REG_LR, 0xDEAD0001)
        try:
            uc.emu_start(HOOK_SITE | 1, HOOK_END, count=2000)
        except UcError as ex:
            raise self.err or Fail(f"{ex} at 0x{uc.reg_read(UC_ARM_REG_PC):08X}") from ex
        if self.err:
            raise self.err
        if uc.reg_read(UC_ARM_REG_PC) != HOOK_END:
            raise Fail(f"section {n}: did not return to 0x{HOOK_END:08X}")
        for reg, v in regs.items():
            if uc.reg_read(reg) != v:
                raise Fail(f"section {n}: callee-saved register/SP not preserved")
        if uc.reg_read(UC_ARM_REG_R0) != regs[UC_ARM_REG_R7]:
            raise Fail(f"section {n}: the caller's movs r0,r7 did not run")

    def run(self):
        for n in self.order:
            self.section(n)
        return self


def stage1_model():
    """Stage 1 with the loader ABI: +4 magic, +8 Thumb hook entry (in the window)."""
    img = bytearray(0x7EC0)
    struct.pack_into(">HHII", img, 0, 0xB5F8, 0x46C0, LDR_MAGIC, STAGE1_HOOK | 1)
    return bytes(img)


def expect(name, emu, status, mount, load, hooks, window, port=1):
    ev = emu.events
    loads = [e[1] for e in ev if e[0] == "Section_Load"]
    got_hooks = [e[1] for e in ev if e[0] == "stage1_hook"]
    n_mount = sum(e[0] == "File_MountMedia" for e in ev)
    n_load = sum(e[0] == "LoadFile" for e in ev)
    win = bytes(emu.uc.mem_read(WINDOW, WINDOW_SIZE))
    ok = (emu.u32(STATE) == status and loads == emu.order and got_hooks == hooks
          and n_mount == mount and n_load == load and sum(e[0] == "Port_Get" for e in ev) == port
          and win == window)
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
    print(f"       ldr_state={emu.u32(STATE)} ({ST.get(emu.u32(STATE))}); Port_Get x{port}, "
          f"File_MountMedia x{n_mount}, LoadFile x{n_load}; stock loads {loads}")
    print(f"       stage-1 hook calls {got_hooks or 'none'}; window "
          f"{'unchanged (0xFF)' if win == b'\xff' * WINDOW_SIZE else 'holds gr2_cfw.bin'}")
    if not ok:
        print(f"       events: {ev}")
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--frm", type=Path, default=REPO_ROOT / "dist" / "rg2_v003_loader.frm")
    ap.add_argument("--stock", type=Path, default=STOCK_FRM)
    ap.add_argument("--stage1", type=Path, default=REPO_ROOT / "bin" / "release" / "gr2_cfw.bin",
                    help="current stage 1 (loader ABI v1, M10b); its hook is modelled at its +8 address")
    a = ap.parse_args(argv)
    if not a.frm.exists():
        print(f"[FAIL] {a.frm} missing: run scripts/build_loader_frm.py first")
        return 1
    try:
        app = check_bytes(a.stock, a.frm)
    except Fail as ex:
        print(f"[FAIL] {ex}")
        return 1

    abi = stage1_model()
    cur = a.stage1.read_bytes() if a.stage1.exists() else None
    if cur is None or struct.unpack_from(">I", cur, 4)[0] != LDR_MAGIC:
        print(f"[FAIL] {a.stage1}: missing or without the loader ABI magic (run make)")
        return 1
    cur_hook = struct.unpack_from(">I", cur, 8)[0]
    if not (cur_hook & 1) or not WINDOW <= cur_hook & ~1 < WINDOW + len(cur):
        print(f"[FAIL] {a.stage1}: hook entry 0x{cur_hook:08X} is not a Thumb address inside the image")
        return 1
    old = cur[:4] + b"\0\0\0\0" + cur[8:]                                # a pre-M10b stage 1: no magic
    ff = b"\xff" * WINDOW_SIZE
    loaded = lambda f: f[:WINDOW_SIZE] + ff[len(f):]                       # noqa: E731
    cases = [
        ("A  card + H:/cfw/gr2_cfw.bin with loader ABI -> load at section 3, hook from there on",
         dict(file=abi), 5, 1, 1, SHOOTING[3:], loaded(abi)),
        ("B  card, file absent -> stock", dict(file=None), 3, 1, 1, [], ff),
        ("C  shutter half-press held (file present) -> stock, no mount", dict(file=abi, skip=True), 1, 0, 0, [], ff),
        ("D  no card -> stock", dict(card=False, file=abi), 2, 1, 0, [], ff),
        ("E  stage 1 without loader ABI (pre-M10b) -> loaded, never called", dict(file=old), 4, 1, 1, [],
         loaded(old)),
        ("F  playback boot order, loader ABI", dict(file=abi, order=PLAYBACK), 5, 1, 1, PLAYBACK[2:], loaded(abi)),
        ("G  stage 1 disarms at section 5", dict(file=abi, disarm_at=5), 6, 1, 1, [3, 5], loaded(abi)),
        ("I  USB-charge boot (section 1 only) -> card untouched, undecided", dict(file=abi, order=[1]), 0, 0, 0, [],
         ff, 0),
        (f"H  current {a.stage1.name} (loader ABI v1) -> hook at its +8 entry 0x{cur_hook:08X}",
         dict(file=cur, hook=cur_hook), 5, 1, 1, SHOOTING[3:], loaded(cur)),
    ]
    ok = True
    for name, kw, status, mount, load, hooks, window, *port in cases:
        try:
            emu = Emu(app, **kw).run()
        except Fail as ex:
            print(f"[FAIL] {name}: {ex}")
            ok = False
            continue
        ok &= expect(name, emu, status, mount, load, hooks, window, *port)
    print("ALL PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
