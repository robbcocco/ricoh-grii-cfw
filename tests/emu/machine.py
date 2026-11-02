"""Unicorn model of the parts of a Ricoh GR II (fw 3.00) that the CFW touches.

The CFW runs unmodified: stage 1 is loaded into the window like `fload`,
`_start` loads stage 2 from the H: drive model, and the scenarios then call
stage-2 functions directly. Firmware entry points the CFW calls are Python
stubs (see `_install_stubs`); firmware code in `REAL_CODE` runs as is. Any
other instruction fetch outside the CFW raises `EmuError` with its address.

Memory: one 16 MiB host buffer is mapped at 0xA0000000 (cached DRAM) and at
0x20000000 (the uncached alias the CFW uses for OSD bitmaps, the live-view
frame and the IQ dump), so both views share their bytes like on the camera.
"""
import ctypes
import struct

from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_FETCH_UNMAPPED, UC_HOOK_MEM_READ_UNMAPPED,
                     UC_HOOK_MEM_WRITE_UNMAPPED, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_PROT_ALL, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP,
                              UC_CPU_ARM_946)

MASK = 0xFFFFFFFF

# ---- memory layout ---------------------------------------------------------------------------
RAM_BASE, RAM_SIZE = 0xA0000000, 0x01000000
ALIAS_BASE = 0x20000000               # GR2_UNCACHED(a) = (a & 0x1FFFFFFF) | 0x20000000
STACK_BASE, STACK_SIZE = 0xB0000000, 0x00100000
STACK_TOP = STACK_BASE + STACK_SIZE - 0x100
# Task stacks the CFW's code runs on (bytes). Calls default to the CFW task; boot paths name theirs.
CFW_TASK_STACK = 0x3000               # src/task.c (also CFWcon)
SCRIPT_TASK_STACK = 0x4000            # Startup.ttl's `ex` runs in the Script task (finding 26 §3)
ROOT_TASK_STACK = 0x4000              # loader hooks run in the root init task (kernel config 0xA00B5B98)
# JOBID_POWER_OFF runs in the FRM task (stack 0x3000, finding 26 §3) below the JobCore loop and the job
# dispatch; their frames are not measured, so 0x800 of the stack is reserved for them [I].
FRM_JOB_STACK = 0x3000 - 0x800
# Static worst-case stack of the stock file API incl. the SD driver (finding 31 §3).
STOCK_FILE_STACK = {'File_Open': 8612 + 712, 'File_Read': 4188 + 712, 'File_Write': 4244 + 712,
                    'File_Close': 4132 + 712}
WINDOW, WINDOW_SIZE = 0xA0017000, 0x8000
SENTINEL = 0xA001F000                 # return address of every call (just past the window)
FW_BASE = 0xA0020200                  # RAM = 0xA0020200 + file offset
HEAP_LO, HEAP_HI = 0xA0798000, 0xA0900000   # Memory_Alloc arena (GlobalHeap stand-in)
OBJ_LO, OBJ_HI = 0xA0900000, 0xA0940000     # firmware-owned objects (handles, TCBs, mutex, PDB clones)
IQ_CTX = 0xA0940000
IQ_BASE = 0xA0950000                  # IQ.bin; IQ_BYTES = file + 0x180
IQ_BYTES = 0x2FAA0
LV_FRAME = 0xA0980000                 # shown live-view frame, planar YUV 4:2:2, 640x480
POOL_BUMP_VALUE = 0xA0B00000          # DEV "Free memory pool" display only
OSD_BITMAPS = (0x20EACC40, 0x20F01240)   # BITMAPS[0], [1] as the camera holds them (uncached)
OSD_BUF_BYTES = 0x54600

# ---- firmware addresses (include/gr2_hw.h, src/osd.c, src/menu.c) -----------------------------
A_AEF = 0xA070FC48
A_IQ_CTX_PTR = 0xA015A430
A_ISP_MUTEX = 0xA0155BBC + 8
A_LV_CACHE_CURVE, A_LV_CACHE_EV = 0xA06C8B9A, 0xA0155D20
A_LV_DISP_ADDR, A_LV_DISP_MODE = 0xA0155BE8, 0xA0155B44
A_POOL_BUMP = 0xA00C2BA4
A_SHELL_CTX = 0xA05FC308
A_GSTATE = 0xA04070AC                 # +0 OSD front index, +3 output (1 = LCD)
A_PAL_PENDING = 0xA04070C8
A_BITMAPS = 0xA077C418
A_INVENT = 0xA06BDD08
A_MSG_HANDLER_SLOT = 0xA04071F0
A_CONSOLE_HOOK_SLOT = 0xA05FCCD0 + 0x44   # console printf hook (finding 21 section 3.2)
A_TSK_OBJ_TABLE = 0xA05FB884
LDR_STATE, LDR_MAGIC = 0xA00AFD70, 0x47524C31     # flashed debug loader status word, stage-1 ABI magic
A_MODE_PROPS_FINAL = 0xA012A0EC       # set by the block after JobSystem step 2 (finding 32)
LOADER_SHOOTING = [1, 4, 2, 3, 5, 10, 11, 8, 9, 6, 12, 13]   # section-load order, shooting boot
LOADER_DECIDE_SECTION = 3            # the stub's card session waits for the lens start (finding 33)
SNAP_TABLES = {0xA06FD5E2: 0x0400, 0xA06FD5E8: 0x02AA, 0xA0700066: 0x0400, 0xA070006C: 0x02AA}  # Lens_F / AF_F
# Full ("RGB") plane, display layer 2 (finding 23): RGBA4444 front buffers BITMAPS[2], [3] (pitch 640 px)
# and the stock canvas (pitch 720 px), all held as uncached aliases like the OSD bitmaps.
FULL_BUFS = (0xA0C00000, 0xA0C96000)  # above the LV frame (0xA0980000 + 0x96000)
FULL_CANVAS = 0xA0D40000
A_GST_MASK = 0xA04070B8               # plane mask of the current screen (bit 1 = full plane)
A_RGB_CANVAS = 0xA043EA44
A_REC_RGB_REDRAW = 0xA0424360
A_LV_ASPECT = 0xA0155B48
SYSMODE_SHOOTING = 6

