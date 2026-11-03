#!/usr/bin/env python3
"""Emulator smoke test of the CFW (both stages, real code) against b01firm8.

    python3 tests/emu/run.py [--variant release|debug|rom] [--render OUT_DIR]

Boots stage 1 like `fload` + `ex`, loads stage 2 from the modelled H: drive,
then drives the CFW task functions through the scenarios below. The rom
variant (release firmware, M11) runs its own scenario: boots through a model
of the release stub with the CFW on the A: drive (scripts/rom_files.py). Exit 0 when
all checks pass, 1 on a failure or missing inputs. Inputs (`make fw`, not in
the repo):
    GR2_FW          firmware image (default fw/b01firm8.bin)
    <fw dir>/afs    isp/IQ.bin, fix/AE_F.bin (RES drive files next to the image)
"""
import argparse
import os
import pathlib
import re
import struct
import sys
import traceback
import types

HERE = pathlib.Path(__file__).resolve().parent
CFW = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(1, str(CFW / 'scripts'))

KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, KEY_EFFECT = 0x4206, 0x4207, 0x4208, 0x4209, 0x420A, 0x421C
MENU_SETTINGS, MENU_RECIPES = 0, 1
TABS = ['EXPOSURE', 'FOCUS', 'VIEW', 'IMAGE', 'INFO', 'DEV']
DOT = '\x86'                                   # GLYPH_DOT in the CFW fonts
NONE = 0xFFFF
TONE_SECONDARY = [NONE, 1, 2, NONE, 1, 2]      # src/look.c
LOOK_MATRIX, LOOK_TONE, LOOK_POST, LOOK_HUE = 1, 2, 4, 8
LOOK_BASES = {'standard': 0, 'vivid': 1, 'setting1': 3, 'setting2': 4}   # colour mode (prop 0x2D)
K5 = {0: 0, 1: 1, 3: 2, 4: 3}                  # colour mode -> IQ mode (look_mode)
# Every slot property a recipe can set (src/recipes.c recipe_props, finding 19): EFFECT, IMAGE_SETTINGS, the
# image-setting arrays of all colour modes, NR and its custom thresholds, EV comp, WB mode, WB comp per WB mode,
# Kelvin, ISO and the ISO AUTO-HI max / min / slowest shutter, DR comp.
STOCK_PROPS = frozenset([0x2B, 0x2C, *range(0x2E, 0xEE), *range(0xEF, 0xF3), *range(0xFD, 0x114), 0x130])
AE_OFF = [0xBE, 0xC4, 0xC2, 0x2C8, 0xCA, 0xCC]
AE_STOCK = [0x55, 0x30, 0x30, 0x50, 0x00, 0xFFD0]
SHOT_BYTES = 54 + 640 * 480 * 3


# ---- inputs ---------------------------------------------------------------------------------------

# Stock OSD palette (0x0AYYUUVV): the ROM table at 0xA04EA83C. A hardware dump (finding 17) differs
# only in entry 0 (cleared to transparent at init) and entry 241.
ROM_PALETTE = 0xA04EA83C
PALETTE_HW = {0: 0x00000000, 241: 0x062D807F}


def load_inputs():
    fw = pathlib.Path(os.environ.get('GR2_FW', CFW / 'fw' / 'b01firm8.bin'))
    afs = fw.parent / 'afs'
    need = {'firmware': fw, 'iq': afs / 'isp' / 'IQ.bin', 'ae_f': afs / 'fix' / 'AE_F.bin'}
    missing = [str(p) for p in need.values() if not p.is_file()]
    if missing:
        return None, missing
    inp = types.SimpleNamespace(**{k: p.read_bytes() for k, p in need.items()})
    off = ROM_PALETTE - 0xA0020200
    pal = list(struct.unpack('>256I', inp.firmware[off:off + 1024]))
    for i, v in PALETTE_HW.items():
        pal[i] = v
    inp.palette = tuple(pal)
    return inp, []


# ---- recipes as the CFW should see them ----------------------------------------------------------

def recipe_info(file, data):
    """What the CFW should make of recipe file `file` with bytes `data`."""
    kv = dict(re.findall(r'^\s*(\w+)\s*=\s*(.*?)\s*$', data.decode('ascii').split('\x00')[0], re.M))
    flags = 0
    if kv.get('base') in LOOK_BASES:
        flags |= LOOK_MATRIX if 'matrix' in kv else 0
        flags |= LOOK_POST if 'post_matrix' in kv else 0
        flags |= LOOK_TONE if any(k == 'tone' or k.startswith('tone_') for k in kv) else 0
        flags |= LOOK_HUE if any(re.fullmatch(r'hue\d+', k) for k in kv) else 0
    mat = [int(x) for x in kv['matrix'].split(',')] if 'matrix' in kv else None
    return dict(file=file, sfn=file.upper(), name=kv.get('name', ''), base=kv.get('base'),
                mode=LOOK_BASES.get(kv.get('base')), flags=flags, matrix=mat)


def by_name(recipes):
    """src/recipes.c sort_list: case-insensitive by name, stable."""
    return sorted(recipes, key=lambda r: r['name'].lower())


def shipped_recipes():
    return by_name(recipe_info(p.name, p.read_bytes()) for p in sorted((CFW / 'recipes').glob('*.txt')))


def test_recipe(name, body):
    """A recipe file built here (the repository ships only the film recipes): Setting2, neutral steps."""
    return (f'# gr2_cfw test recipe\r\nname={name}\r\nbase=setting2\r\nsaturation=0\r\ncontrast=0\r\nsharpness=0\r\n'
            f'vignetting=off\r\n{body}').encode('ascii')


# Look-stage test recipes (the former hand-written recipes/lvtest.txt, t_*.txt): every look key the parser takes.
HUE9 = '38,75,15,38,75,15,38,75,15'
TEST_RECIPES = {
    'lvtest.txt': test_recipe('~Test R-B swap', 'matrix=0,0,128,0,128,0,128,0,0\r\n'),
    't_curve.txt': test_recipe('~Test curve red', 'tone_r=' + ','.join(['48'] * 33) + '\r\n'),
    't_tone.txt': test_recipe('~Test curve all', 'tone=' + ','.join(str(min(255, 8 * i)) for i in range(33)) + '\r\n'),
    't_hueq1.txt': test_recipe('~Test hue Q1', ''.join(f'hue{k}={HUE9}\r\n' for k in range(4, 13))),
    't_hueq4.txt': test_recipe('~Test hue Q4', ''.join(f'hue{k}={HUE9}\r\n' for k in range(1, 10))),
    't_post.txt': test_recipe('~Test post sat x2', 'post_matrix=218,-75,-15,-38,181,-15,-38,-75,241\r\n'),
}


# ---- checks -----------------------------------------------------------------------------------------

class Failed(Exception):
    pass


class Checks:
    def __init__(self):
        self.failures = []
        self.scenario = ''

    def __call__(self, cond, msg):
        if not cond:
            self.failures.append(f'{self.scenario}: {msg}')
            raise Failed(msg)


# ---- session helpers ------------------------------------------------------------------------------

class Session:
    """One emulated camera: boot + the CFW task's start sequence (src/task.c)."""

    def __init__(self, ctx, files=None, props=None, slots=None, loader=False, on_hook=None, drop=(), cfw_files=True,
                 card=True, a_extra=None):
        """cfw_files: the card carries cfw/recipes and cfw/fonts (the SD zip); card=False: no card at all;
        a_extra: files added to the rom variant's A: drive."""
        import machine
        self.ctx = ctx
        self.m = m = machine.Machine(ctx.inputs, ctx.variant, str(CFW), files=files, props=props, slots=slots,
                                     card=card, a_extra=a_extra)
        ctx.machines.append(m)
        for p in sorted((CFW / 'recipes').glob('*.txt')) if cfw_files else ():
            m.put_file('H:/cfw/recipes/' + p.name, p.read_bytes())
        for p in sorted((CFW / 'recipes' / 'licenses').glob('*')) if cfw_files else ():
            m.put_file('H:/cfw/recipes/licenses/' + p.name, p.read_bytes())
        for p in sorted((CFW / 'fonts').glob('*.fnt')) if cfw_files else ():
            m.put_file('H:/cfw/fonts/' + p.name, p.read_bytes())
        for path in drop:                       # card files that are gone in this session
            del m.files[path.upper()]
        self.rc = m.boot(loader=loader, on_hook=on_hook)
        task = [t for t in m.tasks if t['name'] == 'CFW']
        ctx.check(len(task) == 1 and task[0]['entry'] & ~1 == m.syms['cfw_task'] & ~1, 'CFW task not created')
        m.boot_step_done(10)
        m.call('cfw_task_poll')            # the CFW task's first iteration with every boot phase open

    def keys(self, *ks):
        for k in ks:
            self.m.call('cfw_ui_key', k, 1)
            self.m.call('cfw_ui_key', k, 0)

    def request(self, menu):
        self.m.call('cfw_ui_request', menu)

    def is_open(self):
        return self.m.call('cfw_ui_is_open') != 0

    def setting(self, i):
        return self.m.cstr(self.m.call('settings_value', i))

    def info(self, page, row):
        return self.m.cstr(self.m.call('info_value', page, row))

    def render(self, name):
        if self.ctx.renderer:
            self.ctx.renderer.osd(self.m, f'{self.ctx.variant}_{name}')

    # IQ lists (finding 20 section B)
    def list_addr(self, d):
        import machine
        return machine.IQ_BASE + self.m.u16(machine.IQ_BASE + 0x90 + 2 * d)

    def entries(self, k5):
        m = self.m
        mat, hue, tone, ycc = self.list_addr(9), self.list_addr(0xA), self.list_addr(0xC), self.list_addr(0xD)
        return dict(
            mat=[(m.u16(mat + 4 * (3 * k5 + j)), m.u16(mat + 4 * (3 * k5 + j) + 2)) for j in range(3)],
            hue=(m.u16(hue + 4 * k5), m.u16(hue + 4 * k5 + 2)),
            tone=[(m.u16(tone + 4 * (c + 18 * k5 + 342 * t)), m.u16(tone + 4 * (c + 18 * k5 + 342 * t) + 2))
                  for t in (0, 3) for c in range(6)],
            ycc=[m.u16(ycc + 2 * (r + 18 * k5)) for r in range(18)])

    @staticmethod
    def expected_entries(flags):
        return dict(
            mat=[(11 if flags & LOOK_MATRIX else 0, NONE)] * 3,
            hue=(6 if flags & LOOK_HUE else 0, NONE),
            tone=[((30, NONE if s == NONE else 30 + s) if flags & LOOK_TONE else (0, s))
                  for _t in (0, 3) for s in TONE_SECONDARY],
            ycc=[34 + r if flags & LOOK_POST else r for r in range(18)])

    def look_shown(self, k5_flags):
        """{k5: flags} for modes with a look, every other IQ mode 0..3 stock."""
        return all(self.entries(k5) == self.expected_entries(k5_flags.get(k5, 0)) for k5 in range(4))

    def iq_stock_except_sections(self):
        import machine
        cur = self.m.rd(machine.IQ_BASE, len(self.m.iq_image))
        a, b = cur[:0x24] + cur[0x84:], self.m.iq_image[:0x24] + self.m.iq_image[0x84:]
        return a == b

    def recipe_names(self):
        """The Recipes screen's list (src/ui.c), Stock included."""
        m = self.m
        names, n = m.u32(m.syms['g_recipes']), m.u32(m.syms['g_recipe_n'])
        return [m.cstr(m.u32(names + 4 * i)) for i in range(n)]

    def recipe_sel(self):
        return self.m.u32(self.m.syms['g_recipe_sel'])


def finish(ctx, s):
    """Invariants every scenario must leave behind."""
    m, check = s.m, ctx.check
    check(not m.handles, f'file handles left open: {[h[0] for h in m.handles.values()]}')
    check(not m.finds, 'directory handles left open')
    check(m.pdb_open == 0, 'property DB clone not freed')
    check(m.prop_lock_depth == 0, f'property lock depth {m.prop_lock_depth} after the scenario')
    check(not m.prop_set_unlocked, f'Prop_Set outside the property lock: {[hex(p) for p in m.prop_set_unlocked]}')
    for a, (lk, ul, depth) in m.mutex.items():
        check(lk == ul and depth == 0, f'mutex {a:#x}: {lk} locks, {ul} unlocks')
    if ctx.variant == 'debug':
        errs = [l for l in m.log_lines() if 'ERROR' in l]
        check(not errs, f'ERROR lines in the debug log: {errs}')


# ---- scenarios ------------------------------------------------------------------------------------------

def encode_bl(site, target):
    off = (target & ~1) - (site + 4)
    return 0xF000 | ((off >> 12) & 0x7FF), 0xF800 | ((off >> 1) & 0x7FF)


def sc_boot(ctx):
    """Boot, hooks, veneers, power-off status (debug), menu labels via the stock resolver, shell record, key input
    hook."""
    import machine
    check = ctx.check
    s = Session(ctx)
    m = s.m
    check(s.rc == 0, f'_start returned {s.rc}')
    for site, orig, ven, tgt, hook in ((0xA03A1F92, 0xA039607C, 'cfw_veneer_press', 'cfw_veneer_press_target',
                                        'cfw_press_hook'),
                                       (0xA03A5790, 0xA0396EA0, 'cfw_veneer_hold', 'cfw_veneer_hold_target',
                                        'cfw_hold_hook')):
        veneer = m.syms1[ven] & ~1
        ohi, olo = encode_bl(site, orig)
        nhi, nlo = encode_bl(site, veneer)
        check(nlo == olo, f'{ven} does not share the low bits of {orig:#x}')
        img = ctx.inputs.firmware[site - machine.FW_BASE:site - machine.FW_BASE + 4]
        check(img == struct.pack('>HH', ohi, olo), f'stock BL at {site:#x} not as expected in the image')
        check((m.u16(site), m.u16(site + 2)) == (nhi, nlo), f'BL at {site:#x} not retargeted to {veneer:#x}')
        check(m.u32(m.syms1[tgt]) == m.syms[hook] | 1, f'{tgt} != {hook}')
    # Effect press through the patched stock BL: CFW values count, others reach the stock function.
    for value, menu in ((0x20, 0), (0x21, 1), (0x05, None)):
        before = [m.call('cfw_hook_requests', k) for k in (0, 1)]
        m.run_range(0xA03A1F93, 0xA03A1F96, value)
        after = [m.call('cfw_hook_requests', k) for k in (0, 1)]
        if menu is None:
            check(after == before and m.stock_calls[-1] == ('press', value), 'press fall-through broken')
        else:
            check(after[menu] == before[menu] + 1, f'press value {value:#x} not counted')
    for value, menu in ((2, 0), (3, 1), (0, None)):
        m.props[0x1A7] = value
        before = [m.call('cfw_hook_requests', k) for k in (0, 1)]
        n_stock = len(m.stock_calls)
        m.run_range(0xA03A5791, 0xA03A5794)
        after = [m.call('cfw_hook_requests', k) for k in (0, 1)]
        if menu is None:
            check(after == before and m.stock_calls[n_stock:] == [('hold',)], 'hold fall-through broken')
        else:
            check(after[menu] == before[menu] + 1, f'hold value {value} not counted')
    # Power-off job (FRM task): debug logs the session state, then the stock Shutdown.ttl call runs either way.
    off_site, off_orig = 0xA00E9950, 0xA00EA1D4
    stock_off = encode_bl(off_site, off_orig)
    patched = (m.u16(off_site), m.u16(off_site + 2))
    n_log = len(m.log_lines())
    rc = m.run_range(0xA00E994F, off_site + 4, stack=machine.FRM_JOB_STACK, task='FRM')
    new = m.log_lines()[n_log:]
    check(m.stock_calls[-1] == ('run_ttl', '*:/Shutdown.ttl') and rc == 0xFFFFFFFF, 'stock Shutdown.ttl call lost')
    if ctx.variant == 'debug':
        check(patched == encode_bl(off_site, m.syms1['cfw_veneer_off']), f'power-off BL not retargeted: {patched}')
        check(new[:1] == ['status: power-off'] and any(l.startswith('status: task heartbeat') for l in new),
              f'power-off status not logged: {new[:3]}')
    else:
        check(patched == stock_off and not new, 'release build touched the power-off job')
    # Menu choices: labels resolve through the stock (bound-patched) string accessor.
    for sid, label in ((0x354, 'CFW Menu'), (0x35E, 'Recipes')):
        got = m.ustr(m.call_addr(machine.STRING_RESOLVER, sid))
        check(got == label, f'string {sid:#x} resolves to {got!r}')
    check(m.ustr(m.call_addr(machine.STRING_RESOLVER, 0x1A2)) == 'Name', 'stock string 0x1A2 lost')
    efx = 0xA04330D8 + 0xFC * 0x40
    l1 = m.u32(efx + 0x10)
    check(m.u16(efx + 0x0C) == 34 and [m.u32(l1 + 12 * i) for i in (32, 33)] == [0x20, 0x21],
          'Effect menu choices 0x20/0x21 missing')
    # Shell: one record "cfw" appended.
    check(m.u32(machine.A_SHELL_CTX + 8) == 923, 'shell count not incremented')
    node = m.u32(machine.A_SHELL_CTX + 4)
    while m.u32(node + 4):
        node = m.u32(node + 4)
    check(m.cstr(m.u32(node)) == 'cfw', 'shell record "cfw" not appended')
    # Key input: pass-through while closed, queued and consumed while the overlay is open.
    slot = m.u32(machine.A_MSG_HANDLER_SLOT)
    check(slot == m.syms['msg_hook'] | 1, 'message handler slot not swapped')
    msg = machine.OBJ_HI - 0x20
    m.wr(msg, struct.pack('>3I', KEY_RIGHT, 1, 0))
    check(m.call_addr(slot, msg) == 0 and m.stock_calls[-1] == ('msg', KEY_RIGHT, 1), 'closed: key not passed on')
    s.request(MENU_SETTINGS)
    check(s.is_open(), 'CFW Menu did not open')
    check(m.call_addr(slot, msg) == 1, 'open: key press not consumed')
    m.wr(msg, struct.pack('>3I', KEY_RIGHT, 0, 0))
    check(m.call_addr(slot, msg) == 1, 'open: key release not consumed')
    out = machine.OBJ_HI - 0x10
    got = []
    while m.call('cfw_input_pop', out, out + 4):
        got.append((m.u32(out), m.u32(out + 4)))
    check(got == [(KEY_RIGHT, 1), (KEY_RIGHT, 0)], f'queued keys {got}')
    s.keys(KEY_EFFECT)
    check(not s.is_open(), 'Effect did not close the CFW Menu')
    finish(ctx, s)