FN = dict(
    memory_alloc=0xA0024D6E, file_open=0xA002C70E, file_read=0xA002CA98, file_write=0xA002CC4A,
    file_close=0xA002C904, find_first=0xA002EF58, find_next=0xA002F354, find_close=0xA002FA3C,
    prop_get=0xA00E0E2E, prop_get_raw=0xA00E0E80, prop_set=0xA00E0C94, prop_lock=0xA00E127A,
    prop_unlock=0xA00E1286, prop_sa_read=0xA00E0FFE, prop_sa_write=0xA00E100E, prop_path=0xA00DDB68,
    pdb_clone=0xA0037E14, pdb_load=0xA0037A42, pdb_save=0xA0037AE6, pdb_free=0xA0037478,
    ui_state=0xA00E5F20, task_create=0xA0026708, delay=0xA0026836, get_time=0xA0027D96,
    list_append=0xA0054496, mutex_lock=0xA0027360, mutex_unlock=0xA00273A4,
    pal_get=0xA0393990, pal_set=0xA03939EE, osd_state=0xA00F5FE2, hdmi_state=0xA0109862,
    pal_bank=0xA00F5F92, hdmi_pal_bank=0xA010982A, set_layer=0xA00D717E, msg_post=0xA00361E0,
    adc_read=0xA003A8DA, power_temp=0xA00CF606, uart_print=0xA0039CBC, layer_on=0xA00D7148,
    stock_press=0xA039607C, stock_hold=0xA0396EA0, stock_msg_handler=0xA039725E, stock_iq_ready=0xA01C2A80,
    stock_run_ttl=0xA00EA1D4,
    shell_execute=0xA002A274, shell_result=0xA002A718, console_hook=0xA0039CB6,
    ref_tsk=0xA006A2A0, ref_mpl=0xA0067B00,                     # ARM-state kernel calls
    recv=0xA0022A36, send=0xA0022A5E, sock_close=0xA0022B90,
)
STRING_RESOLVER = 0xA03A4AEF          # string ID -> UTF-16 pointer (finding 16 section 6)

# Firmware code that runs for real (half-open ranges).
REAL_CODE = [
    (0xA03A4AD0, 0xA03A4F5E),         # string accessors (resolver, bound checks patched by the CFW)
    (0xA03A1F92, 0xA03A1F96),         # Effect press BL site (patched to the press veneer)
    (0xA03A5790, 0xA03A5794),         # Effect hold BL site (patched to the hold veneer)
    (0xA01B584A, 0xA01B584E),         # IQ-ready BL site in CapInit(5) (patched to the IQ veneer, loader only)
    (0xA00E994E, 0xA00E9954),         # power-off job: adr r0,"*:/Shutdown.ttl"; bl (debug: power-off veneer)
    (SENTINEL, SENTINEL + 2),
]


class EmuError(Exception):
    pass


# ---- ELF symbols (32-bit big-endian, no external tools) ----------------------------------------

def elf_symbols(path):
    """{name: value} for globals, {name: [values]} for locals (static names may repeat)."""
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF' or d[4] != 1 or d[5] != 2:
        raise EmuError(f'{path}: not an ELF32 big-endian file')
    shoff, = struct.unpack_from('>I', d, 0x20)
    shentsize, shnum = struct.unpack_from('>HH', d, 0x2E)
    secs = [struct.unpack_from('>10I', d, shoff + i * shentsize) for i in range(shnum)]
    glob, loc = {}, {}
    for s in secs:
        if s[1] != 2:                 # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for off in range(s[4], s[4] + s[5], 16):
            name_off, value, _size, info, _other, shndx = struct.unpack_from('>IIIBBH', d, off)
            if not name_off or shndx in (0, 0xFFF1) or (info & 0xF) in (3, 4):   # undef, abs, section, file
                continue
            end = d.index(b'\0', strtab[4] + name_off)
            name = d[strtab[4] + name_off:end].decode()
            if name.startswith('$'):
                continue
            if info >> 4 == 0:
                loc.setdefault(name, []).append(value)
            else:
                glob[name] = value
    return glob, loc


class Symbols:
    def __init__(self, path, base=0):
        g, l = elf_symbols(path)
        self.glob = {k: (v + base) & MASK for k, v in g.items()}
        self.loc = {k: [(v + base) & MASK for v in vs] for k, vs in l.items()}

    def __getitem__(self, name):
        if name in self.glob:
            return self.glob[name]
        vs = self.loc.get(name, [])
        if len(vs) != 1:
            raise EmuError(f'symbol {name!r}: {"missing" if not vs else "ambiguous"}')
        return vs[0]

    def __contains__(self, name):
        return name in self.glob or len(self.loc.get(name, [])) == 1

    def name_at(self, addr):
        best, bname = -1, None
        for k, v in list(self.glob.items()) + [(k, v) for k, vs in self.loc.items() for v in vs]:
            if (v & ~1) <= addr and (v & ~1) > best:
                best, bname = v & ~1, k
        return f'{bname}+{addr - best:#x}' if bname else hex(addr)


# ---- palette ----------------------------------------------------------------------------------

def yuv_rgb(y, u, v):
    """Same integer conversion as the CFW screenshot (src/info.c)."""
    u -= 128
    v -= 128
    c = lambda x: 0 if x < 0 else 255 if x > 255 else x
    return c(y + ((359 * v) >> 8)), c(y - ((88 * u + 183 * v) >> 8)), c(y + ((454 * u) >> 8))


class Machine:
    def __init__(self, inputs, variant, cfw_dir, files=None, props=None, slots=None):
        self.stack_limit, self.stack_task = STACK_TOP - CFW_TASK_STACK, 'CFW'
        self.inputs, self.variant, self.cfw_dir = inputs, variant, cfw_dir
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_ARM_946)   # ARMv5TE: Thumb-2 encodings fault
        self._ram = ctypes.create_string_buffer(RAM_SIZE)
        ptr = ctypes.addressof(self._ram)
        self.uc.mem_map_ptr(RAM_BASE, RAM_SIZE, UC_PROT_ALL, ptr)
        self.uc.mem_map_ptr(ALIAS_BASE, RAM_SIZE, UC_PROT_ALL, ptr)
        self.uc.mem_map(STACK_BASE, STACK_SIZE)
        self._err = None
        self.syms1 = Symbols(f'{cfw_dir}/bin/{variant}/gr2_cfw.elf')
        self.syms = None              # stage 2, after boot
        self.main_base = None

        # state of the modelled firmware
        self.files, self.names = {}, {}
        for k, v in (files or {}).items():
            self.put_file(k, v)
        self.handles, self.next_fd = {}, 0x100
        self.finds = {}
        self.props = dict(DEFAULT_PROPS)
        self.props.update(props or {})
        self.prop_sets, self.prop_lock_depth, self.prop_set_unlocked = [], 0, []
        self.prop_sa = {0x1E9: b'GR_A1B2C3', 0x1F2: b'secretpass12'}
        self.prop_sa_written = {}
        self.slots = {k: list(v) for k, v in (slots or {}).items()}
        self.pdb_open = 0
        self.heap = HEAP_LO
        self.allocs = []
        self.obj = OBJ_LO
        self.time_ms = 10000
        self.tasks = []
        self.mutex = {}               # address -> [locks, unlocks, depth]
        self.msgs = []
        self.adc = {6: 641}
        self.temps = {0: 31, 1: 38, 2: 33}
        self.layer = {}
        self.stock_calls = []
        self.shell_lines, self.shell_rc, self.shell_result = [], 0, None
        self.rtos_tasks = {}          # id -> {'stat', 'pri'} for ref_tsk
        self.sock_in, self.sock_out = [], bytearray()   # recv chunks queued for the console session; sent bytes
        self.closed_fds = []                            # sockets closed by the CFW
        pal = list(inputs.palette)
        self.pal_stock = list(pal)
        self.pal_shadow = list(pal)   # what the palette getter returns
        self.pal_banks = [list(pal), list(pal)]
        self.pal_shown = 0            # hardware bank shown on the LCD
        self.pal_sets = []
        self.pal_flashes = []         # (entries, pixels): shown OSD pixels that flipped to a stock colour

        self._seed_memory()
        self._install_stubs()
        self._install_hooks()

    # ---- memory helpers ----------------------------------------------------------------------
    def rd(self, a, n):
        return bytes(self.uc.mem_read(a, n))

    def wr(self, a, b):
        self.uc.mem_write(a, bytes(b))

    def u8(self, a):
        return self.rd(a, 1)[0]

    def u16(self, a):
        return struct.unpack('>H', self.rd(a, 2))[0]

    def u32(self, a):
        return struct.unpack('>I', self.rd(a, 4))[0]

    def w16(self, a, v):
        self.wr(a, struct.pack('>H', v & 0xFFFF))

    def w32(self, a, v):
        self.wr(a, struct.pack('>I', v & MASK))

    def ustr(self, a, n=64):
        out = []
        for i in range(n):
            c = self.u16(a + 2 * i)
            if c == 0:
                break
            out.append(chr(c))
        return ''.join(out)

    def obj_alloc(self, n, fill=0):
        p = self.obj
        self.obj = (self.obj + n + 15) & ~15
        if self.obj > OBJ_HI:
            raise EmuError('firmware object pool exhausted (harness)')
        self.wr(p, bytes([fill]) * n)
        return p

    def cstr(self, a, n=0x200):
        out = b''
        while len(out) < n:
            chunk = self.rd(a + len(out), 16 - ((a + len(out)) & 15))   # never crosses a page
            if 0 in chunk:
                return (out + chunk[:chunk.index(0)]).decode('latin-1')
            out += chunk
        return out[:n].decode('latin-1')

    def put_file(self, path, data):
        key = path.upper()
        self.files[key] = bytearray(data)
        self.names[key] = path

    def file(self, path):
        f = self.files.get(path.upper())
        return None if f is None else bytes(f)

    def log_lines(self):
        f = self.file('H:/cfw/cfw_log.txt')
        return [] if f is None else f.decode('latin-1').splitlines()

    # ---- seeding -----------------------------------------------------------------------------
    def _seed_memory(self):
        inp = self.inputs
        self.wr(FW_BASE, inp.firmware)
        self.wr(WINDOW, b'\xff' * WINDOW_SIZE)            # the IPL copy's erased tail
        self.wr(A_AEF, inp.ae_f)                           # A:/fix/AE_F.bin, loaded at boot
        for a, v in SNAP_TABLES.items():                   # Lens_F / AF_F runtime copies
            self.w16(a, v)
        # IQ RAM: IQ.bin with its section offsets (+0x24, 0x18 words) made absolute.
        iq = bytearray(inp.iq)
        for s in range(0x18):
            off, = struct.unpack_from('>I', iq, 0x24 + 4 * s)
            struct.pack_into('>I', iq, 0x24 + 4 * s, IQ_BASE + off)
        self.wr(IQ_BASE, iq)
        self.iq_image = bytes(iq)
        self.w32(A_IQ_CTX_PTR, IQ_CTX)
        self.wr(IQ_CTX + 0x2C, b'\x00')                    # bank 0
        self.w32(IQ_CTX + 0x158, IQ_BASE)
        self.w32(IQ_CTX + 0x15C, IQ_BASE)
        self.w32(A_ISP_MUTEX, self.obj_alloc(0x20))
        self.w16(A_LV_CACHE_CURVE, 0x0005)
        self.w32(A_LV_CACHE_EV, 0x00000003)
        # Live view: horizontal luma ramp, neutral chroma.
        y = bytes((x * 255) // 639 for x in range(640)) * 480
        self.wr(LV_FRAME, y + b'\x80' * (0x96000 - len(y)))
        self.w32(A_LV_DISP_ADDR, LV_FRAME)
        self.wr(A_LV_DISP_MODE, b'\x01')
        self.w32(A_POOL_BUMP, POOL_BUMP_VALUE)
        # OSD: stock double buffer, front = BITMAPS[GSTATE[0]], LCD output.
        self.w32(A_BITMAPS, OSD_BITMAPS[0])
        self.w32(A_BITMAPS + 4, OSD_BITMAPS[1])
        self.wr(A_GSTATE, b'\x00')
        self.wr(A_GSTATE + 3, b'\x01')
        self.w32(A_PAL_PENDING, 0)
        # Stock UI stand-in: top and bottom info bars using every palette index (2-px columns,
        # entries 224-251 included), transparent in between.
        bar = bytes((x >> 1) & 0xFF for x in range(640)) * 40
        self.wr(OSD_BITMAPS[0], bar)
        self.wr(OSD_BITMAPS[0] + 440 * 640, bar)
        self.osd_front_image = self.rd(OSD_BITMAPS[0], OSD_BUF_BYTES)
        # Full plane (layer 2) in the shooting screen: on, transparent, front = BITMAPS[2]; the stock
        # canvas holds a grid-guide row (y 160, RGBA4444 0x0008) so restores from it can be checked.
        alias = lambda a: (a & 0x1FFFFFFF) | ALIAS_BASE
        self.w32(A_BITMAPS + 8, alias(FULL_BUFS[0]))
        self.w32(A_BITMAPS + 12, alias(FULL_BUFS[1]))
        self.wr(A_GSTATE + 1, b'\x02')
        self.w32(A_GST_MASK, 3)
        self.w32(A_RGB_CANVAS, alias(FULL_CANVAS))
        self.wr(FULL_CANVAS + 160 * 720 * 2, struct.pack('>H', 0x0008) * 640)
        self.w32(A_REC_RGB_REDRAW, 0)
        self.wr(A_LV_ASPECT, b'\x02')                       # 3:2
        self.props.setdefault(0, SYSMODE_SHOOTING)          # PROPID_SYSTEM_MODE
        self.layers_on = {2: 1, 3: 1}
        # Shell registry: 922 stock records (97 base + 825 command.txt).
        self.w32(A_SHELL_CTX + 4, self.obj_alloc(12))
        self.w32(A_SHELL_CTX + 8, 922)
        # Inventory (B:/Invent.bin RAM copy).
        self.w32(A_INVENT + 0xE4, 1)
        for i, v in ((0x24, 12345), (0x25, 13002), (0x23, 811)):
            self.w32(A_INVENT + 0x40 + 4 * i, v)
        # My Settings name caches (props 0x1BA + k) and the stock UI state block.
        for k in range(3):
            self.props.setdefault(0x1BA + k, self.obj_alloc(0x44))
        self.ui_state = self.obj_alloc(0x300)
        self.my_paths = [self.obj_alloc(16) for _ in range(3)]
        for k, p in enumerate(self.my_paths):
            self.wr(p, f'B:/P_MY0{k}.bin'.encode() + b'\0')

    # ---- calls -------------------------------------------------------------------------------
    def r(self, n):
        return self.uc.reg_read(UC_ARM_REG_R0 + n)

    def ret(self, v=0):
        self.uc.reg_write(UC_ARM_REG_R0, v & MASK)
        self.uc.reg_write(UC_ARM_REG_PC, self.uc.reg_read(UC_ARM_REG_LR))

    def call_addr(self, addr, *args, stack=CFW_TASK_STACK, task='CFW'):
        """Call a Thumb function (bit 0 set) or ARM function and return r0, on a task stack of
        `stack` bytes (the stock file stubs check that their stack need fits)."""
        return self.run_range(addr, SENTINEL, *args, stack=stack, task=task)

    def run_range(self, begin, until, *args, timeout_s=120, stack=CFW_TASK_STACK, task='CFW'):
        """Run from `begin` (Thumb: bit 0 set) until pc == `until`; returns r0.
        LR is the sentinel, so a plain function call stops at its return."""
        if len(args) > 4:
            raise EmuError('more than 4 arguments')
        for i, a in enumerate(args):
            self.uc.reg_write(UC_ARM_REG_R0 + i, a & MASK)
        cpsr = self.uc.reg_read(UC_ARM_REG_CPSR)
        self.uc.reg_write(UC_ARM_REG_CPSR, (cpsr & ~0x1F) | 0x10)   # tasks run in USR mode (as on hardware)
        self.uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        self.stack_limit, self.stack_task = STACK_TOP - stack, task
        self.uc.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
        self._err = None
        try:
            self.uc.emu_start(begin, until, timeout=timeout_s * 1000000)
        except UcError as ex:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise self._err or EmuError(f'{ex} at pc {pc:#x} ({self.where(pc)})') from None
        if self._err:
            raise self._err
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        if pc != until & ~1:
            raise EmuError(f'run from {begin:#x} stopped at pc {pc:#x} ({self.where(pc)}), not {until:#x}; timeout?')
        return self.r(0)

    def call(self, name, *args):
        return self.call_addr(self.syms[name] | 1, *args)

    def where(self, addr):
        if self.syms and self.main_base <= addr < self.main_end:
            return 'stage 2 ' + self.syms.name_at(addr)
        if WINDOW <= addr < WINDOW + WINDOW_SIZE:
            return 'stage 1 ' + self.syms1.name_at(addr)
        return 'firmware'

    # ---- hooks -------------------------------------------------------------------------------
    def _install_hooks(self):
        hdr = struct.unpack('>8I', self.inputs_main()[:32])
        self.main_end = HEAP_LO + ((hdr[1] + hdr[2] + 7) & ~7)
        # Block hooks everywhere except the CFW's own code (stage 1 window, stage 2 block):
        # stage 2 is the first Memory_Alloc block, so its range is known before boot.
        for lo, hi in ((0, WINDOW - 1), (WINDOW + WINDOW_SIZE, HEAP_LO - 1), (self.main_end, MASK)):
            self.uc.hook_add(UC_HOOK_BLOCK, self._on_block, begin=lo, end=hi)
        self.uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED,
                         self._on_unmapped)

    def inputs_main(self):
        return open(f'{self.cfw_dir}/bin/{self.variant}/gr2_main.bin', 'rb').read()

    def _fail(self, msg):
        if not self._err:
            self._err = EmuError(msg)
        self.uc.emu_stop()

    def _on_block(self, uc, addr, size, _):
        fn = self.stubs.get(addr)
        if fn:
            try:
                fn()
            except EmuError as ex:
                self._fail(str(ex))
            except Exception as ex:     # harness bug: report with context
                self._fail(f'stub {fn.__name__} at {addr:#x}: {ex!r}')
            return
        for lo, hi in REAL_CODE:
            if lo <= addr < hi:
                return
        lr = uc.reg_read(UC_ARM_REG_LR)
        self._fail(f'unexpected execution at {addr:#x} (lr {lr:#x}, {self.where(lr & ~1)})')

    def _on_unmapped(self, uc, access, addr, size, value, _):
        pc = uc.reg_read(UC_ARM_REG_PC)
        self._fail(f'unmapped access {addr:#x} (size {size}) at pc {pc:#x} ({self.where(pc)})')
        return False

    # ---- stubs -------------------------------------------------------------------------------
    def _install_stubs(self):
        s = {}
        for name, addr in FN.items():
            s[addr] = getattr(self, '_' + name)
        self.stubs = s

    def _memory_alloc(self):
        n = self.r(0)
        p = self.heap
        self.heap = (self.heap + n + 7) & ~7
        if self.heap > HEAP_HI:
            raise EmuError(f'Memory_Alloc({n:#x}): emulated heap exhausted')
        self.allocs.append((p, n))
        self.ret(p)

    # files (H: drive; B: slot files go through the PDB stubs)
    def _stack_need(self, api):
        # The stubs stand in for stock PrFILE2 code that needs a lot of the caller's task stack
        # (finding 31: static worst case of the file API plus 712 B of SD driver). A task whose
        # remaining stack is smaller overflows on hardware (F3, 2026-11-01: CFWldr had 0x1000).
        need = STOCK_FILE_STACK[api]
        left = self.uc.reg_read(UC_ARM_REG_SP) - self.stack_limit
        if left < need:
            self._fail(f'{api} needs ~{need} B of stack, {left} B left in the {self.stack_task} task')

    def _file_open(self):
        self._stack_need('File_Open')
        path, mode = self.cstr(self.r(0)), self.r(1)
        key = path.upper()
        if not key.startswith('H:/'):
            raise EmuError(f'File_Open({path!r}): only H: is modelled')
        if mode == 0:
            if key not in self.files:
                return self.ret(0)
        elif mode == 1:
            self.put_file(path, b'')
        elif mode == 9:
            if key not in self.files:
                self.put_file(path, b'')
        else:
            raise EmuError(f'File_Open({path!r}, {mode:#x}): flags not modelled')
        fd = self.next_fd
        self.next_fd += 1
        self.handles[fd] = [key, 0, mode]
        self.ret(fd)

    def _handle(self, fd):
        h = self.handles.get(fd)
        if h is None:
            raise EmuError(f'file handle {fd:#x} not open')
        return h

    def _file_read(self):
        self._stack_need('File_Read')
        h = self._handle(self.r(0))
        buf, n = self.r(1), self.r(2)
        d = self.files[h[0]][h[1]:h[1] + n]
        self.wr(buf, d)
        h[1] += len(d)
        self.ret(len(d))

    def _file_write(self):
        self._stack_need('File_Write')
        h = self._handle(self.r(0))
        buf, n = self.r(1), self.r(2)
        if h[2] == 0:
            return self.ret(-1)
        f = self.files[h[0]]
        if h[2] == 9:
            h[1] = len(f)
        f[h[1]:h[1] + n] = self.rd(buf, n)
        h[1] += n
        self.ret(n)

    def _file_close(self):
        self._stack_need('File_Close')
        self._handle(self.r(0))
        del self.handles[self.r(0)]
        self.ret(0)

    def _dir_entries(self, d):
        pre = d.upper().rstrip('/') + '/'
        out = {}
        for key in self.files:
            if key.startswith(pre):
                rest = key[len(pre):]
                comp = rest.split('/')[0]
                isdir = '/' in rest
                name = comp if isdir else self.names[key].split('/')[-1]
                out[comp] = (name, isdir, 0 if isdir else len(self.files[key]))
        if not out:
            return None
        return [('.', True, 0), ('..', True, 0)] + [out[k] for k in sorted(out)]

    def _fill_find(self, h, out):
        ents, i = self.finds[h]
        if i >= len(ents):
            return 0
        self.finds[h][1] += 1
        name, isdir, size = ents[i]
        b = bytearray(0x230)
        b[:len(name)] = name.encode()
        sfn = name.upper().encode()[:12]
        b[0x200:0x200 + len(sfn)] = sfn
        b[0x20D] = 0x10 if isdir else 0x20
        struct.pack_into('>II', b, 0x210, 0, size)
        self.wr(out, b)
        return 1

    def _find_first(self):
        d, out = self.cstr(self.r(0)), self.r(1)
        ents = self._dir_entries(d)
        if ents is None:
            return self.ret(0)
        h = self.obj_alloc(16)
        self.finds[h] = [ents, 0]
        self._fill_find(h, out)
        self.ret(h)

    def _find_next(self):
        h = self.r(0)
        if h not in self.finds:
            raise EmuError(f'FindNext on unknown handle {h:#x}')
        self.ret(self._fill_find(h, self.r(1)))

    def _find_close(self):
        if self.finds.pop(self.r(0), None) is None:
            raise EmuError(f'FindClose on unknown handle {self.r(0):#x}')
        self.ret(0)

    # property DB
    def _prop(self, pid):
        if pid not in self.props:
            raise EmuError(f'Prop_Get({pid:#x}): property not seeded in the harness')
        return self.props[pid]

    def _prop_get(self):
        self.ret(self._prop(self.r(0)))

    def _prop_get_raw(self):
        self.ret(self._prop(self.r(0)))

    def _prop_set(self):
        pid, v = self.r(0), self.r(1)
        if self.prop_lock_depth <= 0:
            self.prop_set_unlocked.append(pid)
        self.props[pid] = v
        self.prop_sets.append((pid, v))
        self.ret(0)

    def _prop_lock(self):
        self.prop_lock_depth += 1
        self.ret(0)

    def _prop_unlock(self):
        self.prop_lock_depth -= 1
        self.ret(0)

    def _prop_sa_read(self):
        pid, n, buf = self.r(0), self.r(1), self.r(2)
        if pid not in self.prop_sa:
            raise EmuError(f'Prop_SA_Read({pid:#x}): not seeded')
        self.wr(buf, (self.prop_sa[pid] + b'\0' * n)[:n])
        self.ret(0)

    def _prop_sa_write(self):
        pid, n, buf = self.r(0), self.r(1), self.r(2)
        self.prop_sa_written[pid] = self.rd(buf, n)
        self.ret(0)

    def _prop_path(self):
        kind, k = self.r(0), self.r(1)
        if kind != 1 or k > 2:
            raise EmuError(f'prop path ({kind}, {k}) not modelled')
        self.ret(self.my_paths[k])

    # My Settings slot files: B:/P_MY0k.bin as 899 u32 values
    def _pdb_clone(self):
        db = self.obj_alloc(0x40 + 899 * 4)
        self.w32(db + 0xC, db + 0x40)
        self.pdb_open += 1
        self.ret(db)

    def _pdb_load(self):
        db, path = self.r(0), self.cstr(self.r(1))
        vals = self.slots.get(path, [0] * 899)
        self.wr(self.u32(db + 0xC), struct.pack('>899I', *vals))
        self.ret(db)

    def _pdb_save(self):
        db, path = self.r(0), self.cstr(self.r(1))
        self.slots[path] = list(struct.unpack('>899I', self.rd(self.u32(db + 0xC), 899 * 4)))
        self.ret(0)

    def _pdb_free(self):
        self.pdb_open -= 1
        self.ret(0)

    def _ui_state(self):
        self.ret(self.ui_state)

    # RTOS
    def _task_create(self):
        tcb = self.obj_alloc(0x40)
        self.tasks.append(dict(entry=self.r(0), arg=self.r(1), prio=self.r(2), stack=self.r(3),
                               name=self.cstr(self.u32(self.uc.reg_read(UC_ARM_REG_SP))), tcb=tcb))
        self.ret(tcb)

    def _delay(self):
        self.time_ms += self.r(0)
        self.ret(0)

    def _get_time(self):
        self.ret(self.time_ms)

    def _list_append(self):
        head, data = self.r(0), self.r(1)
        node = self.obj_alloc(12)
        self.w32(node, data)
        if head == 0:
            return self.ret(node)
        p = head
        while self.u32(p + 4):
            p = self.u32(p + 4)
        self.w32(p + 4, node)
        self.w32(node + 8, p)
        self.ret(head)

    def _mutex_lock(self):
        m = self.mutex.setdefault(self.r(0), [0, 0, 0])
        m[0] += 1
        m[2] += 1
        self.ret(0)

    def _mutex_unlock(self):
        m = self.mutex.setdefault(self.r(0), [0, 0, 0])
        m[1] += 1
        m[2] -= 1
        self.ret(0)

    def _uart_print(self):
        self.ret(0)

    # OSD palette and layers (finding 17): the setter writes the shadow table and the
    # hidden hardware bank and sets PAL_PENDING; the bank call shows a bank.
    def _osd_state(self):
        self.ret((self.pal_shown << 24) | (self.pal_shown << 26))

    def _hdmi_state(self):
        self.ret(self.pal_shown << 25)

    def _pal_get(self):
        i = self.r(0)
        if i > 255:
            raise EmuError(f'palette get index {i}')
        self.ret(self.pal_shadow[i])

    def _pal_set(self):
        v, i = self.r(0), self.r(1)
        if i > 255:
            raise EmuError(f'palette set index {i}')
        self.pal_shadow[i] = v
        self.pal_banks[1 - self.pal_shown][i] = v
        self.pal_sets.append((i, v))
        self.w32(A_PAL_PENDING, 1)
        self.ret(0)

    def _pal_bank(self):
        dev, bank = self.r(0), self.r(1)
        if dev != 3 or bank > 1:
            raise EmuError(f'palette bank ({dev}, {bank}) not modelled')
        # An entry switching between its stock colour and an overlay colour while the
        # shown OSD buffer already (opening) or still (closing) has pixels using it
        # makes them flash in the stock colour (the green filmstrip flash, 2026-11-01).
        new, old = self.pal_banks[bank], self.pal_banks[self.pal_shown]
        stock = self.pal_stock
        flip = {i for i in range(224, 252) if new[i] != old[i] and stock[i] in (new[i], old[i])}
        if flip and 3 in self.layer:
            n = sum(1 for p in self.rd(self.layer[3], 640 * 480) if p in flip)
            if n:
                self.pal_flashes.append((sorted(flip), n))
        self.pal_shown = bank
        self.ret(0)

    def _hdmi_pal_bank(self):
        raise EmuError('HDMI palette bank: the harness models the LCD only')

    def _set_layer(self):
        dev, layer, cfg = self.r(0), self.r(1), self.r(2)
        if dev != 0:
            raise EmuError(f'set_layer on device {dev}: the harness models the LCD only')
        self.layer[layer] = self.u32(cfg)
        self.ret(0)

    def _msg_post(self):
        self.msgs.append((self.r(0), self.r(1), self.r(2)))
        self.ret(0)

    def _adc_read(self):
        ch, out = self.r(0), self.r(1)
        self.w32(out, self.adc[ch])
        self.ret(0)

    def _layer_on(self):
        if self.r(0) != 0:
            raise EmuError(f'layer query on device {self.r(0)}: the harness models the LCD only')
        self.ret(self.layers_on.get(self.r(1), 0))

    def _power_temp(self):
        self.ret((self.temps[self.r(0)] + 273) * 8)

    # developer console (src/console.c): shell, printf hook slot, kernel task/pool queries
    def _shell_execute(self):
        self.shell_lines.append(self.cstr(self.r(0)))
        self.ret(self.shell_rc)

    def _shell_result(self):
        if self.shell_result is None:
            return self.ret(MASK)                      # -1: no result text
        self.wr(self.r(0), self.shell_result.encode('latin-1') + b'\0')
        self.ret(0)

    def _console_hook(self):
        self.w32(A_CONSOLE_HOOK_SLOT, self.r(0))
        self.ret(0)

    def _ref_tsk(self):
        t = self.rtos_tasks.get(self.r(0))
        if t is None:
            return self.ret(MASK - 4)                  # E_NOEXS (negative)
        self.wr(self.r(1), struct.pack('>HH', t['stat'], t['pri']) + bytes(28))
        self.ret(0)

    def _ref_mpl(self):
        self.wr(self.r(1), struct.pack('>hhII', 0, 0, 0x123400, 0x100000))
        self.ret(0)

    def _recv(self):
        if not self.sock_in:
            return self.ret(0)                         # peer closed
        chunk = self.sock_in.pop(0)[:self.r(2)]
        self.wr(self.r(1), chunk)
        self.ret(len(chunk))

    def _send(self):
        self.sock_out += self.rd(self.r(1), self.r(2))
        self.ret(self.r(2))

    def _sock_close(self):
        self.closed_fds.append(self.r(0))
        self.ret(0)

    def _stock_press(self):
        self.stock_calls.append(('press', self.r(0)))
        self.ret(0)

    def _stock_hold(self):
        self.stock_calls.append(('hold',))
        self.ret(0)

    def _stock_msg_handler(self):
        self.stock_calls.append(('msg', self.u32(self.r(0)), self.u32(self.r(0) + 4)))
        self.ret(0)

    def _stock_iq_ready(self):
        self.stock_calls.append(('iq_ready',))
        self.ret(0)

    def _stock_run_ttl(self):
        self.stock_calls.append(('run_ttl', self.cstr(self.r(0))))
        self.ret(-1)                    # no Shutdown.ttl on the card

    # ---- boot ----------------------------------------------------------------------------------
    def boot(self, loader=False, on_hook=None):
        """fload stage 1, `ex 0xA0017001`, resolve stage 2 at its load address.
        loader=True: boot through the flashed debug loader's ABI instead (loader_hooks over the
        shooting order, JobSystem step 13 done, then cfw_ldr_start as the CFWldr task runs it)."""
        s1 = open(f'{self.cfw_dir}/bin/{self.variant}/gr2_cfw.bin', 'rb').read()
        if len(s1) > WINDOW_SIZE:
            raise EmuError('stage 1 larger than the window')
        self.put_file('H:/cfw/gr2_cfw.bin', s1)
        self.put_file('H:/cfw/gr2_main.bin', self.inputs_main())
        self.wr(WINDOW, s1)
        if loader:
            self.loader_hooks(LOADER_SHOOTING, on_hook)
            self.boot_step_done(13)
            rc = self.run_ldr_task()
        else:
            rc = self.call_addr(self.syms1['_start'] | 1, stack=SCRIPT_TASK_STACK, task='Script')
        boot = self.syms1['g_boot']           # cfw_boot_t passed to stage 2 (src/boot.c)
        base = self.u32(boot)
        if base == 0:
            raise EmuError(f'stage 2 not loaded (rc {rc:#x}); log: {self.log_lines()}')
        if base != HEAP_LO:
            raise EmuError(f'stage 2 at {base:#x}, expected the first heap block {HEAP_LO:#x}')
        self.main_base = base
        self.syms = Symbols(f'{self.cfw_dir}/bin/{self.variant}/gr2_main.elf', base)
        return rc - (1 << 32) if rc & 0x80000000 else rc

    def loader_hooks(self, order, on_hook=None):
        """What the armed stub v2 does (loader/flash/README.md §3): sections load in `order`; at section 3
        it decides (stage 1 in the window -> ldr_state = 5), and from then on after every section load it
        calls *(window+8)(n, &ldr_state) while the state is 5. on_hook(machine, n) runs after each hook
        (tests inspect and change the boot state there). Returns the sections whose hook ran."""
        if self.u32(WINDOW + 4) != LDR_MAGIC:
            raise EmuError('stage 1 has no loader ABI magic')
        hook, ran = self.u32(WINDOW + 8), []
        for n in order:
            if not ran and n != LOADER_DECIDE_SECTION:
                continue
            if not ran:
                self.w32(LDR_STATE, 5)
            if self.u32(LDR_STATE) != 5:
                break
            self.call_addr(hook, n, LDR_STATE, stack=ROOT_TASK_STACK, task='boot')
            ran.append(n)
            if on_hook:
                on_hook(self, n)
        return ran

    def boot_step_done(self, step):
        self.w32(self.ui_state + 0x54 + 4 * step, 1)

    def run_ldr_task(self):
        """Run the CFWldr task body (cfw_ldr_start) on the stack size stage 1 created it with."""
        t = [t for t in self.tasks if t['name'] == 'CFWldr']
        if len(t) != 1:
            raise EmuError(f'CFWldr created {len(t)} times')
        return self.call_addr(self.syms1['cfw_ldr_start'] | 1, stack=t[0]['stack'], task='CFWldr')