def sc_menu(ctx):
    """CFW Menu: all tabs, Auto ISO AE_F writes and their stock restore, property settings, INFO."""
    import machine
    check = ctx.check
    s = Session(ctx)
    m = s.m
    aef = lambda: [m.u16(machine.A_AEF + o) for o in AE_OFF]
    check(aef() == AE_STOCK, f'AE_F not stock at start: {aef()}')
    s.request(MENU_SETTINGS)
    check(s.is_open() and m.layer.get(3) == machine.OSD_BITMAPS[1], 'menu did not take the OSD back buffer')
    frames = {}
    for name in TABS:
        frames[name] = m.rd(m.layer[3], 640 * 480)
        s.render(f'menu_{name.lower()}')
        s.keys(KEY_RIGHT)
    check(len(set(frames.values())) == len(TABS), 'tabs do not render differently')
    check(frames['EXPOSURE'] == m.rd(m.layer[3], 640 * 480), 'Right on the last tab does not wrap')
    # EXPOSURE rows by function: Auto ISO max, slowest, at max ISO, in M; Auto-Hi slowest; TAv cap, TAv minimum
    s.keys(KEY_DOWN, KEY_DOWN, KEY_RIGHT)
    check(s.setting(1) == '1/60 + crop' and aef() == [0x60, 0x30, 0x30, 0x50, 0, 0xFFD0], f'aisomin: {aef()}')
    s.keys(KEY_UP, KEY_RIGHT, KEY_OK)                      # OK does not step a multi-value setting
    check(s.setting(2) == 'ISO 400' and aef() == [0x60, 0x20, 0x20, 0x20, 0, 0xFFD0], f'aisomax: {aef()}')
    s.keys(KEY_DOWN, KEY_DOWN, KEY_OK)                     # OK toggles a two-value setting
    check(s.setting(3) == 'KEEP SHUTTER' and aef() == [0x60, 0x20, 0x20, 0x20, 0x60, 0x60], f'aisohold: {aef()}')
    m.props[0x21] = 2                                  # 47 mm crop: 1/100
    m.call('settings_poll')
    check(aef()[0] == (0x140 * 0xAAAB) >> 17, f'crop 47 mm: slowest shutter {aef()[0]:#x}, want 1/100')
    m.props[0x21] = 0
    m.props[5] = 7                                     # Movie: stock limits
    m.call('settings_poll')
    check(aef() == AE_STOCK, f'Movie mode not stock: {aef()}')
    m.props[5] = 2
    m.call('settings_poll')
    check(aef() == [0x60, 0x20, 0x20, 0x20, 0x60, 0x60], 'P mode limits not back after Movie')
    s.render('menu_exposure_aiso')
    s.keys(KEY_LEFT, KEY_UP, KEY_LEFT, KEY_UP, KEY_LEFT)   # hold, slowest, max back to STOCK
    check(aef() == AE_STOCK, f'AE_F not stock after STOCK: {aef()}')
    # Auto-Hi slowest shutter (prop 0x113): Auto, 1/2, back to STOCK restores the camera value.
    s.keys(KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_RIGHT, KEY_RIGHT)
    check(m.props[0x113] == 0x30 and s.setting(8) == '1/2', 'aisotv 1/2 not written')
    s.keys(KEY_LEFT, KEY_LEFT)
    check(m.props[0x113] == 0x989681, 'aisotv not restored')
    s.keys(KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP)
    # IMAGE: Clarity 1, then STOCK
    s.keys(KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_DOWN, KEY_RIGHT)
    check(m.props[0xCE] == 1, 'clarity not written')
    s.render('menu_image')
    s.keys(KEY_LEFT)
    check(m.props[0xCE] == 7, 'clarity not restored')
    s.keys(KEY_UP, KEY_RIGHT)
    # INFO
    want = {0: '12345', 1: '13002', 2: f'FULL  {DOT}  AD 641', 3: f'sensor 38 C  {DOT}  lens 31 C',
            4: f'12.0 of 29.7 GB free  {DOT}  999 shots', 5: f'3.00  {DOT}  serial 12345678', 7: '811'}
    for row, v in want.items():
        check(s.info(0, row) == v, f'INFO row {row}: {s.info(0, row)!r} != {v!r}')
    before = m.rd(m.layer[3], 640 * 480)
    m.adc[6] = 1200
    m.call('cfw_ui_tick')
    after = m.rd(m.layer[3], 640 * 480)
    rows = sorted({i // 640 for i in range(len(after)) if after[i] != before[i]})
    check(rows and 60 + 2 * 38 <= rows[0] and rows[-1] < 60 + 2 * 38 + 36, f'INFO tick redrew rows {rows[:1]}..')
    s.keys(KEY_EFFECT)
    check(not s.is_open() and m.layer[3] == machine.OSD_BITMAPS[0], 'stock OSD buffer not back after close')
    cfg = m.cfg_text()
    check('aisomin=00000000' in cfg and 'clarity.s=00000007' in cfg, f'config not saved to D:: {cfg!r}')
    check((m.file('H:/cfw/config.txt') or b'').decode() == cfg, 'card backup copy differs from D:')
    finish(ctx, s)


def sc_snap(ctx):
    """FOCUS: Snap distances 0.7m + 1.2m (1m -> 0.7 m, 1.5m -> 1.2 m), back to STOCK (finding 18 section 4)."""
    import machine
    check = ctx.check
    s = Session(ctx)
    m = s.m
    sites = {0xA06FD5E2: 2, 0xA06FD5E8: 2, 0xA0700066: 2, 0xA070006C: 2, 0xA015F3EC: 4, 0xA01BE978: 4,
             0xA015F19C: 2, 0xA015F19A: 2, 0xA01BE584: 2, 0xA01BE582: 2, 0xA01BD75C: 2, 0xA01BD75A: 2,
             0xA01BD760: 2, 0xA01BD762: 2}
    rd = lambda a: m.u32(a) if sites[a] == 4 else m.u16(a)
    stock = {a: rd(a) for a in sites}
    label = lambda sid: m.ustr(m.call_addr(machine.STRING_RESOLVER, sid))
    check((label(0x42), label(0x43)) == ('1m', '1.5m'), 'stock snap labels')
    s.request(MENU_SETTINGS)
    s.keys(KEY_RIGHT, KEY_DOWN, KEY_RIGHT)
    check(s.setting(4) == '0.7m + 1.2m', f'snap = {s.setting(4)}')
    for a in (0xA06FD5E2, 0xA0700066):
        check(rd(a) == 0x5B8, f'{a:#x} = {rd(a):#x}, want 0x5B8')
    for a, v in ((0xA015F19A, 0x20B7), (0xA01BE582, 0x24B7), (0xA01BD75A, 0x26B7)):
        check(rd(a) == v, f'{a:#x} = {rd(a):#x}, want {v:#x}')
    for a in (0xA06FD5E8, 0xA070006C, 0xA015F3EC, 0xA01BE978):
        check(rd(a) == 0x358, f'{a:#x} = {rd(a):#x}, want 0x358')
    check(rd(0xA01BD760) == 0x266B and rd(0xA01BD762) == 0xE7FB, 'OSD path 1 1.5 m patch')
    check((label(0x42), label(0x43)) == ('0.7m', '1.2m'), f'labels {label(0x42)!r} {label(0x43)!r}')
    s.render('menu_focus_snap')
    s.keys(KEY_LEFT)
    bad = {hex(a): hex(rd(a)) for a in sites if rd(a) != stock[a]}
    check(not bad, f'snap sites not stock: {bad}')
    check((label(0x42), label(0x43)) == ('1m', '1.5m'), 'snap labels not restored')
    s.keys(KEY_EFFECT)
    finish(ctx, s)


def sc_recipes(ctx):
    """Recipes: preview repoints Setting2 lists, close restores, Stock last, apply to MY2, dial, reboot; Stock on MY2
    (dial on it): previewed as stock over the slot's look, every recipe property and the name back to the factory
    values in the slot file and live, recipe forgotten, no look (also after a reboot)."""
    import machine
    check = ctx.check
    rec = ctx.recipes
    s = Session(ctx, props={0x2D: 4})
    m = s.m
    check(s.look_shown({}) and s.iq_stock_except_sections(), 'IQ not stock before the overlay')
    s.request(MENU_RECIPES)
    check(s.look_shown({3: rec[0]['flags']}), f'preview of {rec[0]["name"]} not in Setting2')
    for i in range(1, len(rec)):
        ev = m.u32(machine.A_LV_CACHE_EV)
        s.keys(KEY_RIGHT)
        check(s.look_shown({3: rec[i]['flags']}), f'preview of {rec[i]["name"]} not in Setting2')
        check(m.u32(machine.A_LV_CACHE_EV) != ev, 'live-view change cache not flipped')
    s.keys(KEY_RIGHT)
    check(s.recipe_sel() == len(rec) and s.recipe_names()[-1] == 'Stock' and s.look_shown({}),
          'Right past the last recipe: not Stock, or Stock previewed with a look')
    s.keys(KEY_RIGHT)
    check(s.recipe_sel() == len(rec), 'Right past Stock moved')
    pi = next(i for i, r in enumerate(rec) if r['file'] == 'portra.txt')
    s.keys(*[KEY_LEFT] * (len(rec) - pi))
    check(s.look_shown({3: rec[pi]['flags']}), 'Left did not return to Portra')
    # CFW matrix record 11 = recipe matrix x stock record 0 (src/look.c write_records)
    sect = m.u32(machine.IQ_BASE + 0x24 + 4 * 8)
    stock0 = struct.unpack('>9h', m.iq_image[0x7B44 + 4:0x7B44 + 22])
    M = rec[pi]['matrix']
    want = [max(-2047, min(2047, (sum(M[3 * i + k] * stock0[3 * k + j] for k in range(3)) + 64) >> 7))
            for i in range(3) for j in range(3)]
    check(machine.HEAP_LO <= sect < machine.HEAP_HI, 'matrix section not published from the heap')
    check(list(struct.unpack('>9h', m.rd(sect + 11 * 0x18 + 4, 18))) == want, 'matrix record 11 wrong')
    s.render('recipes_preview')
    si = next(i for i, r in enumerate(rec) if r['file'] == 'superia.txt')   # two-line name
    s.keys(*[KEY_RIGHT if si > pi else KEY_LEFT] * abs(si - pi))
    s.render('recipes_two_line')
    s.keys(*[KEY_LEFT if si > pi else KEY_RIGHT] * abs(si - pi))
    s.keys(KEY_EFFECT)
    check(s.look_shown({}) and s.iq_stock_except_sections(), 'IQ lists not stock after close')
    sec_copy = m.rd(m.u32(machine.IQ_BASE + 0x24 + 4 * 0xC), 34 * 0x14)
    off = struct.unpack_from('>I', ctx.inputs.iq, 0x24 + 4 * 0xC)[0]
    check(sec_copy == ctx.inputs.iq[off:off + 34 * 0x14], 'stock YCbCr records changed in the copy')
    # Apply Portra to MY2.
    s.request(MENU_RECIPES)
    s.keys(KEY_OK)
    s.render('recipes_apply')
    s.keys(KEY_RIGHT, KEY_OK)
    s.render('recipes_saved')
    slot = m.slots.get('B:/P_MY01.bin')
    check(slot is not None, 'MY2 slot file not written')
    name = b''.join(struct.pack('>I', slot[0x1A9 + c]) for c in range(17)).decode('utf-16-be').rstrip('\0')
    check(slot[0x2B] == 0 and slot[0x2C] == 4 and name == 'Portra 400', f'slot: {slot[0x2B]} {slot[0x2C]} {name!r}')
    check(m.ustr(m.props[0x1BB]) == 'Portra 400' and m.props[0x1C4] == 0, 'MY2 name cache / box link')
    check(not m.prop_sa_written, 'live name written while the dial is not on MY2')
    cfg = m.cfg_text()
    for k, v in (('rc2.0', 'PORT'), ('rc2.1', 'RA.T'), ('rc2.2', 'XT\0\0')):
        check(f'{k}={v.encode().hex().upper()}\r\n' in cfg, f'config {k} != {v!r}: {cfg!r}')
    s.keys(KEY_EFFECT)
    check(s.look_shown({}), 'look left after close')
    m.props[6] = 2
    m.call('recipes_poll')
    check(s.look_shown({3: rec[pi]['flags']}), 'MY2 + Setting2: slot look not shown')
    m.props[0x2D] = 0
    m.call('recipes_poll')
    check(s.look_shown({}), 'MY2 in Standard: look shown although the base is Setting2')
    m.props[0x2D] = 4
    m.props[6] = 0
    m.call('recipes_poll')
    check(s.look_shown({}), 'dial off MY2: look still shown')
    finish(ctx, s)
    # Reboot with that internal drive and slot: the look comes back from D: (config + look cache) ...
    s2 = Session(ctx, files=m.d_files(), slots=m.slots, props={0x2D: 4, 6: 2})
    check(s2.look_shown({3: rec[pi]['flags']}), 'after reboot: MY2 look not restored')
    finish(ctx, s2)
    # ... and from the look cache alone when the recipe file is no longer on the card (no ERROR, no rewrite).
    d = s2.m.d_files()
    s3 = Session(ctx, files=d, slots=m.slots, props={0x2D: 4, 6: 2}, drop=['H:/cfw/recipes/portra.txt'])
    check(s3.look_shown({3: rec[pi]['flags']}), 'recipe file gone: cached MY2 look not shown')
    check(s3.m.d_files() == d, 'D: rewritten although nothing changed')
    finish(ctx, s3)
    # Stock on MY2 with the dial on it. The slot file holds non-factory values in every recipe property.
    dflt = m.prop_defaults()
    check(dflt[0x2E + 1] == 7 and dflt[0x2E + 4] == 5 and dflt[0x46 + 7] == 0x7FFFFFFF and dflt[0xFE] == 1 and
          dflt[0x10F] == 5000 and dflt[0xEF] == 4 and dflt[0x2B] == dflt[0x2C] == 0,
          'firmware defaults table differs from finding 19 section 1.3')
    name_ids = range(0x1A9, 0x1BA)
    marked = [v + 1 if i in STOCK_PROPS or i in name_ids or i in (0xEE, 0x147) else v
              for i, v in enumerate(m.slots['B:/P_MY01.bin'])]
    s4 = Session(ctx, files=d, slots={'B:/P_MY01.bin': marked}, props={0x2D: 4, 6: 2})
    m4 = s4.m
    check(s4.look_shown({3: rec[pi]['flags']}), 'MY2 look not shown before Stock')
    s4.request(MENU_RECIPES)
    s4.keys(*[KEY_RIGHT] * len(rec))
    check(s4.recipe_sel() == len(rec) and s4.look_shown({}), 'Stock previewed with the MY2 look')
    s4.keys(KEY_OK, KEY_RIGHT, KEY_OK)
    slot = m4.slots['B:/P_MY01.bin']
    bad = [hex(i) for i in STOCK_PROPS if slot[i] != dflt[i]]
    check(not bad, f'Stock: slot properties not at the factory value: {bad}')
    check(all(slot[i] == dflt[i] == 0 for i in name_ids), 'Stock: slot name not the factory (empty) one')
    check(all(slot[i] == marked[i] for i in range(899) if i not in STOCK_PROPS and i not in name_ids),
          'Stock changed slot properties a recipe cannot set')
    bad = [hex(i) for i in STOCK_PROPS if m4.props.get(i) != dflt[i]]
    check(not bad, f'Stock with the dial on MY2: live properties not at the factory value: {bad}')
    check(m4.prop_sa_written.get(0x1A9) == bytes(0x44) and m4.ustr(m4.props[0x1BB]) == '' and m4.props[0x1C4] == 0,
          'Stock: live name, MY2 name cache or box link')
    cfg = m4.cfg_text()
    check(all(f'rc2.{w}=00000000' in cfg or f'rc2.{w}=' not in cfg for w in range(3)), f'MY2 recipe kept: {cfg!r}')
    check(s4.look_shown({}), 'Stock previewed with a look after the apply')
    s4.keys(KEY_EFFECT)
    check(s4.look_shown({}), 'Stock on MY2: look still shown')
    finish(ctx, s4)
    s5 = Session(ctx, files=m4.d_files(), slots=m4.slots, props={0x2D: 4, 6: 2})
    check(s5.look_shown({}), 'Stock on MY2: look back after a reboot (config or look cache on D:)')
    finish(ctx, s5)


def sc_palette(ctx):
    """Overlay-owned palette entries 224-251 and the ISP mutex."""
    import machine
    check = ctx.check
    s = Session(ctx, props={0x2D: 4})
    m = s.m
    user = slice(224, 252)
    s.request(MENU_RECIPES)
    touched = {i for i, _ in m.pal_sets}
    check(touched and touched <= set(range(224, 252)), f'palette entries outside 224-251 set: {sorted(touched)}')
    check(m.pal_banks[0][user] == m.pal_banks[1][user] == m.pal_shadow[user], 'banks differ while open')
    check(m.pal_shadow[user] != m.pal_stock[user], 'swatches did not use the overlay entries')
    check(m.u32(machine.A_PAL_PENDING) == 0, 'palette left pending')
    check(not m.pal_flashes, f'overlay pixels shown in stock colours while opening: {m.pal_flashes}')
    n = len(m.pal_sets)
    s.keys(KEY_RIGHT)
    moved = {i for i, _ in m.pal_sets[n:]}
    check(0 < len(moved) <= 12, f'scroll reprogrammed {len(moved)} entries (expected the entering recipe and '
                                f'the two changing selection: <= 12)')
    s.keys(KEY_LEFT)
    s.keys(KEY_EFFECT)
    for name, p in (('shadow', m.pal_shadow), ('bank 0', m.pal_banks[0]), ('bank 1', m.pal_banks[1])):
        check(p == m.pal_stock, f'{name} not stock after close')
    check(not m.pal_flashes, f'overlay pixels shown in stock colours while closing: {m.pal_flashes}')
    isp = m.u32(machine.A_ISP_MUTEX)
    check(m.mutex.get(isp, [0])[0] > 0, 'ISP mutex never taken')
    finish(ctx, s)


def sc_dev(ctx):
    """DEV: console OFF/ON/OFF/ON (Wi-Fi left to the camera menu, stock telnetd not started), IQ dump,
    recipe reload, screenshot."""
    import machine
    check = ctx.check
    s = Session(ctx)
    m = s.m
    m.props[0x260] = 0                                         # Wi-Fi off
    s.request(MENU_SETTINGS)
    s.keys(KEY_LEFT, KEY_DOWN)
    check(s.info(1, 0) == 'OFF' and s.info(1, 8) == '-', f'before start: {s.info(1, 0)!r}')
    s.keys(KEY_OK)
    check(not m.msgs and m.props[0x1E8] == 0, f'Wi-Fi touched: msgs {m.msgs}')
    check([t['name'] for t in m.tasks] == ['CFW', 'CFWcon'], f'tasks: {[t["name"] for t in m.tasks]}')
    check(s.info(1, 0) == f'ON  {DOT}  Wi-Fi off (camera menu)', f'console row, Wi-Fi off: {s.info(1, 0)!r}')
    m.props[0x260] = 1                                         # turned on in the camera menu
    m.w32(m.syms['g_listen'], 3)                               # CFWcon has its listening socket
    check(s.info(1, 0) == f'ON  {DOT}  telnet 192.168.0.1' and s.info(1, 8) == f'GR_A1B2C3  {DOT}  secretpass12',
          f'Wi-Fi row: {s.info(1, 8)!r}')
    m.call('cfw_ui_tick')
    s.render('menu_dev')
    s.keys(KEY_OK)                                             # OFF: socket closed, printf back on the UART
    check(s.info(1, 0) == 'OFF' and m.closed_fds == [3] and m.u32(machine.A_CONSOLE_HOOK_SLOT) == 0,
          f'console off: row {s.info(1, 0)!r}, closed {m.closed_fds}')
    s.keys(KEY_OK)
    check(len(m.tasks) == 2 and s.info(1, 0) != 'OFF', 'console on again: task started twice or still off')
    s.keys(KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_OK)
    dump = m.file('H:/cfw/iq_ram.bin')
    check(dump is not None and len(dump) == 0x2FAA0, f'IQ dump size {len(dump or b"")}')
    check(dump == m.rd(machine.IQ_BASE, 0x2FAA0) and s.info(1, 4) == 'iq_ram.bin saved', 'IQ dump content')
    s.keys(KEY_UP, KEY_UP, KEY_OK)
    check(s.info(1, 2) == f'{len(ctx.recipes)} recipes', f'reload: {s.info(1, 2)!r}')
    s.keys(KEY_UP, KEY_OK)
    check(not s.is_open(), 'screenshot did not close the menu')
    m.call('info_poll')
    check(m.file('H:/cfw/shot0001.bmp') is None, 'screenshot taken before the delay')
    m.time_ms += 1500
    m.call('info_poll')
    bmp = m.file('H:/cfw/shot0001.bmp')
    check(bmp is not None and len(bmp) == SHOT_BYTES, f'screenshot size {len(bmp or b"")}')
    check(bmp == expected_shot(m), 'screenshot pixels differ from LV + shown OSD')
    check(lv_gradient_visible(bmp, m), 'LV gradient not visible in the screenshot')
    if ctx.renderer:
        ctx.renderer.save_bmp(bmp, f'{ctx.variant}_screenshot')
    finish(ctx, s)


def sc_pattern(ctx):
    """DEV test pattern row: OFF, RUNNING until the menu has been closed 1.5 s, ON, then OFF again."""
    check = ctx.check
    s = Session(ctx)
    m = s.m
    s.request(MENU_SETTINGS)
    s.keys(KEY_LEFT, KEY_DOWN, *[KEY_DOWN] * 6)
    check(s.info(1, 6) == 'OFF', f'pattern row: {s.info(1, 6)!r}')
    s.keys(KEY_OK)
    check(not s.is_open() and s.info(1, 6) == 'RUNNING', f'pattern pending: {s.info(1, 6)!r}')
    m.time_ms += 1500
    m.call('info_poll')
    check(s.info(1, 6) == 'ON', f'pattern drawn: {s.info(1, 6)!r}')
    s.request(MENU_SETTINGS)
    s.keys(KEY_OK)
    m.time_ms += 1500
    m.call('info_poll')
    check(s.info(1, 6) == 'OFF', f'pattern removed: {s.info(1, 6)!r}')
    finish(ctx, s)


# ---- halation (src/halation.c, finding 37) ---------------------------------------------------------------------
# The still effect step's call in FUN_a02AED1C (stock F000 FC85) and the frame layout FUN_a02AE7EA gives the YCC buffer
# (pitches, rows, Cb/Cr plane offsets; top/left margins as H1 logged them); the buffer size is the stock type table's.
FX_SITE, FX_SITE_LO = 0xA02AEDC0, 0xFC85
FX_VENEER, FX_TARGET = 0xA001E6CE, 0xA001E6D4
YCC_SIZE, YCC_PITCH_Y, YCC_PITCH_C, YCC_ROWS = 0x01F0E020, 4960, 2496, 0xCC0
YCC_CB, YCC_CR, YCC_TOP, YCC_LEFT = 0xF7A320, 0x1744140, 8, 0x20
FX_MAP, FX_BUF = 0xA1F00000, 0xA2000000        # mapped DRAM: 1 MiB guard, the buffer, guard to the end
FX_MAP_SIZE = 0x2200000
SPRWORK_AT = 0xA4200000                        # the still path's scratch, mapped exactly: any overrun faults
# Frame sizes (ctx+0x258): 3:2, 4:3, 1:1 as H1 logged them [H]; 16:9 and the 35 mm crop are synthetic.
FX_FRAMES = {'3:2': (4928, 3264), '4:3': (4352, 3264), '1:1': (3264, 3264), '16:9': (4928, 2768),
             'crop': (3456, 2304)}
HALO_E = (400, 800)                            # src/halation.c E_LO, E_HI: the source ramp (linear units)
HALO_EBIG = 560                                # src/halation.c E_BIG: a bright area's cells (the boost's fade)
HALO_MID = 100                                 # src/halation.c E_MID: mid grey (linear units), the boost's reference
HALO_FLO = 960                                 # src/halation.c F_LO: the fade at the top of the brightest channel
BIG_S = 8                                      # src/halation.c BIG_S: the bright share's window, 2 BIG_S + 1 cells
HX_OK, HX_MODE, HX_RAW, HX_CONTEXT, HX_NONE, HX_BUSY = 0, 2, 3, 4, 5, 6
HX_NO_BUFFER, HX_NO_CHROMA, HX_BAD_SIZE, HX_OUTSIDE, HX_NO_SCRATCH = 7, 9, 10, 11, 13
REC_STATUS, REC_CELLS, REC_WORDS, REC_DEC = 28, 48, 60, 64   # src/halation.c hx_rec_t: status, cells (rows, groups),
#                                                              words, estimate (decoded groups, halo, sources, boosted)
EST = {k: int(v) for k, v in re.findall(r'#define (EST_\w+|NY1_DEC)\s+(\d+)u?\s',
                                         (CFW / 'src' / 'halation.c').read_text())}
CAM_NS, CAM_MS0 = 12.5, -34     # the camera's ms per emulator instruction (H2 shots vs their counts), intercept


def est_ms(dec, nh, nb):
    """src/halation.c over_budget()'s modelled camera time, ms."""
    rows = 1 if dec > EST['NY1_DEC'] else 2
    return (EST['EST_FIX'] + (EST['EST_DEC'] + rows * EST['EST_ROW']) * dec + EST['EST_CELL'] * nh +
            EST['EST_BOOST'] * nb) / 1000


TEST_HALO = 't_halo.txt'                       # a halation look without the boost (sc_halation)


def halo_keys(path):
    """The halation keys of a recipe file: strength x1000 (R, G, B), size um, boost (ev, range, protect_ev)."""
    kv = dict(recipe_kv(pathlib.Path(path).read_bytes()))
    boost = [float(x) for x in kv.get('halation_boost', '0').split(',')]
    boost += [0.3, 2.0][len(boost) - 1:]
    return dict(halo=tuple(int(x) for x in kv['halation'].split(',')), um=int(kv.get('halation_size', '65')),
                boost=tuple(boost[:3]))


def log_hex(m, label):
    """The 0x values of the last log line starting with `label` (cfw_log_hex/hex2 format)."""
    lines = [l for l in m.log_lines() if l.startswith(label)]
    return [int(v, 16) for v in re.findall(r' 0x([0-9A-F]{8})(?= |$)', lines[-1])] if lines else None


def still_model(m, k5=3):
    """Setting2's still rendering as its IQ lists give it (contrast and saturation 0, sRGB; finding 20 section B):
    the R, G, B tone curves (10-bit linear -> code, made monotone) and the RGB'->YCbCr record (x256)."""
    import numpy as np
    import machine
    lst = lambda d: machine.IQ_BASE + m.u16(machine.IQ_BASE + 0x90 + 2 * d)
    sect = lambda s: m.u32(machine.IQ_BASE + 0x24 + 4 * s)
    rec = sect(0xB) + m.u16(lst(0xC) + 4 * (18 * k5 + 342 * 3)) * 0x1400
    tone = np.stack([np.frombuffer(m.rd(rec + 0x400 * (c + 1), 1024), np.uint8) for c in range(3)])
    ycc = sect(0xC) + m.u16(lst(0xD) + 2 * (4 + 18 * k5)) * 0x14
    return np.maximum.accumulate(tone.astype(np.int64), axis=1), np.array(struct.unpack('>9h', m.rd(ycc, 18)),
                                                                         float).reshape(3, 3)


def ycc_encode(rgb, C):
    """Display RGB' codes (h, w, 3) -> full-range YCbCr 4:2:2 through the record C (x256)."""
    import numpy as np
    v = rgb @ C.T / 256
    q = lambda a: np.clip(np.rint(a), 0, 255).astype(np.uint8)
    pair = (v[:, 0::2, 1:] + v[:, 1::2, 1:]) / 2
    return q(v[..., 0]), q(pair[..., 0] + 128), q(pair[..., 1] + 128)


def ycc_decode(Y, Cb, Cr, C):
    import numpy as np
    up = lambda a: np.repeat(a.astype(float) - 128, 2, axis=1)
    return np.stack([Y.astype(float), up(Cb), up(Cr)], -1) @ np.linalg.inv(C / 256).T


def gauss(img, sigma):
    """Separable Gaussian, edges replicated (an independent stand-in for spektrafilm's fast_gaussian_filter)."""
    import numpy as np
    r = int(4 * sigma) + 1
    x = np.arange(-r, r + 1)
    k = np.exp(-x * x / (2 * sigma * sigma))
    k /= k.sum()
    out = img
    for ax in (0, 1):
        a = np.moveaxis(out, ax, -1)
        p = np.pad(a, [(0, 0), (r, r)], mode='edge')
        out = np.moveaxis(np.stack([np.convolve(row, k, mode='valid') for row in p]), -1, ax)
    return out


def boost_ref(E, boost):
    """spektrafilm's boost_highlights (utils/numba_boost_hightlights.py) with the camera's white (1023) as np.max and
    mid grey HALO_MID; boost = (boost_ev, boost_range, protect_ev)."""
    import numpy as np
    ev, rng, prot = boost
    x0 = min(HALO_MID * 2.0 ** prot, 1023.0)
    if ev <= 0 or x0 >= 1023:
        return E
    a = 28.0 ** (1.0 - rng)
    L = 1.0 - x0 / 1023
    k = (2.0 ** ev - 1.0) / (np.exp(a * L) - a * L - 1.0)
    dx = np.maximum(E - x0, 0) / 1023
    return np.where(E > x0, E + 1023 * k * (np.exp(a * dx) - a * dx - 1.0), E)


def ref_halation(Y, Cb, Cr, tone, C, halo=(300, 100, 15), um=50, boost=(0, 0.3, 2.0)):
    """spektrafilm's halation (3 bounces, decay 0.5, renormalised) on the decoded frame, full resolution, float, exact
    Gaussians, as src/halation.c models it: S = E x gain per channel (the E_LO..E_HI ramp times spektrafilm's boost
    over E, per channel), the boost faded by the share q of bright cells (both samples of row 8i + 2 at E >= E_BIG)
    in the 17 x 17 cells around, a large bright area's source a quarter of its light; x = h - k A with h = a' G * S,
    k = 1.4 a', A the hold of the 3 x 3 cells around (of each cell's max(mean E, mean S): their mean, at most twice
    their least) interpolated between cell centres;
    x H / (H + x) with H the headroom to 1023; faded to none at the top of the brightest channel; added where >= 1/2
    linear unit, as t[E + x] - its code, or for a pixel below L (the cells' least mean E: a lit area's dark texture)
    as L's own step t[L + x] - t[L]. E = the centre of each code's interval of the tone curve."""
    import numpy as np
    codes = np.clip(np.rint(ycc_decode(Y, Cb, Cr, C)), 0, 255).astype(int)
    ap = np.array(halo) / 1000 / (1 + np.array(halo) / 1000)
    d = np.zeros(codes.shape, int)
    k = np.arange(256)
    E = np.stack([np.clip((np.searchsorted(tone[c], k, 'left') + np.searchsorted(tone[c], k, 'right') - 1) / 2, 0,
                          1023)[codes[..., c]] for c in range(3)], -1)
    hh, ww = E.shape[:2]
    H8, W8 = (hh + 7) // 8, ww // 8
    em = E.max(-1)
    smp = em[np.minimum(np.arange(H8) * 8 + 2, hh - 1)][:, :W8 * 8].reshape(H8, W8, 8)[:, :, [2, 6]]
    bright = (smp >= HALO_EBIG).all(-1).astype(float)
    pad = np.pad(bright, BIG_S, mode='edge')
    n = 2 * BIG_S + 1
    box = np.lib.stride_tricks.sliding_window_view(pad, (n, n)).mean((-1, -2))
    q = np.floor(box * 64 + 0.5)[0::2, 0::2]
    q = np.repeat(np.repeat(q, 16, 0), 16, 1)[:hh, :ww] / 64                  # per 2 x 2 cells, as in the CFW
    q = np.pad(q, ((0, max(0, hh - q.shape[0])), (0, max(0, ww - q.shape[1]))), mode='edge')
    w = lambda e: np.clip((e - HALO_E[0]) / (HALO_E[1] - HALO_E[0]), 0, 1)
    fade = np.clip((1023 - em) / (1023 - HALO_FLO), 0, 1)
    yc, xc = np.arange(hh) - 3.5, np.arange(ww) - 3.5                          # cell centres 3.5 px into the cell
    fy, fx = np.clip(yc / 8, 0, H8 - 1), np.clip(xc / 8, 0, W8 - 1)
    src_blk = np.repeat((smp > HALO_E[0]).any(-1)[:, :W8 // 4 * 4].reshape(H8, -1, 4).any(-1), 4, 1)
    src_blk = np.pad(src_blk, ((0, 0), (0, W8 - src_blk.shape[1])))           # 32-px blocks the CFW decodes
    bg = ap[1] > 0 and 4 * ap[2] <= ap[1]                                     # blue's halo from green's
    hs = {}
    for c in range(3):
        e = E[..., c]
        src_c = 1 if bg and c == 2 else c
        if src_c not in hs:
            es = E[..., src_c]
            g = np.where(es > HALO_E[0], w(es) * (boost_ref(es, boost) * (1 - q) + es * q / 4) / np.maximum(es, 1), 0)
            S = es * g
            pd = ((0, H8 * 8 - hh), (0, 0))
            ce = np.pad(es, pd, mode='edge')[:, :W8 * 8].reshape(H8, 8, W8, 8).mean((1, 3)) * src_blk
            cs = np.pad(S, pd, mode='edge')[:, :W8 * 8].reshape(H8, 8, W8, 8).mean((1, 3)) * src_blk
            win = lambda a: np.lib.stride_tricks.sliding_window_view(np.pad(a, 1, mode='edge'), (3, 3))
            hc = np.maximum(ce, cs)
            A = np.minimum(win(hc).mean((-1, -2)), 2 * win(hc).min((-1, -2)))
            Lc = np.minimum(np.minimum(win(ce).mean((-1, -2)), 2 * win(ce).min((-1, -2))), 1023)
            up = lambda a: np.array([np.interp(fx, np.arange(W8), r) for r in
                                     np.array([np.interp(fy, np.arange(H8), a[:, j]) for j in range(W8)]).T])
            B = sum(wk * gauss(S, um * np.sqrt(b) * 4928 / 35000) for b, wk in ((1, 4 / 7), (2, 2 / 7), (3, 1 / 7)))
            hs[src_c] = (B, up(A), up(Lc))
        B, A, L = hs[src_c]
        if bg and c == 2:
            L = np.zeros_like(L)                                              # blue: no level
        x = ap[c] * B - 1.40625 * ap[c] * A
        tex = (e < L) & (L >= 16)                                             # a lit area's dark texture under a
        wl = np.clip(2 - np.maximum(x, 0) / np.maximum(L, 1), 0, 1)           # faint glow: L's step, back to its own
        base = np.where(tex, e + (L - e) * wl, e)                             # as the glow grows from L to 2 L
        hr = 1023 - base
        x = np.where(x > 0, x * hr / np.maximum(hr + x, 1e-6), x)             # never past the top
        h = x * fade
        t0 = np.where(tex, tone[c][np.clip(np.rint(base), 0, 1023).astype(int)], codes[..., c])
        new = tone[c][np.clip(np.rint(base + h), 0, 1023).astype(int)]
        d[..., c] = np.where(h >= 0.5, new - t0, 0)
    if ap[0] > 0:                                                             # red-orange: G's step <= R's >= B's
        d[..., 1] = np.minimum(d[..., 1], d[..., 0])
        d[..., 2] = np.minimum(d[..., 2], d[..., 1])
    dv = d @ C.T / 256
    q = lambda a: np.clip(np.rint(a), 0, 255).astype(np.uint8)
    pair = (dv[:, 0::2, 1:] + dv[:, 1::2, 1:]) / 2
    return q(Y + dv[..., 0]), q(Cb + pair[..., 0]), q(Cr + pair[..., 1])


class FxFrames:
    """The YCC buffer in mapped DRAM, the still path's scratch, and the contexts the still path hands the effect
    step; handles hold uncached aliases, as on the camera [H: H1]."""

    def __init__(self, m):
        import numpy as np
        self.m = m
        self.mem = np.frombuffer(m.map_dram(FX_MAP, FX_MAP_SIZE), np.uint8, count=FX_MAP_SIZE)
        self.mem[:] = (np.arange(FX_MAP_SIZE) % 251).astype(np.uint8)
        spr = np.frombuffer(m.map_sprwork(SPRWORK_AT), np.uint8)
        spr[:] = 0xA5                                              # whatever the stock passes left there
        self.y = FX_BUF + YCC_TOP * YCC_PITCH_Y + YCC_LEFT
        self.cb = FX_BUF + YCC_CB + YCC_TOP * YCC_PITCH_C + YCC_LEFT
        self.cr = FX_BUF + YCC_CR + YCC_TOP * YCC_PITCH_C + YCC_LEFT
        self.paint = None
        m.on_fx_step = self._stock_step

    def view(self, a, pitch, w, h, arr=None):
        from numpy.lib.stride_tricks import as_strided
        arr = self.mem if arr is None else arr
        return as_strided(arr[a - FX_MAP:], shape=(h, w), strides=(pitch, 1))

    def planes(self, w, h, arr=None):
        return (self.view(self.y, YCC_PITCH_Y, w, h, arr), self.view(self.cb, YCC_PITCH_C, w // 2, h, arr),
                self.view(self.cr, YCC_PITCH_C, w // 2, h, arr))

    def get(self, w, h):
        return tuple(p.copy() for p in self.planes(w, h))

    def _stock_step(self, mm, c):
        """The stock passes develop the frame: a hook that ran before them would see its work overwritten."""
        if self.paint:
            for dst, src in zip(self.planes(*self.paint[0]), self.paint[1], strict=False):
                dst[:] = src

    def ctx(self, w, h, over=None, mode=4, kind=0, alias=True, magic=0x42554646):
        m = self.m
        handle = m.obj_alloc(8)
        m.w32(handle, magic)
        m.w32(handle + 4, (FX_BUF & 0x1FFFFFFF) | 0x20000000 if alias else FX_BUF)
        c = m.obj_alloc(0x600)
        f = {0x1E0: 0, 0x1F0: handle, 0x218: 1, 0x21C: YCC_CB, 0x220: YCC_CR, 0x224: YCC_PITCH_Y,
             0x228: YCC_PITCH_C, 0x22C: YCC_ROWS, 0x230: YCC_TOP, 0x234: YCC_LEFT, 0x238: YCC_TOP, 0x23C: YCC_LEFT,
             0x258: w | h << 16}
        f.update(over or {})
        for o, v in f.items():
            m.w32(c + o, v)
        m.wr(c + 0x408, bytes([mode]))                         # colour mode (prop 0x2D at capture)
        m.wr(c + 0x008, bytes([kind]))                         # 7: the DNG's own still
        return c

    def shoot(self, ctx, c, frame=None, check_stack=True):
        """The still path's BL at FX_SITE with context c in the still-processing task; frame = (w, h, (Y, Cb, Cr))
        is what the stock step develops. Returns a copy of the whole mapped region before the shot (with the
        developed frame in it)."""
        import machine
        m, check = self.m, ctx.check
        self.paint = ((frame[0], frame[1]), frame[2]) if frame else None
        before = self.mem.copy()
        if frame:
            for dst, src in zip(self.planes(frame[0], frame[1], before), frame[2], strict=False):
                dst[:] = src
        n_io, n_calls = len(m.io), len(m.stock_calls)
        top = machine.STACK_TOP
        m.wr(top - 0x2000, b'\xcd' * 0x2000)
        m.run_range(FX_SITE | 1, FX_SITE + 4, c, stack=0x2000, task='ProcessStill', timeout_s=900)
        used = 0x2000 - next((i for i, b in enumerate(m.rd(top - 0x2000, 0x2000)) if b != 0xCD), 0x2000)
        check(m.stock_calls[n_calls:] == [('fx_step', c)], f'stock step not called once: {m.stock_calls[n_calls:]}')
        check(m.io[n_io:] == [], f'file I/O from the still-processing task: {m.io[n_io:]}')
        check(not m.sprwork_handle, 'scratch buffer not released')
        check(not check_stack or used <= 1024, f'hook stack {used} B (budget 1 KB: the task stack is unknown)')
        self.stack = used
        return before

    def status(self):
        return self.m.u32(self.m.syms['g_rec'] + REC_STATUS)

    def outside_same(self, before, w, h):
        """Nothing outside the three frame rectangles changed."""
        exp = before.copy()
        for dst, src in zip(self.planes(w, h, exp), self.planes(w, h), strict=False):
            dst[:] = src
        return int((self.mem != exp).sum())


def scene(w, h, C, sources=(), base=24, texture=0, seed=1):
    """Display-RGB' scene: a flat (or textured) dark ground and discs (x, y, radius, (r, g, b)), encoded."""
    import numpy as np
    rgb = np.full((h, w, 3), float(base))
    if texture:
        rng = np.random.default_rng(seed)
        rgb += rng.uniform(0, texture, (h, w, 1)) + rng.uniform(-10, 10, (h, w, 3))
    yy, xx = np.mgrid[0:h, 0:w]
    for x, y, r, col in sources:
        rgb[(xx - x) ** 2 + (yy - y) ** 2 <= r * r] = col
    return ycc_encode(np.clip(rgb, 0, 255), C)


def save_halation_png(ctx, name, before, after, C, box):
    """Before | after crop as sRGB codes (the frame decoded through C)."""
    import numpy as np
    from PIL import Image
    x0, y0, x1, y1 = box
    half = lambda p, ax0, ax1: p[y0:y1, ax0:ax1]
    tiles = [np.clip(ycc_decode(half(Y, x0, x1), half(Cb, x0 // 2, x1 // 2), half(Cr, x0 // 2, x1 // 2), C), 0, 255)
             for Y, Cb, Cr in (before, after)]
    img = np.concatenate([tiles[0], np.full((y1 - y0, 8, 3), 255.0), tiles[1]], axis=1).astype(np.uint8)
    ctx.renderer._save(Image.fromarray(img), name)


def sc_halation(ctx):
    """Halation in final stills (src/halation.c), every variant: the effect veneer at 0xA001E6CE; the effect step's
    call is retargeted (one halfword) only while the shown look has `halation` (preview, slot look, also from the D:
    look cache) and restored otherwise. Through the patched BL in the still-processing task, the stock step first: a
    bright disc gets a red-dominant halo that matches an independent float reference of spektrafilm's model, without
    the boost (a test recipe) and with it (800T); frames without highlights (status 5), other colour modes, the DNG's
    own still, RAW development, interval composite / multiple exposure, a busy scratch buffer and frame contexts that
    do not fit their buffer stay byte-identical; nothing outside the frame is written in 3:2, 4:3, 1:1, 16:9 and the
    crop. Verify first: a foreign halfword at the call refuses with no write. The halation keys' parser. Debug:
    instruction counts of full 3:2 frames; one over the cost budget (status 6) stays untouched."""
    import numpy as np
    from unicorn import UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE
    from unicorn.arm_const import UC_ARM_REG_CPSR
    check = ctx.check
    rec = ctx.recipes
    data_t = test_recipe('~Test halo', 'halation=300,100,15\r\nhalation_size=50\r\n')
    idx = idx_t = {r['file']: i for i, r in enumerate(by_name(list(rec) + [recipe_info(TEST_HALO, data_t)]))}
    s = Session(ctx, files={'H:/cfw/recipes/' + TEST_HALO: data_t}, props={0x2D: 4})
    m = s.m
    stock = lambda mm: mm.u16(FX_SITE) == 0xF000 and mm.u16(FX_SITE + 2) == FX_SITE_LO
    routed = lambda mm: (mm.u16(FX_SITE) == 0xF56F and mm.u16(FX_SITE + 2) == FX_SITE_LO and
                         mm.u32(FX_TARGET) == mm.syms['halation_hook'])   # ARM state: bit 0 clear
    check(m.rd(FX_VENEER, 10) == bytes.fromhex('4b01471846c000000000'), f'effect veneer: {m.rd(FX_VENEER, 10).hex()}')
    check(encode_bl(FX_SITE, FX_VENEER) == (0xF56F, FX_SITE_LO), 'veneer BL encoding')
    check(stock(m) and m.u32(FX_TARGET) == 0, 'effect-step call not stock after boot')

    def goto(i):
        d = i - s.recipe_sel()
        s.keys(*[KEY_RIGHT if d > 0 else KEY_LEFT] * abs(d))

    # Routed only while a halation look is shown: the preview, then MY1 = 800T (Setting2), MY2 = Vision3 500T.
    s.request(MENU_RECIPES)
    for f, want in (('c800d.txt', True), ('v3_500t.txt', False), ('c800t.txt', True)):
        goto(idx[f])
        check(routed(m) if want else stock(m), f'preview of {f}: effect step {"not " if want else ""}routed')
    s.keys(KEY_EFFECT)
    check(stock(m), 'call not restored after the preview')
    for f, k in (('c800t.txt', 0), ('v3_500t.txt', 1)):
        s.request(MENU_RECIPES)
        goto(idx[f])
        s.keys(KEY_OK, *[KEY_RIGHT] * k, KEY_OK)
        s.keys(KEY_EFFECT)
    for props, want, what in (({6: 1}, True, 'MY1 800T'), ({0x2D: 0}, False, 'MY1 in Standard'),
                              ({0x2D: 4}, True, 'MY1 back in Setting2'), ({6: 2}, False, 'MY2 Vision3 500T'),
                              ({6: 0}, False, 'no slot'), ({6: 1}, True, 'MY1 again')):
        m.props.update(props)
        m.call('recipes_poll')
        check(routed(m) if want else stock(m), f'{what}: effect step {"not " if want else ""}routed')

    fr = FxFrames(m)
    tone, C = still_model(m)

    # A bright white disc and an orange lamp on a dark ground: the halo against the float reference, for a look
    # without the boost (the test recipe ~Test halo, previewed) and for 800T (MY1, boosted and wider).
    def disc(tag, halo, um, boost):
        W, H = 1200, 800
        tone, C = still_model(m)                               # the look shown now
        planes = scene(W, H, C, [(600, 400, 40, (255, 255, 255)), (250, 200, 20, (255, 170, 60))])
        c = fr.ctx(W, H)
        before = fr.shoot(ctx, c, (W, H, planes))
        out = fr.get(W, H)
        check(fr.status() == HX_OK and fr.outside_same(before, W, H) == 0, f'{tag} disc: status {fr.status()} / outside')
        ref = ref_halation(*planes, tone, C, halo, um, boost)
        din = ycc_decode(*planes, C)
        dc, dr = ycc_decode(*out, C) - din, ycc_decode(*ref, C) - din
        check(dc.min() >= -0.5, f'{tag}: a channel lost light: {dc.min():.2f} codes at '
                                f'{np.unravel_index(dc.min(-1).argmin(), dc.shape[:2])}')
        yy, xx = np.mgrid[0:H, 0:W]
        rad = np.hypot(xx - 600, yy - 400)
        if boost[0]:                                           # no outline: the result's luma falls off from the core
            ar = out[0].astype(float)                          # out, past its 1-px anti-aliased edge (whose chroma
            pa = [ar[(rad >= r0) & (rad < r0 + 1)].mean() for r0 in range(41, 70)]   # pairs straddle the white)
            dip = max(pa[i + 1] - pa[i] for i in range(len(pa) - 1))
            check(dip <= 1, f'{tag}: the result rises by {dip:.1f} codes going out from the disc (an outline)')
        prof = [(r0, dc[(rad >= r0) & (rad < r0 + 5)].mean(0), dr[(rad >= r0) & (rad < r0 + 5)].mean(0))
                for r0 in (42, 47, 52, 57, 62)]
        # Without the boost the halo next to the disc is about its cells' hold (their mean light, at most twice their
        # least), so it is judged from 1.5 cells out.
        i0 = 1 if boost[0] else 2
        for r0, a, b in prof[0 if boost[0] else i0:]:          # red ring means within 1.5 codes or 15 %
            check(abs(a[0] - b[0]) <= max(1.5, 0.15 * b[0]),
                  f'{tag} disc halo at r {r0}: red {a[0]:.1f}, reference {b[0]:.1f}')
        rr = prof[i0][1]
        check(rr[0] > (10 if boost[0] else 2) and rr[0] > rr[1] > rr[2] and (boost[0] or rr[0] > 2 * rr[1] > 0),
              f'{tag}: halo not red-dominant at r {prof[i0][0]}: {rr.round(1)}')
        check(all(prof[i][1][0] > prof[i + 1][1][0] for i in range(1 if boost[0] else i0, 4)),
              f'{tag}: halo does not fall off with the radius: {[(r0, a.round(1).tolist()) for r0, a, _ in prof]}')
        reach = 6 * um * np.sqrt(3) * 4928 / 35000                # 6 sigma of the widest bounce, px
        ring = (rad > prof[0 if boost[0] else i0][0]) & (rad < 40 + reach)
        diff = np.abs(dc - dr)
        err = diff[ring]
        # per pixel: 5 codes or 25 % of the reference, but for 3 % of the ring: where the halo meets 1.4 x the ground's
        # own light it turns off within a few px, and at the disc's anti-aliased edge a pair's fade and its cells'
        # least sampled code decide here, each pixel there
        bad = (diff > np.maximum(5, 0.25 * np.abs(dr))).any(-1) & ring
        wy_, wx_ = np.unravel_index((diff.max(-1) * ring).argmax(), ring.shape)
        check(err.mean() <= 1.2 and bad.sum() <= 0.03 * ring.sum(),
              f'{tag} disc ring vs reference: mean {err.mean():.2f} codes, {bad.sum()} of {ring.sum()} pixels off; '
              f'worst ({wx_}, {wy_}) r {rad[wy_, wx_]:.1f}: {dc[wy_, wx_].round(1)} vs {dr[wy_, wx_].round(1)}')
        zone = np.ones_like(rad, bool) if boost[0] else (rad > prof[i0][0]) & (np.hypot(xx - 250, yy - 200) > 44)
        tot = dc[..., 0][zone].sum() / dr[..., 0][zone].sum()
        lo = 0.95 if boost[0] else 0.9                         # without the boost: halo and own light near equal
        check(lo <= tot <= 2 - lo, f'{tag}: total red halo {tot:.3f} x the reference')
        far = (rad > 40 + reach) & (np.hypot(xx - 250, yy - 200) > 20 + reach)
        check(all((o.astype(int) == i.astype(int))[far[:, ::k]].all() for o, i, k in zip(out, planes, (1, 2, 2),
                                                                                            strict=False)),
              f'{tag}: pixels far from the sources changed')
        if ctx.renderer:
            save_halation_png(ctx, f'{ctx.variant}_halation_{tag}_disc', planes, out, C, (440, 240, 760, 560))
            save_halation_png(ctx, f'{ctx.variant}_halation_{tag}_disc_reference', planes, ref, C,
                              (440, 240, 760, 560))
            save_halation_png(ctx, f'{ctx.variant}_halation_{tag}_lamp', planes, out, C, (130, 80, 370, 320))
        return fr.stack

    s.request(MENU_RECIPES)                                    # ~Test halo: spektrafilm's preset, no boost
    goto(idx_t[TEST_HALO])
    check(routed(m), 'preview of the test recipe: effect step not routed')
    disc('noboost', (300, 100, 15), 50, (0, 0.3, 2.0))
    s.keys(KEY_EFFECT)
    m.call('recipes_poll')
    c8 = halo_keys(CFW / 'recipes' / 'c800t.txt')               # MY1 = 800T again
    check(c8['boost'][0] > 0, f'800T has no halation_boost: {c8}')
    halo_hook_stack = disc('800t', c8['halo'], c8['um'], c8['boost'])

    # 800T: a lit, noisy near-white surface (400 x 300 px) and a lamp (60 px) of the same white on one dark ground. The
    # surface gets no speckles inside (its pixels' own light and their cells' cancel its halo) and, being large, loses
    # part of the boost (its edge cells: about half their 17 x 17 cells bright): 20 .. 40 px beside it the glow is
    # weaker than beside the lamp, though its light is far more. A warm clipped lamp keeps its colour inside (the fade
    # at the top of its brightest channel), its glow outside. A warm patch beside the lamp, red near its top: the
    # halo there adds less green than red (no green fringe). A cool lamp (cyan-white, green boosted far more than
    # red) gets no green halo either.
    rng_ = np.random.default_rng(5)
    W2, H2 = 1280, 640
    rgb = np.full((H2, W2, 3), 30.0)
    rgb[170:470, 180:580] = 228 + rng_.uniform(-24, 24, (300, 400, 1))
    pits = np.zeros((H2, W2), bool)                            # dark pits in it (1 %): below 1/8 of its light
    pits[170:470, 180:580] = rng_.random((300, 400)) < 0.01
    rgb[pits] = 60
    yy_, xx_ = np.mgrid[0:H2, 0:W2]
    rgb[(yy_ - 320) ** 2 + (xx_ - 900) ** 2 <= 30 ** 2] = 255
    warm = (yy_ - 320) ** 2 + (xx_ - 1150) ** 2 <= 30 ** 2
    rgb[warm] = (255, 170, 60)
    rgb[300:340, 820:866] = (214, 175, 110)
    rgb[(yy_ - 570) ** 2 + (xx_ - 720) ** 2 <= 25 ** 2] = (212, 255, 255)
    planes = ycc_encode(np.clip(rgb, 0, 255), C)
    fr.shoot(ctx, fr.ctx(W2, H2), (W2, H2, planes))
    dlt = ycc_decode(*fr.get(W2, H2), C) - ycc_decode(*planes, C)
    check(dlt.min() >= -0.5, f'surface scene: a channel lost light: {dlt.min():.2f} codes')
    inner = np.abs(dlt[210:430, 220:540])
    check(fr.status() == HX_OK and inner.mean() <= 1.0 and (inner > 6).any(-1).mean() <= 0.01,
          f'speckles inside a bright surface: mean {inner.mean():.2f} codes, '
          f'{(inner > 6).any(-1).mean() * 100:.1f} % of pixels over 6')
    pin = pits & (yy_ >= 210) & (yy_ < 430) & (xx_ >= 220) & (xx_ < 540)
    pit_hot = (np.abs(dlt[pin]) > 6).any(-1).mean()
    check(pit_hot <= 0.05, f'dark pits inside a bright surface took its halo: {pit_hot * 100:.1f} % over 6 codes')
    near_big = dlt[318:323, 600:620, 0].mean()                 # 20 .. 40 px right of the surface
    near_lamp = dlt[318:323, 950:970, 0].mean()                # 20 .. 40 px right of the lamp's edge
    check(near_lamp > 20 and near_big < 0.95 * near_lamp,
          f'large bright area vs lamp: red {near_big:.1f} beside the surface, {near_lamp:.1f} beside the lamp')
    wi = (yy_ - 320) ** 2 + (xx_ - 1150) ** 2 <= 24 ** 2
    check(np.abs(dlt[wi]).max() <= 2 and dlt[318:323, 1190:1200, 0].mean() > 20,
          f'clipped warm lamp: inside {np.abs(dlt[wi]).max(0)} codes, red beside {dlt[318:323, 1190:1200, 0].mean():.1f}')
    wp = dlt[300:340, 820:866]
    check(wp[..., 0].max() > 0 and (wp[..., 1] <= wp[..., 0] + 1).all(),
          f'warm patch near its top beside a lamp: green over red by up to {(wp[..., 1] - wp[..., 0]).max():.1f} codes')
    wr = np.rint(ycc_decode(*fr.get(W2, H2), C)[300:340, 820:866, 0])
    check(wr.max() < tone[0][1023], f'warm patch: the halo took red to its top code {tone[0][1023]} (no headroom taper)')
    cl = dlt[568:573, 755:765].mean((0, 1))                    # 10 .. 20 px right of the cool lamp
    check(cl[0] > 5 and cl[1] <= cl[0] + 0.5, f'cool lamp: halo green: {cl.round(1)}')

    # 800T: a lamp-lit, textured wall below the clip (E 680 in every channel, 2 % dark pits at code 120) beside a lamp
    # on dark ground.
    # A large bright area: little glow off it (20 .. 40 px left of it, against the lamp's at the same distance). Where
    # the lamp's faint glow lands on it, its dark pits change about as much as the wall around (the texture keeps
    # its contrast); far from the lamp the wall stays.
    rng_ = np.random.default_rng(9)
    W3, H3 = 1280, 640
    rgb = np.full((H3, W3, 3), 30.0)
    rgb[120:520, 200:900] = np.array([tone[c][680] for c in range(3)], float) + rng_.uniform(-2, 2, (400, 700, 1))
    wpits = np.zeros((H3, W3), bool)
    wpits[120:520, 200:900] = rng_.random((400, 700)) < 0.02
    rgb[wpits] = 120
    yy3, xx3 = np.mgrid[0:H3, 0:W3]
    rgb[(yy3 - 320) ** 2 + (xx3 - 950) ** 2 <= 40 ** 2] = 255
    planes = ycc_encode(np.clip(rgb, 0, 255), C)
    fr.shoot(ctx, fr.ctx(W3, H3), (W3, H3, planes))
    d3 = ycc_decode(*fr.get(W3, H3), C) - ycc_decode(*planes, C)
    check(fr.status() == HX_OK and d3.min() >= -0.5, f'lit wall: status {fr.status()}, least change {d3.min():.2f}')
    off_wall = d3[300:340, 160:180, 0].mean()                  # 20 .. 40 px left of the wall
    off_lamp = d3[300:340, 1010:1030, 0].mean()                # 20 .. 40 px right of the lamp's edge
    near = (xx3 >= 860) & (xx3 < 900) & (yy3 > 260) & (yy3 < 380)
    pit_d, wall_d = d3[near & wpits, 0], d3[near & ~wpits, 0]
    far = (xx3 > 300) & (xx3 < 600) & (yy3 > 220) & (yy3 < 420)   # the wall's inside, far from the lamp
    check(off_lamp > 20 and off_wall < 0.2 * off_lamp,
          f'lit wall: red {off_wall:.1f} beside it, {off_lamp:.1f} beside the lamp (a large bright area is not a lamp)')
    check(np.percentile(pit_d, 90) <= 9, f'lit wall: its dark pits under the lamp\'s glow change by up to '
                                          f'{np.percentile(pit_d, 90):.1f} codes (90 %), the wall {np.median(wall_d):.1f}')
    check(((np.abs(d3[far]) > 0.5).any(-1)).mean() <= 0.01, 'lit wall: changed far from the lamp')
    # A clipped window (120 x 80) on a lit facade (code 175): no outline next to it, the result's luma 2 px past its
    # edge at most 4 codes under that 3 .. 16 px out (the window's cells hold a little within one cell: 8 px).
    rgb = np.full((H3, W3, 3), 175.0) + rng_.uniform(-4, 4, (H3, W3, 1))
    rgb[280:360, 580:700] = 255
    planes = ycc_encode(np.clip(rgb, 0, 255), C)
    fr.shoot(ctx, fr.ctx(W3, H3), (W3, H3, planes))
    wy = fr.get(W3, H3)[0][300:340, 702:760].astype(float).mean(0)   # luma, 2 .. 60 px right of the window
    rise = wy[1:15].max() - wy[0]
    check(fr.status() == HX_OK and wy[0] > planes[0][300:340, 702].mean() + 5 and rise <= 4,
          f'window on a facade: luma rises by {rise:.1f} going out (an outline): {wy[:16].round(1)}')

    # Frames the pass must leave byte-identical (each still runs the stock step once).
    sw, sh = 480, 320
    lamp = scene(sw, sh, C, [(240, 160, 30, (255, 255, 255))])
    dark = scene(sw, sh, C, base=20, texture=150)              # texture below the highlight ramp
    check(ycc_decode(*dark, C).max() < 200, 'harness: the "no highlights" frame has highlights')
    same = [('no highlights', fr.ctx(sw, sh), dark, HX_NONE, None),
            ('Standard frame', fr.ctx(sw, sh, mode=0), lamp, HX_MODE, None),
            ('stock effect routing (ctx+0x357)', fr.ctx(sw, sh), lamp, HX_MODE, lambda cc: m.wr(cc + 0x357, b'\x10')),
            ("the DNG's own still (ctx+0x008 = 7)", fr.ctx(sw, sh, kind=7), lamp, HX_RAW, None),
            ('RAW development (RAW handle)', fr.ctx(sw, sh, over={0x1E0: m.obj_alloc(8)}), lamp, HX_RAW, None),
            ('interval composite', fr.ctx(sw, sh), lamp, HX_CONTEXT, lambda cc: m.props.update({0x2BE: 1})),
            ('multiple exposure', fr.ctx(sw, sh), lamp, HX_CONTEXT, lambda cc: m.props.update({0x2BE: 3})),
            ('scratch busy', fr.ctx(sw, sh), lamp, HX_NO_SCRATCH, lambda cc: setattr(m, 'sprwork_busy', True)),
            ('Cr plane past the buffer end', fr.ctx(4928, 3264, over={0x220: YCC_SIZE - 1000 * YCC_PITCH_C}), lamp,
             HX_OUTSIDE, None),
            ('frame wider than the Y pitch', fr.ctx(5000, 3264), lamp, HX_BAD_SIZE, None),
            ('width not a multiple of 8', fr.ctx(sw + 4, sh), lamp, HX_BAD_SIZE, None),
            ('more rows than the plane', fr.ctx(4928, YCC_ROWS + 1), lamp, HX_BAD_SIZE, None),
            ('huge pitch', fr.ctx(sw, sh, over={0x224: 0x10000}), lamp, HX_BAD_SIZE, None),
            ('handle without the BUFF magic', fr.ctx(sw, sh, magic=0), lamp, HX_NO_BUFFER, None),
            ('no chroma planes', fr.ctx(sw, sh, over={0x218: 0}), lamp, HX_NO_CHROMA, None)]
    for name, cc, planes_, status, setup in same:
        if setup:
            setup(cc)
        before = fr.shoot(ctx, cc, (sw, sh, planes_))
        m.props[0x2BE], m.sprwork_busy = 0, False
        check(fr.status() == status, f'{name}: status {fr.status()}, expected {status}')
        check(np.array_equal(fr.mem, before), f'{name}: {int((fr.mem != before).sum())} bytes changed')
        m.call('cfw_task_poll')
        errs = [l for l in m.log_lines() if 'ERROR' in l]
        check(bool(errs) == (status >= HX_NO_BUFFER), f'{name}: ERROR lines {errs}')
        if status >= HX_NO_BUFFER:
            m.files.pop('H:/CFW/CFW_LOG.TXT')                  # the expected ERROR line
    m.props[0x2BE] = 2                                         # interval shooting (not composite): applied
    fr.shoot(ctx, fr.ctx(sw, sh), (sw, sh, lamp))
    m.props[0x2BE] = 0
    check(fr.status() == HX_OK and m.u32(m.syms['g_rec'] + REC_WORDS) > 0, 'interval shot: no halo')

    # Off the slot the call is stock again; the hook still reached through a stale I-cache line only passes through.
    m.props[6] = 0
    m.call('recipes_poll')
    check(stock(m), 'not restored off the slot')
    fr.paint = ((sw, sh), lamp)
    before = fr.mem.copy()
    for dst, src in zip(fr.planes(sw, sh, before), lamp, strict=False):
        dst[:] = src
    n_calls = len(m.stock_calls)
    m.call_addr(m.syms['halation_hook'], fr.ctx(sw, sh), stack=0x2000, task='ProcessStill')
    check(len(m.stock_calls) == n_calls + 1 and np.array_equal(fr.mem, before), 'restored: the hook did not pass through')
    m.props[6] = 1
    m.call('recipes_poll')

    # Every aspect at full size, sources on the frame's edges and corners: the halo is clipped at the frame.
    edge = lambda w, h: [(0, 0, 60, (255, 255, 255)), (w - 1, h // 2, 50, (255, 200, 120)), (w // 2, h - 1, 50,
                         (255, 255, 255)), (w - 30, 20, 25, (255, 255, 255))]
    for name, (w, h) in FX_FRAMES.items():
        planes_ = scene(w, h, C, edge(w, h), base=30)
        before = fr.shoot(ctx, fr.ctx(w, h), (w, h, planes_))
        outside = fr.outside_same(before, w, h)
        got = fr.get(w, h)
        changed = int((got[0] != planes_[0]).sum())
        check(fr.status() == HX_OK and outside == 0 and changed > 1000,
              f'{name}: status {fr.status()}, {outside} bytes outside the frame, {changed} Y changed')
        check((got[2][:200, :100].astype(int) >= planes_[2][:200, :100]).all() and
              (got[2][:200, :100] != planes_[2][:200, :100]).any(), f'{name}: no red halo at the corner source')

    # Timing (debug, 800T): instructions per ARM/Thumb block at full 3:2; the cost estimate decides on the modelled
    # camera time (src/halation.c HX_BUDGET, EST_*: fitted to the camera time of the emulator's instructions, CAM_NS
    # from the H2 shots), a frame over it stays byte-identical; a frame it runs (also one with much to decode and no
    # highlight: status 5) has an estimate within 25 % of its camera time.
    if ctx.variant == 'debug':
        lo, hi = m.main_base, m.main_end
        rng = np.random.default_rng(7)
        lamps = lambda n, r0, r1, col, edge=40: [(int(x), int(y), int(r), col) for x, y, r in zip(
            rng.integers(edge, 4928 - edge, n), rng.integers(edge, 3264 - edge, n), rng.integers(r0, r1, n),
            strict=False)]
        night, worst = lamps(24, 6, 40, (255, 230, 180)), lamps(160, 40, 160, (255, 255, 255), 0)
        bright, city = lamps(20, 40, 160, (255, 255, 255), 0), lamps(120, 4, 30, (255, 220, 160))
        for what, planes_, want in (('night, 24 lamps', scene(4928, 3264, C, night, base=18), HX_OK),
                                    ('busy night, 120 lamps', scene(4928, 3264, C, city, base=18), None),
                                    ('bright, 20 large highlights on texture', scene(4928, 3264, C, bright, base=40,
                                                                                     texture=40), None),
                                    ('bright worst case, 160 large highlights', scene(4928, 3264, C, worst, base=60,
                                                                                      texture=90), HX_BUSY),
                                    ('texture without highlights', scene(4928, 3264, C, base=20, texture=150),
                                     HX_NONE)):
            n = [0]

            def count(uc, a, size, _, n=n):                    # Thumb blocks: 2-byte, ARM blocks: 4-byte instructions
                n[0] += size >> (1 if uc.reg_read(UC_ARM_REG_CPSR) & 0x20 else 2)
            hk = m.uc.hook_add(UC_HOOK_BLOCK, count, begin=lo, end=hi)
            m.uc.ctl_remove_cache(lo, hi)                      # translated blocks predate the hook
            before = fr.shoot(ctx, fr.ctx(4928, 3264), (4928, 3264, planes_))
            m.uc.hook_del(hk)
            cells, rows, groups, words, dec, nh, src, nb = (m.u32(m.syms['g_rec'] + o) for o in (
                REC_CELLS, REC_CELLS + 4, REC_CELLS + 8, REC_WORDS, REC_DEC, REC_DEC + 4, REC_DEC + 8, REC_DEC + 12))
            st = fr.status()
            if st == HX_OK:
                lost = (ycc_decode(*fr.get(4928, 3264), C) - ycc_decode(*planes_, C)).min()
                check(lost >= -0.5, f'{what}: a channel lost light: {lost:.2f} codes')
            est, cam = est_ms(dec, nh, nb), n[0] * CAM_NS / 1e6 + CAM_MS0
            print(f'  halation timing, {what}: status {st}, {n[0] / 1e6:.1f} M instructions = {cam:.0f} ms on the '
                  f'camera, estimate {dec} decoded groups / {src} source / {nh} halo / {nb} boosted cells = '
                  f'{est:.0f} ms, {cells} source cells, {groups} groups with a halo, {words} words written')
            ctx.halo_timing = getattr(ctx, 'halo_timing', []) + [(what, n[0], est, st)]
            check(st in (HX_OK, HX_BUSY, HX_NONE) and (want is None or st == want),
                  f'timing shot ({what}): status {st}, expected {want}')
            check(st == HX_BUSY or est <= 3000, f'{what}: applied with an estimate of {est:.0f} ms (budget 3 s)')
            check(st == HX_BUSY or abs(est - cam) <= 0.25 * cam, f'{what}: estimate {est:.0f} ms, {cam:.0f} ms')
            check(st == HX_OK or np.array_equal(fr.mem, before), f'{what}: over budget but changed')
        print(f'  halation hook stack: {halo_hook_stack} B')

    finish(ctx, s)

    # After a reboot: MY1's halation from the config and the D: look cache, also without the recipe file.
    d = m.d_files()
    for drop in ((), ('H:/cfw/recipes/c800t.txt',)):
        s2 = Session(ctx, files=d, slots=m.slots, props={0x2D: 4, 6: 1}, drop=drop)
        check(routed(s2.m), f'after a reboot{" without c800t.txt" if drop else ""}: effect step not routed')
        s2.m.props[6] = 0
        s2.m.call('recipes_poll')
        check(stock(s2.m), 'after a reboot: not restored off the slot')
        finish(ctx, s2)

    # Verify first: a foreign halfword at the call refuses with no write, one ERROR line, and stays refused.
    s3 = Session(ctx, files=d, slots=m.slots, props={0x2D: 4, 6: 0})
    m3 = s3.m
    m3.w16(FX_SITE, 0xF001)
    writes = []
    m3.uc.hook_add(UC_HOOK_MEM_WRITE, lambda uc, acc, a, size, val, _: writes.append((a, size, val)),
                   begin=FX_SITE, end=FX_SITE + 3)
    m3.props[6] = 1
    for _ in range(3):
        m3.call('recipes_poll')
    errs = [l for l in m3.log_lines() if 'ERROR' in l]
    check(not writes and m3.u16(FX_SITE) == 0xF001 and m3.u32(FX_TARGET) == 0 and len(errs) == 1 and
          'halation: effect-step call not stock' in errs[0], f'foreign call site: writes {writes}, log {errs}')
    m3.files.pop('H:/CFW/CFW_LOG.TXT')                         # the expected ERROR line
    finish(ctx, s3)

    # The keys' parser: a boost or size needs `halation`; values out of range are errors; decimals as documented.
    bad = {'t_hb1.txt': 'halation_boost=2.0\r\n', 't_hb2.txt': 'halation=300,100,15\r\nhalation_boost=6.1\r\n',
           't_hb3.txt': 'halation=300,100,15\r\nhalation_size=131\r\n',
           't_hb4.txt': 'halation=300,100,15\r\nhalation_boost=2,1.01\r\n',
           't_hb5.txt': 'halation=300,100,15\r\nhalation_boost=2,0.3,2,1\r\n'}
    good = {'t_hb6.txt': 'halation=300,100,15\r\nhalation_size=130\r\nhalation_boost=6,0.5,4\r\n',
            't_hb7.txt': 'halation=300,100,15\r\nhalation_boost=2.5\r\n'}
    files = {'H:/cfw/recipes/' + f: test_recipe('~Test ' + f[:-4], b) for f, b in {**bad, **good}.items()}
    s4 = Session(ctx, files=files, props={0x2D: 4})
    names = s4.recipe_names()
    check(all(('~Test ' + f[:-4]) in names for f in good) and not any(('~Test ' + f[:-4]) in names for f in bad),
          f'halation key parsing: list {names}')
    errs = [l for l in s4.m.log_lines() if 'ERROR' in l]
    check(len(errs) == len(bad) and all(any(f.upper() in e for e in errs) for f in bad), f'halation key errors {errs}')
    s4.m.files.pop('H:/CFW/CFW_LOG.TXT')
    finish(ctx, s4)


def lv_gradient_visible(bmp, m):
    """Where the shown OSD is transparent the screenshot is the grey LV ramp: dark left, bright right."""
    import numpy as np
    import machine
    img = np.frombuffer(bmp[54:], np.uint8).reshape(480, 640, 3)[::-1].astype(np.int32)
    osd = np.frombuffer(m.rd(m.u32(machine.A_BITMAPS), 640 * 480), np.uint8).reshape(480, 640)
    clear = ((np.array(m.pal_shadow, np.int64) >> 24) & 0xF)[osd] == 0
    left, right = clear[:, :32], clear[:, -32:]
    if left.sum() < 100 or right.sum() < 100:
        return False
    grey = np.abs(img[..., 0] - img[..., 2])[clear].max() <= 2
    return grey and img[:, :32][left].mean() < 20 and img[:, -32:][right].mean() > 235


def expected_shot(m):
    """src/info.c screenshot(), computed independently from the modelled LV frame and OSD layer."""
    import numpy as np
    import machine
    lv = np.frombuffer(m.rd(machine.LV_FRAME, 0x96000), np.uint8).astype(np.int64)
    Y = lv[:640 * 480].reshape(480, 640)
    U = np.repeat(lv[0x4B000:0x4B000 + 320 * 480].reshape(480, 320), 2, axis=1) - 128
    V = np.repeat(lv[0x70800:0x70800 + 320 * 480].reshape(480, 320), 2, axis=1) - 128
    osd = np.frombuffer(m.rd(m.u32(machine.A_BITMAPS), 640 * 480), np.uint8).reshape(480, 640)
    pal = np.array(m.pal_shadow, np.int64)
    e = pal[osd]
    al = (e >> 24) & 0xF
    ey, eu, ev = (e >> 16) & 0xFF, ((e >> 8) & 0xFF) - 128, (e & 0xFF) - 128

    def rgb(y, u, v):
        return y + ((359 * v) >> 8), y - ((88 * u + 183 * v) >> 8), y + ((454 * u) >> 8)
    r, g, b = rgb(Y, U, V)
    orr, og, ob = rgb(ey, eu, ev)
    blend = lambda c, o: np.where(al > 0, ((c * (15 - al) + o * al) * 273) >> 12, c)
    out = np.stack([blend(b, ob), blend(g, og), blend(r, orr)], axis=-1).clip(0, 255).astype(np.uint8)
    hdr = bytearray(54)
    hdr[0:2] = b'BM'
    struct.pack_into('<IIIIiiHHII', hdr, 2, SHOT_BYTES, 0, 54, 40, 640, 480, 1, 24, 0, 640 * 480 * 3)
    return bytes(hdr) + out[::-1].tobytes()


def sc_recipe_files(ctx):
    """Every shipped recipe and the look-stage test recipes (built here: matrix, tone, tone_r, hue sectors, post
    matrix) parse; the list is sorted by name, then Stock, also after card recipes named "zzz" and "Stock"; every
    entry previews its look stages in Setting2, the built-in Stock none."""
    check = ctx.check
    extra = {**TEST_RECIPES,
             'zzz.txt': test_recipe('zzz', 'matrix=128,0,0,0,128,0,0,0,128\r\n'),
             'stock.txt': test_recipe('Stock', 'matrix=120,4,4,4,120,4,4,4,120\r\n')}
    s = Session(ctx, files={'H:/cfw/recipes/' + k: v for k, v in extra.items()}, props={0x2D: 4})
    want = by_name(ctx.recipes + [recipe_info(k, v) for k, v in extra.items()])
    names = s.recipe_names()
    check(names == [r['name'] for r in want] + ['Stock'], f'recipe list {names}')
    check(s.m.call('recipes_count') == len(want), f'recipes_count {s.m.call("recipes_count")} != {len(want)}')
    check(not [l for l in s.m.log_lines() if 'invalid line' in l or 'cannot read' in l], 'recipe parse errors')
    s.request(MENU_RECIPES)
    for i, r in enumerate(want + [None]):
        if i:
            s.keys(KEY_RIGHT)
        flags = r['flags'] if r else 0
        check(s.look_shown({3: flags} if flags else {}), f'preview of {names[i]!r} (entry {i}): look stages')
    s.keys(KEY_EFFECT)
    finish(ctx, s)


def sc_view(ctx):
    """VIEW: frame lines and zebras in the shown full-plane buffer, restamp after a stock flip,
    removal, plane-off guard, DEV overlay checks (src/overlay.c, finding 23 Tier A)."""
    import numpy as np
    import machine
    check = ctx.check
    s = Session(ctx)
    m = s.m
    MASK_C, LINE_C = 0x000A, 0xFFFC
    buf = lambda i: np.frombuffer(m.rd(machine.FULL_BUFS[i], 640 * 480 * 2), '>u2').reshape(480, 640)
    # 3:2 image area y 28-451; XPan 65:24 -> 236 rows at y 122-357.
    def xpan_ok(b):
        return ((b[28:122] == MASK_C).all() and (b[358:452] == MASK_C).all() and
                (b[122:124] == LINE_C).all() and (b[356:358] == LINE_C).all() and
                (b[124:356] == 0).all() and (b[:28] == 0).all() and (b[452:] == 0).all())
    s.request(MENU_SETTINGS)
    s.keys(KEY_RIGHT, KEY_RIGHT, KEY_DOWN, KEY_RIGHT)
    check(s.setting(5) == 'XPan 65:24', f'frames row: {s.setting(5)!r}')
    s.keys(KEY_EFFECT)
    check(not s.is_open(), 'menu still open')
    m.call('overlay_poll')
    check(xpan_ok(buf(0)), 'XPan frame not stamped into BITMAPS[2] (mask y 28-121/358-451, lines 122-123/356-357)')
    s.render('view_xpan')
    # Stock flip: it copies its canvas over BITMAPS[3] and shows it; the CFW stamps again.
    m.wr(machine.FULL_BUFS[1], b'\0' * (640 * 480 * 2))
    m.wr(machine.A_GSTATE + 1, b'\x03')
    m.call('overlay_poll')
    check(xpan_ok(buf(1)), 'frame not restamped after the stock flip')
    # Zebras at Y >= 252: the LV ramp reaches 252 from x 632 (cells 158, 159).
    m.call('overlay_set_zebra', 3)
    m.call('overlay_poll')
    check(m.u32(machine.A_REC_RGB_REDRAW) == 1, 'setting change did not request a stock redraw')
    check(xpan_ok(buf(1)[:, :632]), 'frame not stamped again after the setting change')
    for _ in range(5):
        m.call('overlay_poll')
    b = buf(1)
    hot = b[28:452, 632:640]
    check(np.isin(hot, (0xFFFF, 0x000F)).all() and (hot == 0xFFFF).any() and (hot == 0x000F).any(),
          'no zebra stripes at x 632-639')
    check(xpan_ok(b[:, :632]), 'zebras touched cold cells')
    s.render('view_zebra')
    m.wr(machine.LV_FRAME, b'\0' * (640 * 480))           # dark scene: cells go cold
    for _ in range(5):
        m.call('overlay_poll')
    b = buf(1)
    check((b[160, 632:640] == 0x0008).all(), 'cold cell not restored from the stock canvas (grid row y 160)')
    check((b[100, 632:640] == MASK_C).all() and (b[200, 632:640] == 0).all(), 'cold cell lost the frame overlay')
    # Everything off: the shown buffer is cleared and stock asked to rebuild its own pixels.
    m.w32(machine.A_REC_RGB_REDRAW, 0)
    m.call('overlay_set_frame', 0)
    m.call('overlay_set_zebra', 0)
    m.call('overlay_poll')
    check(not buf(1).any() and m.u32(machine.A_REC_RGB_REDRAW) == 1, 'overlay not removed')
    # Plane off (menus, playback): nothing is drawn.
    m.layers_on[2] = 0
    m.call('overlay_set_frame', 4)
    m.call('overlay_poll')
    check(not buf(1).any() and m.call('overlay_test_pattern') != 0, 'drew while the full plane was off')
    m.layers_on[2] = 1
    m.call('overlay_poll')
    b = buf(1)
    check((b[28:452, 108:110] == LINE_C).all() and (b[28:452, :108] == MASK_C).all(), '1:1 frame (x 108-531)')
    rc = m.call('overlay_test_pattern')
    check(rc == 1 and buf(1)[40, 40] == 0xF00F and buf(1)[40, 160] == 0xF008, 'test pattern')
    # second press removes it (hardware 2026-11-01: it otherwise stayed until a reboot); the frame comes back
    m.w32(machine.A_REC_RGB_REDRAW, 0)
    check(m.call('overlay_test_pattern') == 0 and buf(1)[40, 40] == 0 and m.u32(machine.A_REC_RGB_REDRAW) == 1,
          'test pattern not removed')
    m.call('overlay_poll')
    check((buf(1)[28:452, 108:110] == LINE_C).all(), '1:1 frame not restamped after removing the pattern')
    if ctx.variant == 'debug':
        m.call('overlay_log_state')
        check(sum('ovl:' in l for l in m.log_lines()) == 7, 'overlay state not logged')
    finish(ctx, s)


def sc_ae_hooks(ctx):
    """EXPOSURE: TAv Auto-Hi minimum (SV hook 0xA00DC236) and Auto ISO in M, on and off; the EV-comp
    getter stays stock; config keys; a non-stock guard halfword refuses to patch (finding 21 R3/R5)."""
    check = ctx.check
    S_TAVMIN, S_MAUTO = 13, 14
    TAV, MSV, MEV = 0xA00DC236, 0xA00DC22A, 0xA00DAFE6
    s = Session(ctx)
    m = s.m
    code = lambda: [m.u16(a) for a in (TAV, MSV, MEV)]
    check(code() == [0x000A, 0x22F0, 0x2806], f'hooks not stock at start: {[hex(v) for v in code()]}')
    check(s.m.cstr(m.call('settings_label', S_TAVMIN)) == 'TAv: Auto-Hi minimum too', 'row order')
    m.call('settings_step', S_TAVMIN, 1)
    check(code() == [0x0002, 0x22F0, 0x2806] and s.setting(S_TAVMIN) == 'ON', 'TAv min not patched')
    m.call('settings_step', S_MAUTO, 1)
    check(code() == [0x0002, 0x000A, 0x2806] and s.setting(S_MAUTO) == 'ON', 'M auto not patched')
    m.call('settings_step', S_MAUTO, 1)
    m.call('settings_step', S_TAVMIN, -1)
    check(code() == [0x000A, 0x22F0, 0x2806], f'not stock after OFF: {[hex(v) for v in code()]}')
    m.call('settings_step', S_MAUTO, 1)
    m.call('settings_save')
    cfg = m.cfg_text()
    check('mauto=00000001' in cfg and 'tavmin=00000000' in cfg, f'config: {cfg!r}')
    finish(ctx, s)
    # Reboot with that config: M auto is applied at init. A foreign guard halfword blocks TAv min.
    s2 = Session(ctx, files=m.d_files())
    m2 = s2.m
    check(m2.u16(MSV) == 0x000A, 'M auto not re-applied from the config')
    m2.w16(0xA00DC232, 0x0000)
    m2.call('settings_step', S_TAVMIN, 1)
    check(m2.u16(TAV) == 0x000A and s2.setting(S_TAVMIN) == 'ERR', 'patched over a foreign guard')
    errs = [l for l in m2.log_lines() if 'ERROR' in l]
    check(len(errs) == 1 and 'not as expected' in errs[0], f'guard error not logged: {errs}')
    m2.files.pop('H:/CFW/CFW_LOG.TXT')                 # the expected ERROR line (both variants log ERRORs)
    finish(ctx, s2)


def console_exec(m, line, size=8192, task='CFW'):
    """Run one developer-console line in the CFW (console_exec); returns (rc, text)."""
    lp = m.obj_alloc(len(line) + 1)
    m.wr(lp, line.encode('latin-1') + b'\0')
    op = m.obj_alloc(size)
    rc = m.call_addr(m.syms['console_exec'] | 1, lp, op, size, task=task)
    rc = rc - (1 << 32) if rc & 0x80000000 else rc
    return rc, m.cstr(op, size)


def sc_console(ctx):
    """DEV telnet ON starts the developer console: server task, printf capture hook, CFW log tap;
    commands log, sh, tasks, heap, mem (bounds), status, unknown, exit."""
    import machine
    check = ctx.check
    s = Session(ctx)
    m = s.m
    s.request(MENU_SETTINGS)
    s.keys(KEY_LEFT, KEY_DOWN, KEY_OK)                         # DEV: telnet ON
    check(s.info(1, 0) == f'ON  {DOT}  telnet 192.168.0.1' or s.info(1, 0) == f'ON  {DOT}  Wi-Fi off (camera menu)',
          f'console row: {s.info(1, 0)!r}')
    con = [t for t in m.tasks if t['name'] == 'CFWcon']
    check(len(con) == 1 and con[0]['entry'] & ~1 == m.syms['server'] & ~1, 'console task not started')
    hook = m.u32(machine.A_CONSOLE_HOOK_SLOT)
    check(hook & ~1 == m.syms['printf_hook'] & ~1, f'printf hook not installed: {hook:#x}')
    s.keys(KEY_OK, KEY_OK)                                     # off and on again: still one task, hook back
    check(len([t for t in m.tasks if t['name'] == 'CFWcon']) == 1 and m.u32(machine.A_CONSOLE_HOOK_SLOT) == hook,
          'console task started twice or hook not reinstalled')
    s.keys(KEY_EFFECT)
    # printf text from any task lands in the ring
    sp = m.obj_alloc(64)
    m.wr(sp, b'lens: hello from printf\n\0')
    m.call_addr(hook, sp)
    rc, out = console_exec(m, 'log')
    check(rc > 0 and 'lens: hello from printf\r\n' in out, f'log: {out[-200:]!r}')
    if ctx.variant == 'debug':
        check('console: on' in out, 'CFW log lines not mirrored into the console')
    # sh: stock command, its result text
    m.shell_rc, m.shell_result = 7, 'done'
    rc, out = console_exec(m, 'sh  wsys  ')
    check(m.shell_lines == ['wsys  '] and out.endswith('result: 7 <done>\r\n'), f'sh: {m.shell_lines} {out!r}')
    m.shell_rc, m.shell_result = -1, None
    rc, out = console_exec(m, 'sh bogus')
    check(out.endswith('result: -1\r\n'), f'sh -1: {out!r}')
    # tasks: names from the TSK object table
    obj = m.obj_alloc(0x20)
    name = m.obj_alloc(16)
    m.wr(name, b'Telnetd\0')
    m.wr(obj, struct.pack('>IIIII', 0x54534B20, 0, 0xA0912345, 0x3000, name))
    m.w32(machine.A_TSK_OBJ_TABLE + 4 * 3, obj)
    m.rtos_tasks = {1: {'stat': 1, 'pri': 0x10}, 3: {'stat': 4, 'pri': 0x96}}
    rc, out = console_exec(m, 'tasks')
    rows = out.split('\r\n')
    check(len(rows) == 4 and rows[1].startswith('0x00000001 ?') and 'RUN' in rows[1], f'tasks: {rows}')
    check(rows[2].split() == ['0x00000003', 'Telnetd', 'WAIT', '150', '0xA0912345', '0x00003000'],
          f'tasks row 3: {rows[2]!r}')
    # heap: pool section present, walk bounded
    rc, out = console_exec(m, 'heap')
    check('memory pools' in out and 'pool map' in out and out.count('\r\n') < 200, f'heap: {out[:300]!r}')
    # mem: bytes from the firmware image, bounds
    rc, out = console_exec(m, 'mem A0020400 20')
    want = m.rd(0xA0020400, 16)
    first = out.split('\r\n')[0]
    check(first.startswith('A0020400 ') and bytes.fromhex(''.join(first[9:9 + 48].split())) == want,
          f'mem: {first!r}')
    check(len(out.split('\r\n')) == 3, 'mem 0x20 is not two rows')
    rc, out = console_exec(m, 'mem 0x1000')
    check(out.startswith('only DRAM'), f'mem outside DRAM: {out!r}')
    rc, out = console_exec(m, 'mem 0xBFFFFFF0 0x40')
    check(out.startswith('only DRAM'), 'mem past the end of DRAM accepted')
    # status, unknown, truncation, exit
    rc, out = console_exec(m, 'status')
    check(out.startswith('gr2_cfw ') and 'Auto ISO in M' in out and 'capture on' in out, f'status: {out[:200]!r}')
    m.shell_rc, m.shell_result = 3, None
    rc, out = console_exec(m, 'ver')                           # not a console command: runs as a stock command
    check(m.shell_lines[-1] == 'ver' and out == 'result: 3\r\n', f'stock fallthrough: {out!r}')
    rc, out = console_exec(m, 'wifi')
    check(out.count('\r\n') == 8 and 'Wi-Fi status (prop 0x260)' in out, f'wifi: {out!r}')
    rc, out = console_exec(m, 'help', size=40)
    check(rc == 39 and len(out) == 39, f'truncation: rc {rc}, {len(out)} chars')
    check(console_exec(m, '  exit ')[0] == -1, 'exit not reported')
    # one telnet session: option negotiation skipped, CR/LF lines, DEL edits, empty lines ignored, exit ends it
    m.sock_in = [b'\xff\xfb\x01\xff\xfd\x03he', b'lp\r\n', b'\r\n', b'frob\x7fx\r', b'exit\r\n', b'status\r\n']
    m.call('session', 5)
    out = m.sock_out.decode('latin-1')
    check(out.startswith('gr2_cfw ') and 'developer console' in out and out.count('\r\n> ') == 3, f'session: {out!r}')
    check('mem <addr> [len]' in out and out.endswith('result: 3\r\n> ') and m.shell_lines[-1] == 'frox',
          f'session tail: {out[-80:]!r}')
    check(m.sock_in == [b'status\r\n'], 'session read past exit')
    finish(ctx, s)


# ---- export (src/export.c): recipe keys -> slot properties, worked out here independently of the CFW -------------

EXPORT_BASES = {'standard': (0, 0), 'vivid': (1, 0), 'setting1': (3, 0), 'setting2': (4, 0), 'bright': (2, 1),
                'bw': (5, 1), 'bw_te': (6, 1), 'hicontrast_bw': (7, 1), 'cross_process': (8, 1),
                'positive_film': (9, 1), 'bleach_bypass': (10, 1), 'retro': (11, 1), 'miniature': (12, 1),
                'high_key': (13, 1), 'slight': (14, 1), 'shift_crop': (15, 1), 'clarity': (16, 1),
                'brilliance': (17, 1), 'vibrant': (18, 1), 'hdr_tone': (19, 1), 'portrait': (20, 1)}   # (mode, effect)
SLIDERS = {'saturation': 0x2E, 'contrast': 0x46, 'sharpness': 0x5E, 'vignetting': 0x76, 'hue': 0x8E,
           'toning': 0xA6, 'clarity': 0xBE, 'glossiness': 0xD6}
WB_VALUE = {'auto': 0, 'multi_auto': 1, 'daylight': 2, 'shade': 3, 'cloudy': 4, 'incandescent1': 5,
            'incandescent2': 6, 'fl_daylight': 7, 'fl_neutral': 8, 'fl_cool': 9, 'fl_warm': 10, 'cte': 12, 'kelvin': 13}
PLAIN_KEYS = {'wb': 0xFE, 'wb_kelvin': 0x10F, 'exposure': 0xFD, 'dr': 0x130, 'nr': 0xEF, 'iso': 0x110,
              'auto_hi_max': 0x111, 'auto_hi_min': 0x112, 'auto_hi_shutter': 0x113}
LOOK_KEY = re.compile(r'matrix|post_matrix|tone(_[rgb])?|hue\d+|halation(_size|_boost)?')
NAME_IDS = range(0x1A9, 0x1BA)


def recipe_kv(data):
    """(key, value) pairs of a recipe file; comments and blank lines dropped."""
    out = []
    for line in data.decode('ascii').split('\r\n'):
        line = line.split('#', 1)[0].strip()
        if line:
            k, v = line.split('=', 1)
            out.append((k.strip(), v.strip()))
    return out


def key_props(kv):
    """Slot property IDs the keys of one recipe set (src/recipes.c recipe_props)."""
    d = dict(kv)
    mode, effect = EXPORT_BASES[d['base']]
    ids = {0x2B} | (set() if effect else {0x2C})
    ids |= {SLIDERS[k] + mode for k in d if k in SLIDERS} | {PLAIN_KEYS[k] for k in d if k in PLAIN_KEYS}
    if d.get('nr') == 'custom':
        ids |= {0xF0, 0xF1, 0xF2}
    if 'wb_ab' in d or 'wb_gm' in d:
        ids.add(0xFF + WB_VALUE[d['wb']])
    return ids


def look_of(kv):
    """The look a recipe's keys give (src/recipes.c set_key: unset tone channels Standard, unset hue sectors unity)."""
    look, tone, hue = {}, None, None
    for k, v in kv:
        if not LOOK_KEY.fullmatch(k):
            continue
        if k.startswith('halation'):
            look[k] = v
            continue
        n = [int(x) for x in v.split(',')]
        if k in ('matrix', 'post_matrix'):
            look[k] = n
        elif k.startswith('tone'):
            tone = tone or [[min(255, 8 * i) for i in range(33)] for _ in range(3)]
            for c in range(3):
                if k in ('tone', 'tone_' + 'rgb'[c]):
                    tone[c] = n
        else:
            hue = hue or [[128, 0, 0, 0, 128, 0, 0, 0, 128] for _ in range(12)]
            hue[int(k[3:]) - 1] = n
    if tone:
        look['tone'] = tone
    if hue:
        look['hue'] = hue
    return look


def name_words(s):
    return list(struct.unpack('>17I', s.encode('utf-16-be').ljust(0x44, b'\0')))


def slot_name(v):
    return b''.join(struct.pack('>I', v[i]) for i in NAME_IDS).decode('utf-16-be').split('\0')[0]


def sc_export(ctx):
    """DEV "Export settings to card" and the console's `export` (src/export.c): (a) MY slots, boxes, live settings,
    Setting1/2 and changed effect sliders give the expected keys (Hi-Contrast B&W contrast, custom NR, ISO,
    AUTO-HI, comments for manual WB and a 1/2-step EV; a name with '#' changed; the camera's Clarity value under a CFW
    Clarity strength); nothing written at boot, no slot or property changed; (b) every file applied to MY3 sets each
    property its keys name to the source value and nothing else; (c) the slot linked to Portra exports Portra's look
    keys (also live with the dial on it), which parse to the same look, and MY1 linked to 800T (no slot change) its
    look with the recipe's three halation keys, no other file a halation key; (d) no card: an error, nothing
    written;
    (e) the export runs in the CFW task within its stack, also for the console (CFWcon waits in Task_Delay)."""
    import machine
    check = ctx.check
    mask = 0xFFFFFFFF
    dflt = list(struct.unpack_from('>' + 'I4x' * 899, ctx.inputs.firmware, machine.A_PROP_SETUP - machine.FW_BASE))

    def preset(vals, name=''):
        v = list(dflt)
        for i, x in vals.items():
            v[i] = x & mask
        v[0x1A9:0x1BA] = name_words(name)
        return v

    my1 = preset({0x15: 3, 0x2C: 4, 0x2E + 4: 7, 0x46 + 4: 3, 0x5E + 4: 6, 0x76 + 4: 1, 0xFE: 13, 0x10F: 6500,
                  0xFF + 13: (-3 << 16) | 2, 0xFD: 32, 0x130: 3, 0xEF: 5, 0xF0: 0x150, 0xF1: 0x1B0, 0xF2: 0x2D0,
                  0x110: 0x150, 0x111: 0x210, 0x112: 0x120, 0x113: 0x60}, 'Portra Look')
    my2 = preset({0x15: 2, 0x2B: 8, 0x2C: 3, 0x2E + 8: 3, 0x46 + 8: 6, 0x5E + 8: 5, 0x76 + 8: 2, 0x8E + 8: 2,
                  0xFE: 2, 0xFF + 2: (8 << 16) | (-8 & 0xFFFF), 0xFD: -48, 0x130: 6, 0xEF: 1, 0x110: 0x989813},
                 'XPRO #2')
    box1 = preset({0x2B: 7, 0x46 + 7: -2, 0x5E + 7: 8, 0x76 + 7: 3, 0xFE: 11, 0x2BB: 0x1234, 0x2BC: 0x5678,
                   0xFD: 0x18, 0xEF: 0, 0x110: 0x1B0}, 'HICON')
    box3 = preset({0x2B: 15})
    slots = {'B:/P_MY00.bin': my1, 'B:/P_MY01.bin': my2, 'B:/P_BOX00.bin': box1, 'B:/P_BOX02.bin': box3}
    live = {i: dflt[i] for i in range(1, 899) if i not in machine.DEFAULT_PROPS and not 0x1BA <= i <= 0x1C2}
    live.update({5: 2, 6: 0, 0x2B: 0, 0x2C: 3, 0x2D: 3, 0x2E + 3: 6, 0x46 + 3: 4, 0x76 + 3: 0, 0x2E + 9: 8, 0xFE: 4})
    cfg = b'rc1.0=504F5254\r\nrc1.1=52412E54\r\nrc1.2=58540000\r\nclarity=00000009\r\n'   # MY1 = PORTRA.TXT
    cfg800 = b'rc1.0=43383030\r\nrc1.1=542E5458\r\nrc1.2=54000000\r\nclarity=00000009\r\n'   # MY1 = C800T.TXT
    portra = recipe_kv((CFW / 'recipes' / 'portra.txt').read_bytes())
    c800t = recipe_kv((CFW / 'recipes' / 'c800t.txt').read_bytes())

    s = Session(ctx, files={'H:/cfw/config.txt': cfg}, props=dict(live), slots=slots)
    m = s.m
    check(m.props[0xCE] == 9, 'CFW Clarity strength 9 not in force')
    check(not [e for e in m.io if 'EXPORT' in e[1].upper() or 'P_BOX' in e[1]], 'export files touched at boot')
    live_src = [m.props.get(i, dflt[i]) for i in range(899)]
    live_src[0xCE] = 7                                          # the camera's own Clarity value
    before, sets, io0 = {k: list(v) for k, v in m.slots.items()}, len(m.prop_sets), len(m.io)
    m.stack_margin = 1 << 30
    s.request(MENU_SETTINGS)
    s.keys(KEY_LEFT, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_OK)   # DEV: Export settings to card
    out = {m.names[k].rsplit('/', 1)[1]: m.file(k) for k in m.files if k.startswith('H:/CFW/EXPORT/')}
    want = ['box01.txt', 'box03.txt', 'fx09.txt', 'live.txt', 'my1.txt', 'my2.txt', 'set1.txt', 'set2.txt']
    check(sorted(out) == want, f'export files {sorted(out)}')
    check(s.info(1, 3) == f'{len(want)} files in cfw/export', f'DEV row: {s.info(1, 3)!r}')
    check(m.slots == before and len(m.prop_sets) == sets, 'the export changed a slot file or a property')
    check(all(t == 'CFW' for _, _, t in m.io[io0:]) and m.stack_margin >= 0,
          f'export outside the CFW task or over its stack (margin {m.stack_margin} B)')
    for f, d in out.items():
        check(d.isascii() and b'\n' not in d.replace(b'\r\n', b'') and len(d) <= 4094 and d.startswith(b'# '),
              f'{f}: not an ASCII CRLF recipe of at most 4094 bytes')
    keys = {f: {k: v for k, v in recipe_kv(d) if not LOOK_KEY.fullmatch(k)} for f, d in out.items()}
    auto_hi = {'auto_hi_max': '3200', 'auto_hi_min': '100', 'auto_hi_shutter': 'auto'}   # factory values
    expect = {
        'my1.txt': {'name': 'Portra Look', 'base': 'setting2', 'saturation': '2', 'contrast': '-2', 'sharpness': '1',
                    'vignetting': 'weak', 'wb': 'kelvin', 'wb_kelvin': '6500', 'wb_ab': '2', 'wb_gm': '-3',
                    'exposure': '+0.7', 'dr': 'weak', 'nr': 'custom', 'nr_weak': '400', 'nr_medium': '1600',
                    'nr_strong': 'off', 'iso': '400', 'auto_hi_max': '6400', 'auto_hi_min': '200',
                    'auto_hi_shutter': '1/4'},
        'my2.txt': {'name': 'XPRO _2', 'base': 'cross_process', 'saturation': '-2', 'contrast': '1', 'sharpness': '0',
                    'vignetting': 'medium', 'hue': 'magenta', 'wb': 'daylight', 'wb_ab': '-8', 'wb_gm': '8',
                    'exposure': '-1.0', 'dr': 'auto', 'nr': 'weak', 'iso': 'auto_hi', **auto_hi},
        'box01.txt': {'name': 'HICON', 'base': 'hicontrast_bw', 'contrast': '-2', 'sharpness': '3',
                      'vignetting': 'strong', 'dr': 'off', 'nr': 'off', 'iso': '1600', **auto_hi},   # no wb, no exposure
        'set1.txt': {'name': 'Setting1', 'base': 'setting1', 'saturation': '1', 'contrast': '-1', 'sharpness': '0',
                     'vignetting': 'off'},
        'set2.txt': {'name': 'Setting2', 'base': 'setting2', 'saturation': '0', 'contrast': '0', 'sharpness': '0',
                     'vignetting': 'off'},
        'fx09.txt': {'name': 'Camera positive_film', 'base': 'positive_film', 'saturation': '3', 'contrast': '0',
                     'sharpness': '0', 'vignetting': 'off'},
    }
    for f, kv in expect.items():
        check(keys[f] == kv, f'{f}: keys {keys[f]}')
    check(keys['box03.txt']['name'] == 'Box 3' and keys['box03.txt']['base'] == 'shift_crop', 'box03.txt')
    check(keys['live.txt']['name'] == 'Live' and keys['live.txt']['base'] == 'setting1' and
          keys['live.txt']['wb'] == 'cloudy', f'live.txt: {keys["live.txt"]}')
    # (c) MY1 holds Portra: its look keys parse to Portra's look; MY2 and the rest have none.
    check(look_of(portra) and look_of(recipe_kv(out['my1.txt'])) == look_of(portra), 'my1.txt: not Portra\'s look')
    check(not any(look_of(recipe_kv(d)) for f, d in out.items() if f != 'my1.txt'), 'look keys in another file')
    check(not any(k.startswith('halation') for d in out.values() for k, _ in recipe_kv(d)), 'halation keys')
    # ... and with MY1 on 800T (same base) its look with the halation keys.
    s8 = Session(ctx, files={'H:/cfw/config.txt': cfg800}, props=dict(live), slots=slots)
    s8.m.stack_margin = 1 << 30
    s8.request(MENU_SETTINGS)
    s8.keys(KEY_LEFT, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_OK)
    e800 = next((s8.m.file(k) for k in s8.m.files if k.upper() == 'H:/CFW/EXPORT/MY1.TXT'), b'')
    got, want_l = look_of(recipe_kv(e800)), look_of(c800t)
    check(want_l and got == want_l, f'my1.txt on 800T: not 800T\'s look (keys differing: '
          f'{sorted(k for k in set(got) | set(want_l) if got.get(k) != want_l.get(k))})')
    halo = {k: v for k, v in recipe_kv(e800) if k.startswith('halation')}
    want_h = {k: v for k, v in c800t if k.startswith('halation')}
    check(len(want_h) == 3 and halo == want_h, f'my1.txt on 800T: halation keys {halo}, recipe {want_h}')
    # (d) no card: an error on the row, nothing written anywhere.
    files, io1 = {k: bytes(v) for k, v in m.files.items()}, len(m.io)
    m.card = False
    s.keys(KEY_OK)
    check(s.info(1, 3) == 'no card', f'no card: {s.info(1, 3)!r}')
    check({k: bytes(v) for k, v in m.files.items()} == files and m.slots == before and
          all(e[0] in ('File_Mkdir', 'File_Open') and e[1].upper().startswith('H:/CFW') for e in m.io[io1:]),
          f'no card: something written: {m.io[io1:]}')
    m.card = True
    s.keys(KEY_EFFECT)
    # (e) the stub's stack check is live on this path: a 0x2000-byte task stack fails.
    try:
        m.call_addr(m.syms['export_run'] | 1, stack=0x2000)
        check(False, 'export_run on an 8 KB stack passed the file-API stack check')
    except machine.EmuError as ex:
        check('stack' in str(ex), f'small stack: {ex}')
    finish(ctx, s)

    # (b) Round trip: every exported file, from cfw/recipes, applied to MY3 (never saved before).
    src = {'my1.txt': my1, 'my2.txt': my2, 'box01.txt': box1, 'box03.txt': box3, 'live.txt': live_src,
           'set1.txt': live_src, 'set2.txt': live_src, 'fx09.txt': live_src}
    s2 = Session(ctx, files={'H:/cfw/recipes/' + f: d for f, d in out.items()}, props=dict(live))
    m2 = s2.m
    names = s2.recipe_names()
    for f, d in sorted(out.items()):
        kv = recipe_kv(d)
        i = names.index(dict(kv)['name'])
        old = list(m2.slots.get('B:/P_MY02.bin', dflt))
        check(m2.call('recipes_apply', i, 2) == 0, f'{f}: not applied')
        slot = m2.slots['B:/P_MY02.bin']
        ids = key_props(kv)
        want_v = {p: src[f][p] for p in ids}
        if f[:2] in ('se', 'fx'):                               # image presets: the base is the preset's mode
            mode, effect = EXPORT_BASES[dict(kv)['base']]
            want_v.update({0x2B: mode if effect else 0, **({} if effect else {0x2C: mode})})
        bad = [hex(p) for p, v in want_v.items() if slot[p] != v]
        check(not bad, f'{f} applied to MY3: properties not the exported values: {bad}')
        other = [hex(p) for p in range(899) if slot[p] != old[p] and p not in ids and p not in NAME_IDS]
        check(not other, f'{f} applied to MY3 changed properties no key names: {other}')
        check(slot_name(slot) == dict(kv)['name'], f'{f}: MY3 name {slot_name(slot)!r}')
    # (c) the CFW parses my1.txt to Portra's look, and MY3 now holds it.
    mono = m2.obj_alloc(4)
    look = lambda n: m2.rd(m2.call('recipes_look', names.index(n), mono), 353)
    check(look('Portra Look') == look('Portra 400'), 'my1.txt parsed to another look than portra.txt')
    i = names.index('Portra Look')
    check(m2.call('recipes_apply', i, 2) == 0 and 'rc3.0=4D59312E' in m2.cfg_text(), 'MY3 does not hold MY1.TXT')
    finish(ctx, s2)

    # Console `export` with the dial on MY1: the CFW task runs it while CFWcon waits; live carries MY1's look,
    # Setting1 comes from B:/P_COMMON.bin.
    live_b = {i: my1[i] for i in live}
    live_b.update({5: 3, 6: 1, 0x2D: 4})
    slots_b = {**slots, 'B:/P_COMMON.bin': preset({0x2C: 3, 0x2E + 3: 9})}
    s3 = Session(ctx, files={'H:/cfw/config.txt': cfg}, props=live_b, slots=slots_b)
    m3 = s3.m
    m3.on_delay = lambda mm: mm.call_in_task('cfw_task_poll')
    io0, m3.stack_margin = len(m3.io), 1 << 30
    rc, text = console_exec(m3, 'export', task='CFWcon')
    writes = [e for e in m3.io[io0:] if e[1].upper().startswith('H:/CFW/EXPORT/')]
    check('7 files in cfw/export' in text and len(writes) == 7 and all(e[2] == 'CFW' for e in writes),
          f'console export: {text!r}, writes {writes}')
    check(m3.stack_margin >= 0, f'console export: over the CFW task stack (margin {m3.stack_margin} B)')
    check(look_of(recipe_kv(m3.file('H:/cfw/export/live.txt'))) == look_of(portra), 'live.txt on MY1: no Portra look')
    set1 = dict(recipe_kv(m3.file('H:/cfw/export/set1.txt')))
    check(set1['saturation'] == '4', f'set1.txt not from P_COMMON: {set1}')
    m3.on_delay = None                                          # the CFW task does not run: the console gives up
    rc, text = console_exec(m3, 'export', task='CFWcon')
    check(rc > 0 and 'files' not in text, f'console export without the CFW task: {text!r}')
    finish(ctx, s3)


def sc_loader(ctx):
    """Flashed loader (stub v2): stage 1 header, hook(n) from section 3 on (preload at the first hook, CFWldr task +
    disarm at 13), M10c patch-on-load at hook(10), the CFW settings and the MY slot's look at the IQ-ready call (after
    CapInit(5) loaded AE_F, never before), the CFW task from hook(10) in phases (APP, card, late), nothing written to H:
    before step 13, no double install or task by cfw_main; USB boot (section 1 only) runs no CFW code at all; missing
    stage 2 is logged only once the task runs; Startup.ttl refuses while the loader is active; no step 13 -> timeout,
    CFW not started."""
    import machine
    check = ctx.check
    s1 = (CFW / 'bin' / ctx.variant / 'gr2_cfw.bin').read_bytes()
    check(struct.unpack_from('>HHI', s1, 0)[2] == machine.LDR_MAGIC and s1[0] >> 3 == 0x1C,
          'stage 1 header: no b over the header or no GRL1 magic')
    check(struct.unpack_from('>I', s1, 8)[0] == ctx_sym1(ctx, 'cfw_ldr_hook') | 1, 'hook entry at +8')
    # full loader boot, Portra remembered for MY2 (Setting2) and the dial on MY2
    rec = ctx.recipes
    portra = next(r for r in rec if r['file'] == 'portra.txt')
    cfg = b'rc2.0=504F5254\r\nrc2.1=52412E54\r\nrc2.2=58540000\r\nclarity=00000009\r\n'
    aef = (machine.A_AEF, 0x2D8)                                # AE_F, loaded by CapInit(5) (finding 35)
    site, seen = 0xA03A1F92, {}                                 # Effect press BL (finding 16 section 4)
    iq_site = 0xA01B584A                                        # IQ-ready BL in CapInit(5) (finding 32)

    def shown(mm, flags):
        view = object.__new__(Session)
        view.m = mm
        return view.look_shown(flags)

    def task(mm):
        mm.call('cfw_task_poll')
        return tuple(mm.u32(mm.syms[k]) for k in ('g_task_started', 'g_task_fonts', 'g_task_late'))

    def on_hook(mm, n):
        seen.setdefault('card', mm.file('H:/cfw/cfw_log.txt') is not None)
        seen['card'] |= mm.file('H:/cfw/cfw_log.txt') is not None
        if n == machine.LOADER_DECIDE_SECTION:                  # the first hook: JobSystem step 2 ran later
            seen['look_before_props'] = not shown(mm, {})
            mm.w32(machine.A_MODE_PROPS_FINAL, 1)
            seen['iq_ctx'] = mm.u32(machine.A_IQ_CTX_PTR)       # IQ.bin is loaded only inside CapInit(5)
            mm.w32(machine.A_IQ_CTX_PTR, 0)
            seen['aef'] = mm.rd(*aef)
            mm.wr(aef[0], bytes(aef[1]))
        if n == 5:
            seen['look_before_iq'] = not shown(mm, {})
            seen['clarity_before_iq'] = mm.props[0xCE]
            seen['iq_bl'] = (mm.u16(iq_site), mm.u16(iq_site + 2))
            mm.w32(machine.A_IQ_CTX_PTR, seen['iq_ctx'])
            mm.wr(aef[0], seen['aef'])
            mm.run_range(iq_site | 1, iq_site + 4, stack=machine.ROOT_TASK_STACK, task='boot')   # CapInit(5)
            seen['look_at_iq'] = shown(mm, {3: portra['flags']})
            seen['clarity_at_iq'] = mm.props[0xCE]
            seen['iq_stock'] = mm.stock_calls.count(('iq_ready',))
            seen['bl_before_10'] = mm.u16(site)
        if n == 10:
            seen['bl_at_10'] = mm.u16(site)
            seen['tasks_at_10'] = sum(t['name'] == 'CFW' for t in mm.tasks)
            seen['task_before_app'] = task(mm)                  # JobSystem step 10 not done yet
            mm.boot_step_done(10)
            seen['task_app'] = task(mm)                         # loop live; the card is being remounted later
        if n == 6:                                              # first hook after hook(9): remount over
            seen['task_card'] = task(mm)
        if n == 13:
            seen['task_13'] = task(mm)                          # cfw_main has not run: still not late
    s = Session(ctx, files={'H:/cfw/config.txt': cfg}, props={0x2D: 4, 6: 2, 0xCE: 5}, loader=True, on_hook=on_hook)
    m = s.m
    cfw_bl = encode_bl(site, m.syms1['cfw_veneer_press'])[0]
    ldr = [t for t in m.tasks if t['name'] == 'CFWldr']
    check(s.rc == 0 and len(ldr) == 1 and ldr[0]['entry'] & ~1 == m.syms1['ldr_task'] & ~1, 'CFWldr task')
    check(m.u32(machine.LDR_STATE) == 6, 'loader not disarmed after section 13')
    check(not seen['card'], 'H: written during the section-load hooks')
    check(seen['iq_bl'] == encode_bl(iq_site, m.syms1['cfw_veneer_iq']), f'IQ-ready BL not patched at hook(5): {seen["iq_bl"]}')
    check(m.u32(m.syms1['cfw_veneer_iq_target']) & ~1 == m.syms['iq_ready'] & ~1, 'IQ veneer target')
    check(seen['bl_before_10'] != cfw_bl and seen['bl_at_10'] == cfw_bl and m.u16(site) == cfw_bl,
          f'press BL not patched at hook(10): {seen.get("bl_before_10", 0):#x} {seen.get("bl_at_10", 0):#x}')
    check(not seen['look_before_props'] and not seen['look_before_iq'] and seen['look_at_iq'] and seen['iq_stock'] == 1,
          'MY2 look not applied at the IQ-ready call (before live view), or the stock call not made once')
    check(s.look_shown({3: portra['flags']}), 'MY2 look lost after the task start')
    check(seen['clarity_before_iq'] == 5 and seen['clarity_at_iq'] == 9, 'Clarity not applied at the IQ-ready call')
    check(seen['tasks_at_10'] == 1 and sum(t['name'] == 'CFW' for t in m.tasks) == 1,
          'CFW task not created once at hook(10)')
    check(seen['task_before_app'] == (0, 0, 0) and seen['task_app'] == (1, 0, 0) and seen['task_card'] == (1, 1, 0)
          and seen['task_13'] == (1, 1, 0) and task(m) == (1, 1, 1),
          f'task phases: {[seen[k] for k in ("task_before_app", "task_app", "task_card", "task_13")]}')
    check('clarity.s=00000005' in m.cfg_text(), f'MY2 Clarity not recorded as the stock value: {m.cfg_text()!r}')
    if ctx.variant == 'debug':
        log = m.log_lines()
        check(any('started by the flashed loader' in l for l in log), 'loader start not logged')
        check(sum('menu: CFW Menu / Recipes added' in l for l in log) == 1, 'menus installed twice or never')
        check(any(l.startswith('hook: (n, systime) 0x0000000A') for l in log), 'hook timeline not logged')
        check(log[0] == 'safeboot: state (before, after) 0x00000000 0x00000001' and log[1].startswith('=== gr2_cfw'),
              f'held lines out of order (stage 1, then stage 2): {log[:2]}')
    finish(ctx, s)

    def bare(main=True):
        mm = machine.Machine(ctx.inputs, ctx.variant, str(CFW))
        ctx.machines.append(mm)
        if main:
            mm.put_file('H:/cfw/gr2_main.bin', mm.inputs_main())
        mm.wr(machine.WINDOW, s1)
        return mm

    # USB/PC boot: only section 1 loads, so the stub never decides and no CFW code runs
    mu = bare()
    before = mu.u32(machine.LDR_STATE)
    check(mu.loader_hooks([1]) == [] and mu.u32(machine.LDR_STATE) == before, 'USB boot: state')
    check(not mu.tasks and not mu.allocs, 'USB boot: CFW code ran')
    # stage 2 missing: muted at the first hook, logged by the task
    mm = bare(main=False)
    mm.loader_hooks(machine.LOADER_SHOOTING)
    check(mm.file('H:/cfw/cfw_log.txt') is None, 'card written at the first hook')
    check([t['name'] for t in mm.tasks] == ['CFWldr'], 'CFWldr not started after a failed preload')
    mm.boot_step_done(13)
    rc = mm.run_ldr_task()
    log = mm.log_lines()
    check(rc == 0xFFFFFFFF and any('gr2_main.bin missing' in l for l in log) and
          any('not loaded at the first hook' in l for l in log), f'missing stage 2: {log}')
    # Startup.ttl while the loader runs this stage 1
    rc = mm.call_addr(mm.syms1['_start'] | 1, stack=machine.SCRIPT_TASK_STACK, task='Script')
    check(rc == 0xFFFFFFFF and any('flashed loader active' in l for l in mm.log_lines()), 'double load not refused')
    check(mm.u32(mm.syms1['g_boot']) == 0, 'stage 2 started twice')
    mm.files.pop('H:/CFW/CFW_LOG.TXT')                         # expected ERROR lines
    # JobSystem step 13 never completes
    mt = bare()
    mt.loader_hooks(machine.LOADER_SHOOTING)
    rc = mt.run_ldr_task()
    check(rc == 0xFFFFFFFF and any('not done after 60 s' in l for l in mt.log_lines()), 'no timeout')
    mt.boot_step_done(10)
    mt.call('cfw_task_poll')
    check([t['name'] for t in mt.tasks].count('CFW') == 1 and mt.u32(mt.syms['g_task_late']) == 0,
          'CFW task not started at hook(10), or its late phase (settings, saves) ran without step 13')
    mt.files.pop('H:/CFW/CFW_LOG.TXT')


def ctx_sym1(ctx, name):
    import machine
    return machine.Symbols(str(CFW / 'bin' / ctx.variant / 'gr2_cfw.elf'))[name]


def sc_store(ctx):
    """Config on D: (src/store.c) with a card backup: a D: without config (first start, or formatted by stock after a
    battery pull, finding 34 section 11) takes the card copy, the two generations alternate, a torn newest file falls
    back to the older one and is the next to be rewritten, D: wins over a stale card copy, nothing gives defaults."""
    check = ctx.check
    S_MAUTO = 14
    card = b'mauto=00000001\r\n'
    s = Session(ctx, files={'H:/cfw/config.txt': card})
    m = s.m
    check('mauto=00000001' in m.cfg_text() and m.file('H:/cfw/config.txt').decode() == m.cfg_text(),
          'card config not imported, or the backup copy differs')
    check(m.store('D:/CFW/CFG_A.DAT', 'D:/CFW/CFG_B.DAT') is not None and m.file('D:/CFW/CFG_B.DAT') is None,
          'first save not in CFG_A')
    m.call('settings_step', S_MAUTO, 1)                         # ON -> OFF
    m.call('settings_save')
    a, b = m.file('D:/CFW/CFG_A.DAT'), m.file('D:/CFW/CFG_B.DAT')
    check(b is not None and 'mauto=00000000' in m.cfg_text() and b'mauto=00000001' in a,
          'second save did not go to CFG_B, or touched CFG_A')
    finish(ctx, s)
    # Power lost while CFG_B was written: the next boot uses CFG_A and rewrites CFG_B, never CFG_A.
    d = m.d_files()
    d['D:/CFW/CFG_B.DAT'] = d['D:/CFW/CFG_B.DAT'][:20]
    s2 = Session(ctx, files=d)
    m2 = s2.m
    check(s2.setting(S_MAUTO) == 'ON' and m2.file('D:/CFW/CFG_A.DAT') == a, 'torn CFG_B: CFG_A not used as is')
    m2.call('settings_step', S_MAUTO, 1)
    m2.call('settings_save')
    check(m2.file('D:/CFW/CFG_A.DAT') == a and 'mauto=00000000' in m2.cfg_text(), 'save after a torn CFG_B')
    finish(ctx, s2)
    # D: valid, card copy stale (edited or old): D: wins.
    d2 = m2.d_files()
    s4 = Session(ctx, files={**d2, 'H:/cfw/config.txt': b'mauto=00000001\r\n'})
    check(s4.setting(S_MAUTO) == 'OFF', 'stale card copy preferred over D:')
    check(s4.m.file('H:/cfw/config.txt').decode() == s4.m.cfg_text(), 'stale card copy not rewritten from D:')
    finish(ctx, s4)
    # Fresh cfw/ folder (no card copy), D: valid: the copy is written again, so a later battery pull can restore it.
    s5 = Session(ctx, files=d2)
    check(s5.m.file('H:/cfw/config.txt').decode() == s5.m.cfg_text() == m2.cfg_text(), 'missing card copy not written')
    finish(ctx, s5)
    # Wiped D: (Reset Settings) and no card file: built-in defaults.
    s3 = Session(ctx)
    check(s3.setting(S_MAUTO) == 'OFF', 'empty D: and no card config: M auto not at its default')
    if ctx.variant == 'debug':
        check('config: none' in s3.m.log_lines(), 'empty D: not reported')
    finish(ctx, s3)


def sc_safeboot(ctx):
    """M12 safe boot (include/safeboot.h): OPSR bits 2-3 hold the state. A boot moves ARMED -> ONE_FAIL -> DISARMED
    and still runs the CFW; DISARMED boots stock (stage 2 never loaded) until a PLAY power-on re-arms it; a session
    ended by the power-off job, or the healthy mark 20 s after the task start, counts as healthy; "Boot stock once"
    gives one stock boot without undoing it; an invalid RTC runs the CFW (fail-open)."""
    import machine
    check = ctx.check
    s1 = (CFW / 'bin' / ctx.variant / 'gr2_cfw.bin').read_bytes()

    def boot(state, clean=0, ui_mode=1, rtc=True):
        mm = machine.Machine(ctx.inputs, ctx.variant, str(CFW), props={1: ui_mode})
        ctx.machines.append(mm)
        mm.put_file('H:/cfw/gr2_main.bin', mm.inputs_main())
        mm.wr(machine.WINDOW, s1)
        lo = 0x50 | state << 2
        mm.opsr, mm.rtc_valid = (~lo & 0xFF) << 8 | lo, rtc
        mm.w32(machine.A_LAST_OFF_CLEAN, clean)
        ran = mm.loader_hooks(machine.LOADER_SHOOTING)
        cfw = ran == machine.LOADER_SHOOTING[3:] and bool(mm.allocs)
        stock = ran == [3] and mm.u32(machine.LDR_STATE) == 6 and not mm.allocs and not mm.tasks
        check(cfw != stock, f'neither a CFW nor a stock boot: hooks {ran}, allocs {len(mm.allocs)}')
        return mm, cfw

    ARMED, ONE_FAIL, DISARMED, STOCK_ONCE = range(4)
    for want_cfw, want_after, kw, what in (
            (True, ONE_FAIL, dict(state=ARMED), 'armed'),
            (True, DISARMED, dict(state=ONE_FAIL), 'second unhealthy boot'),
            (True, ONE_FAIL, dict(state=ONE_FAIL, clean=1), 'last session ended by the power-off job'),
            (False, DISARMED, dict(state=DISARMED), 'disarmed'),
            (False, ARMED, dict(state=DISARMED, ui_mode=2), 'disarmed + PLAY power-on'),
            (False, ARMED, dict(state=STOCK_ONCE), 'boot stock once'),
            (True, DISARMED, dict(state=DISARMED, rtc=False), 'RTC invalid')):
        mm, cfw = boot(**kw)
        check(cfw == want_cfw and mm.safeboot_state() == want_after,
              f'{what}: CFW ran {cfw}, state after {mm.safeboot_state()} (want {want_cfw}, {want_after})')
    # Healthy mark 20 s after the task start; "Boot stock once" survives it.
    mm, _ = boot(ARMED)
    mm.boot_step_done(10)
    mm.time_ms = 15000
    for _ in range(50):
        mm.call('cfw_task_poll')
    check(mm.safeboot_state() == ONE_FAIL, 'healthy mark before 20 s')
    mm.time_ms = 25000
    for _ in range(50):
        mm.call('cfw_task_poll')
    check(mm.safeboot_state() == ARMED, 'no healthy mark after 20 s')
    mm.call('info_action', 1, 7)                               # DEV "Boot stock once"
    check(mm.safeboot_state() == STOCK_ONCE and mm.cstr(mm.call('info_value', 1, 7)) == 'next power-on: stock',
          'Boot stock once not set')



def sc_rom(ctx):
    """Release firmware (rom build, M11): stage 1's length/CRC fields; stage 1 from A: through the release-stub model,
    stage 2 and fonts from A: (no recipe files there); no card access in the boot hooks nor in the CFW task before
    step 13. First boot with an empty D: (first start, or stock formatted it after a battery pull) and a card config:
    settings_early is skipped (the camera's Clarity at the IQ-ready call, no early look); the late phase imports the
    card config, applies it, writes D: and the card copy, lists the card's recipes then Stock (the empty A: recipe
    folder quietly), shows MY2's look and saves the look cache; the log holds only ERROR lines (a broken card
    recipe). Next boot from that D: without a card: the config and MY2's look at the IQ-ready call, only Stock
    listed, no log. With recipe files added to A:, a card file with an A: file's 8.3 name replaces it. A stage 1
    with one flipped bit, or truncated, disarms at its first hook: no stage 2, no task, no file access; its ERROR
    line stays in stage 1's RAM log buffer."""
    import machine
    import stage1_crc
    check = ctx.check
    s1 = (CFW / 'bin' / 'rom' / 'gr2_cfw.bin').read_bytes()
    check(stage1_crc.check(s1) is None, f'bin/rom/gr2_cfw.bin: {stage1_crc.check(s1)}')
    portra = next(r for r in ctx.recipes if r['file'] == 'portra.txt')
    src = (CFW / 'recipes' / 'portra.txt').read_bytes()
    card_portra = src.replace(b'\r\nname=Portra 400\r\n', b'\r\nname=Portra Card\r\n')
    mine = (CFW / 'recipes' / 'gold.txt').read_bytes().replace(b'\r\nname=Gold 200\r\n', b'\r\nname=Mine\r\n')
    check(card_portra != src and b'name=Mine' in mine, 'recipe name lines not found')
    card = {'H:/cfw/config.txt': b'rc2.0=504F5254\r\nrc2.1=52412E54\r\nrc2.2=58540000\r\nclarity=00000009\r\n',
            'H:/cfw/recipes/portra.txt': card_portra, 'H:/cfw/recipes/mine.txt': mine,
            'H:/cfw/recipes/bad.txt': b'name=Bad\r\nbase=nope\r\n'}
    aef = (machine.A_AEF, 0x2D8)                                # AE_F, loaded by CapInit(5) (finding 35)
    iq_site = 0xA01B584A                                        # IQ-ready BL in CapInit(5) (finding 32)

    def boot(files, card=True, a_extra=None):
        seen = {}

        def on_hook(mm, n):
            if n == machine.LOADER_DECIDE_SECTION:              # JobSystem step 2 ran later, CapInit(5) not yet
                mm.w32(machine.A_MODE_PROPS_FINAL, 1)
                seen['iq_ctx'] = mm.u32(machine.A_IQ_CTX_PTR)
                mm.w32(machine.A_IQ_CTX_PTR, 0)
                seen['aef'] = mm.rd(*aef)
                mm.wr(aef[0], bytes(aef[1]))
            if n == 5:
                mm.w32(machine.A_IQ_CTX_PTR, seen['iq_ctx'])
                mm.wr(aef[0], seen['aef'])
                mm.run_range(iq_site | 1, iq_site + 4, stack=machine.ROOT_TASK_STACK, task='boot')   # CapInit(5)
                view = object.__new__(Session)
                view.m = mm
                seen['look_at_iq'] = view.look_shown({3: portra['flags']})
                seen['clarity_at_iq'] = mm.props[0xCE]
            if n == 10:
                mm.boot_step_done(10)
            if n in (10, 6):                                    # the early CFW task: loop live, fonts after hook(9)
                mm.call('cfw_task_poll')
            if n == 13:                                         # last hook: CFWldr / cfw_main / late phase follow
                seen['fonts'] = mm.u32(mm.syms['g_task_fonts'])
                seen['io'] = list(mm.io)
        s = Session(ctx, files=files, props={0x2D: 4, 6: 2, 0xCE: 5}, loader=True, on_hook=on_hook, cfw_files=False,
                    card=card, a_extra=a_extra)
        hooks_io = seen['io']
        check(hooks_io[0] == ('LoadFile', machine.ROM_STAGE1, 'stub') and
              [e for e in hooks_io if e[2] == 'boot'][:2] == [('LoadFile', 'A:/cfw/gr2_main.bin', 'boot')] * 2,
              f'stage 1 / stage 2 not read from A: by LoadFile: {hooks_io[:3]}')
        check(s.rc == 0 and s.m.u32(machine.LDR_STATE) == 6, 'not armed and disarmed after section 13')
        h = [e for e in hooks_io if e[1].upper().startswith('H:')]
        check(not h, f'card accessed before JobSystem step 13 (boot hooks, early CFW task): {h}')
        a_fonts = sorted(k for k in s.m.a_files if k.startswith('A:/cfw/fonts/'))
        fonts = sorted({e[1] for e in hooks_io if e[1].startswith('A:/cfw/fonts/') and e[2] == 'CFW'})
        check(seen['fonts'] == 1 and len(a_fonts) == 5 and fonts == a_fonts, f'fonts not read from A:: {fonts}')
        return s, seen

    # First boot: D: empty, card with config, two own recipes (one for MY2) and a broken one; no A: recipes.
    from rom_files import rom_files
    check(not [k for k in rom_files(CFW) if k.startswith('A:/cfw/recipes/')], 'recipe files on A: (scripts/rom_files.py)')
    s, seen = boot(card)
    m = s.m
    check(seen['clarity_at_iq'] == 5 and not seen['look_at_iq'],
          'empty D:: settings or look applied at the IQ-ready call (settings_early must be skipped)')
    check(m.props[0xCE] == 9 and 'clarity=00000009' in m.cfg_text() and 'rc2.0=504F5254' in m.cfg_text() and
          m.file('H:/cfw/config.txt').decode() == m.cfg_text(),
          f'card config not imported and saved in the late phase: {m.cfg_text()!r}')
    check(s.recipe_names() == ['Mine', 'Portra Card', 'Stock'], f'recipes: {s.recipe_names()}')
    check(('FindFirst', 'A:/cfw/recipes/', 'CFW') in m.io and ('File_Open', 'H:/cfw/recipes/PORTRA.TXT', 'CFW') in m.io,
          'A: recipe folder not listed or card recipe not read')
    check(s.look_shown({3: portra['flags']}), 'MY2 look not shown after the late phase')
    check(m.store('D:/CFW/LOOK_A.DAT', 'D:/CFW/LOOK_B.DAT') is not None, 'look cache not saved to D:')
    log = m.log_lines()
    check(log and all(l.startswith('ERROR') and 'BAD.TXT' in l for l in log), f'log: {log}')
    m.files.pop('H:/CFW/CFW_LOG.TXT')                           # the expected ERROR line
    finish(ctx, s)

    # Next boot: D: from the first one, no card.
    s2, seen2 = boot(m.d_files(), card=False)
    m2 = s2.m
    check(seen2['clarity_at_iq'] == 9 and seen2['look_at_iq'], 'D: config or MY2 look not applied at the IQ-ready call')
    check(s2.recipe_names() == ['Stock'], f'recipes: {s2.recipe_names()}')
    check(s2.look_shown({3: portra['flags']}) and m2.props[0xCE] == 9, 'MY2 look or Clarity lost after the late phase')
    check(not [e for e in m2.io if e[1].upper() == 'H:/CFW/CFW_LOG.TXT'], 'log written without an error')
    finish(ctx, s2)

    # A: with recipe files (none ship there; the scan stays): the card's portra.txt replaces the A: one.
    a_extra = {'A:/cfw/recipes/portra.txt': src, 'A:/cfw/recipes/gold.txt': (CFW / 'recipes' / 'gold.txt').read_bytes()}
    s3, _ = boot({**m2.d_files(), 'H:/cfw/recipes/portra.txt': card_portra}, a_extra=a_extra)
    m3 = s3.m
    check(s3.recipe_names() == ['Gold 200', 'Portra Card', 'Stock'], f'recipes: {s3.recipe_names()}')
    check(('LoadFile', 'A:/cfw/recipes/portra.txt', 'CFW') in m3.io and
          ('File_Open', 'H:/cfw/recipes/PORTRA.TXT', 'CFW') in m3.io, 'A: recipe or its card replacement not read')
    check(not m3.log_lines(), f'log: {m3.log_lines()}')
    finish(ctx, s3)

    # Stage 1 corrupt (one bit in its zero padding) or truncated (window tail stays 0xFF): disarmed at the first hook.
    flipped = bytearray(s1)
    flipped[len(s1) // 2] ^= 0x10
    for what, data in (('one flipped bit', bytes(flipped)), ('truncated', s1[:0x7000])):
        mm = machine.Machine(ctx.inputs, 'rom', str(CFW))
        ctx.machines.append(mm)
        mm.a_files[machine.ROM_STAGE1] = data
        ran = mm.loader_hooks(machine.LOADER_SHOOTING)
        held = mm.rd(mm.syms1['g_hold'], 2048)                  # stage 1's boot-task log buffer
        check(ran == [3] and mm.u32(machine.LDR_STATE) == 6 and not mm.allocs and not mm.tasks and
              mm.io == [('LoadFile', machine.ROM_STAGE1, 'stub')],
              f'{what}: hooks {ran}, state {mm.u32(machine.LDR_STATE)}, allocs {mm.allocs}, io {mm.io}')
        check(b'ERROR boot (loader): stage 1 length/CRC mismatch' in held, f'{what}: no ERROR line held')


SCENARIOS = [('boot', sc_boot), ('menu', sc_menu), ('snap', sc_snap), ('recipes', sc_recipes),
             ('palette', sc_palette), ('dev', sc_dev), ('recipe-files', sc_recipe_files), ('view', sc_view),
             ('ae-hooks', sc_ae_hooks), ('console', sc_console), ('export', sc_export), ('loader', sc_loader),
             ('pattern', sc_pattern), ('halation', sc_halation),
             ('store', sc_store), ('safeboot', sc_safeboot)]
ROM_SCENARIOS = [('rom', sc_rom)]          # the rom variant boots only through the release stub


# ---- main -----------------------------------------------------------------------------------------------

def run_variant(variant, inputs, renderer, only=None):
    import machine
    ctx = types.SimpleNamespace(variant=variant, inputs=inputs, renderer=renderer, check=Checks(),
                                machines=[], recipes=shipped_recipes())
    ok = True
    for name, fn in (ROM_SCENARIOS if variant == 'rom' else SCENARIOS):
        if only and name not in only:
            continue
        ctx.check.scenario = f'{variant}/{name}'
        try:
            fn(ctx)
            print(f'PASS {variant}/{name}')
        except Failed:
            ok = False
            print(f'FAIL {ctx.check.failures[-1]}')
        except machine.EmuError as ex:
            ok = False
            print(f'FAIL {variant}/{name}: emulation: {ex}')
        except Exception:
            ok = False
            print(f'FAIL {variant}/{name}: harness error')
            traceback.print_exc()
    if variant in ('release', 'rom'):
        logged = [m.file('H:/cfw/cfw_log.txt') for m in ctx.machines if m.file('H:/cfw/cfw_log.txt') is not None]
        if logged:
            ok = False
            print(f'FAIL {variant}/log: H:/cfw/cfw_log.txt written: {logged[0][:200]!r}')
        else:
            print(f'PASS {variant}/log (no cfw_log.txt)')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--variant', choices=['release', 'debug', 'rom'], help='default: all')
    ap.add_argument('--render', metavar='OUT_DIR', help='write PNGs of key screens')
    ap.add_argument('--scenario', action='append', metavar='NAME', help='run only these scenarios (repeatable)')
    args = ap.parse_args()
    inputs, missing = load_inputs()
    if inputs is None:
        print('ERROR firmware inputs not found (run make fw):\n  ' + '\n  '.join(missing))
        return 1
    variants = [args.variant] if args.variant else ['release', 'debug', 'rom']
    for v in variants:
        for f in ('gr2_cfw.bin', 'gr2_cfw.elf', 'gr2_main.bin', 'gr2_main.elf'):
            if not (CFW / 'bin' / v / f).is_file():
                print(f'ERROR bin/{v}/{f} missing: run make first')
                return 1
    renderer = None
    if args.render:
        import render
        renderer = render.Renderer(args.render)
    ok = all([run_variant(v, inputs, renderer, args.scenario) for v in variants])
    if renderer:
        print(f'rendered {len(renderer.written)} PNGs to {args.render}')
    print('emulator tests ' + ('passed' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