DEFAULT_PROPS = {
    0x001: 1,            # UI mode (shooting)
    0x002: 0,            # connect mode (none)
    0x005: 2,            # shooting mode P
    0x006: 0,            # no MY slot
    0x021: 0,            # crop off
    0x02D: 0,            # colour mode Standard
    0x0CE: 7,            # Clarity strength
    0x0EF: 4, 0x0F0: 0xF0, 0x0F1: 0x1A0, 0x0F2: 0x200,   # NR Auto, thresholds
    0x111: 0x1E0,        # ISO Auto-High max (ISO 3200)
    0x113: 0x989681,     # Auto-High slowest shutter: Auto
    0x1A6: 0, 0x1A7: 0,  # Effect button / hold
    0x1E4: 1,            # English
    0x1E8: 0,            # Wi-Fi request
    0x1C3: 1, 0x1C4: 1, 0x1C5: 1,   # MY slot box links
    0x234: 0x03000000,   # firmware 3.00
    0x244: 0x12345678,   # serial
    0x24A: 3,            # battery FULL
    0x254: 0x01E00280,   # OSD 640x480
    0x260: 1,            # Wi-Fi AP up
    0x2D9: 999,          # shots left
    0x2DC: 31914983424 >> 32, 0x2DD: 31914983424 & MASK,   # card total
    0x2DE: 12884901888 >> 32, 0x2DF: 12884901888 & MASK,   # card free
}
