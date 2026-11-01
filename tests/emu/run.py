#!/usr/bin/env python3
"""Emulator smoke test of the CFW (both stages, real code) against b01firm8.

    python3 tests/emu/run.py [--variant release|debug] [--render OUT_DIR]

Boots stage 1 like `fload` + `ex`, loads stage 2 from the modelled H: drive,
then drives the CFW task functions through the scenarios below. Exit 0 when
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

KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, KEY_EFFECT = 0x4206, 0x4207, 0x4208, 0x4209, 0x420A, 0x421C
MENU_SETTINGS, MENU_RECIPES = 0, 1
TABS = ['EXPOSURE', 'FOCUS', 'VIEW', 'IMAGE', 'INFO', 'DEV']
DOT = '\x86'                                   # GLYPH_DOT in the CFW fonts
NONE = 0xFFFF
TONE_SECONDARY = [NONE, 1, 2, NONE, 1, 2]      # src/look.c
LOOK_MATRIX, LOOK_TONE, LOOK_POST, LOOK_HUE = 1, 2, 4, 8
LOOK_BASES = {'standard': 0, 'vivid': 1, 'setting1': 3, 'setting2': 4}   # colour mode (prop 0x2D)
K5 = {0: 0, 1: 1, 3: 2, 4: 3}                  # colour mode -> IQ mode (look_mode)
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

def shipped_recipes():
    out = []
    for p in sorted((CFW / 'recipes').glob('*.txt')):
        kv = dict(re.findall(r'^\s*(\w+)\s*=\s*(.*?)\s*$', p.read_text('ascii').split('\x00')[0], re.M))
        flags = 0
        if kv.get('base') in LOOK_BASES:
            flags |= LOOK_MATRIX if 'matrix' in kv else 0
            flags |= LOOK_POST if 'post_matrix' in kv else 0
            flags |= LOOK_TONE if any(k == 'tone' or k.startswith('tone_') for k in kv) else 0
            flags |= LOOK_HUE if any(re.fullmatch(r'hue\d+', k) for k in kv) else 0
        mat = [int(x) for x in kv['matrix'].split(',')] if 'matrix' in kv else None
        out.append(dict(file=p.name, sfn=p.name.upper(), name=kv.get('name', ''), base=kv.get('base'),
                        mode=LOOK_BASES.get(kv.get('base')), flags=flags, matrix=mat))
    # src/recipes.c sort_list: case-insensitive by name, stable
    return sorted(out, key=lambda r: r['name'].lower())


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

    def __init__(self, ctx, files=None, props=None, slots=None, loader=False, on_hook=None):
        import machine
        self.ctx = ctx
        self.m = m = machine.Machine(ctx.inputs, ctx.variant, str(CFW), files=files, props=props, slots=slots)
        ctx.machines.append(m)
        for p in sorted((CFW / 'recipes').glob('*.txt')):
            m.put_file('H:/cfw/recipes/' + p.name, p.read_bytes())
        for p in sorted((CFW / 'recipes' / 'licenses').glob('*')):
            m.put_file('H:/cfw/recipes/licenses/' + p.name, p.read_bytes())
        for p in sorted((CFW / 'fonts').glob('*.fnt')):
            m.put_file('H:/cfw/fonts/' + p.name, p.read_bytes())
        self.rc = m.boot(loader=loader, on_hook=on_hook)
        task = [t for t in m.tasks if t['name'] == 'CFW']
        ctx.check(len(task) == 1 and task[0]['entry'] & ~1 == m.syms['cfw_task'] & ~1, 'CFW task not created')
        for f in ('settings_init', 'recipes_init', 'recipes_poll', 'osd_fonts_load'):
            m.call(f)

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
        m = self.m
        n = m.call('recipes_count')
        return [m.cstr(m.u32(m.syms['g_names'] + 4 * i)) for i in range(n)]


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
    """Boot, hooks, veneers, menu labels via the stock resolver, shell record, key input hook."""
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
    # EXPOSURE rows: TAv, Auto ISO slowest, Auto ISO max, hold, Auto-Hi slowest
    s.keys(KEY_DOWN, KEY_DOWN, KEY_RIGHT)
    check(s.setting(1) == '1/60 + crop' and aef() == [0x60, 0x30, 0x30, 0x50, 0, 0xFFD0], f'aisomin: {aef()}')
    s.keys(KEY_DOWN, KEY_RIGHT)
    check(s.setting(2) == 'ISO 400' and aef() == [0x60, 0x20, 0x20, 0x20, 0, 0xFFD0], f'aisomax: {aef()}')
    s.keys(KEY_DOWN, KEY_RIGHT)
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
    s.keys(KEY_LEFT, KEY_UP, KEY_LEFT, KEY_UP, KEY_LEFT)
    check(aef() == AE_STOCK, f'AE_F not stock after STOCK: {aef()}')
    # Auto-Hi slowest shutter (prop 0x113): Auto, 1/2, back to STOCK restores the camera value.
    s.keys(KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_RIGHT, KEY_RIGHT)
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
    cfg = (m.file('H:/cfw/config.txt') or b'').decode()
    check('aisomin=00000000' in cfg and 'clarity.s=00000007' in cfg, f'config not saved: {cfg!r}')
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
    """Recipes: preview repoints Setting2 lists, close restores, apply to MY2, dial, reboot."""
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
    check(s.look_shown({3: rec[-1]['flags']}), 'Right past the last recipe moved')
    pi = next(i for i, r in enumerate(rec) if r['file'] == 'portra.txt')
    s.keys(*[KEY_LEFT] * (len(rec) - 1 - pi))
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
    cfg = m.file('H:/cfw/config.txt').decode()
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
    # Reboot with that config and slot: the look comes back from config.txt.
    s2 = Session(ctx, files={'H:/cfw/config.txt': m.file('H:/cfw/config.txt')}, slots=m.slots,
                 props={0x2D: 4, 6: 2})
    check(s2.look_shown({3: rec[pi]['flags']}), 'after reboot: MY2 look not restored')
    finish(ctx, s2)


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
    check(s.info(1, 0) == 'OFF' and s.info(1, 6) == '-', f'before start: {s.info(1, 0)!r}')
    s.keys(KEY_OK)
    check(not m.msgs and m.props[0x1E8] == 0, f'Wi-Fi touched: msgs {m.msgs}')
    check([t['name'] for t in m.tasks] == ['CFW', 'CFWcon'], f'tasks: {[t["name"] for t in m.tasks]}')
    check(s.info(1, 0) == f'ON  {DOT}  Wi-Fi off (camera menu)', f'console row, Wi-Fi off: {s.info(1, 0)!r}')
    m.props[0x260] = 1                                         # turned on in the camera menu
    m.w32(m.syms['g_listen'], 3)                               # CFWcon has its listening socket
    check(s.info(1, 0) == f'ON  {DOT}  telnet 192.168.0.1' and s.info(1, 6) == 'GR_A1B2C3', 'Wi-Fi rows')
    m.call('cfw_ui_tick')
    s.render('menu_dev')
    s.keys(KEY_OK)                                             # OFF: socket closed, printf back on the UART
    check(s.info(1, 0) == 'OFF' and m.closed_fds == [3] and m.u32(machine.A_CONSOLE_HOOK_SLOT) == 0,
          f'console off: row {s.info(1, 0)!r}, closed {m.closed_fds}')
    s.keys(KEY_OK)
    check(len(m.tasks) == 2 and s.info(1, 0) != 'OFF', 'console on again: task started twice or still off')
    s.keys(KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_OK)
    dump = m.file('H:/cfw/iq_ram.bin')
    check(dump is not None and len(dump) == 0x2FAA0, f'IQ dump size {len(dump or b"")}')
    check(dump == m.rd(machine.IQ_BASE, 0x2FAA0) and s.info(1, 3) == 'iq_ram.bin saved', 'IQ dump content')
    s.keys(KEY_UP, KEY_OK)
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
    s.keys(KEY_LEFT, KEY_DOWN, *[KEY_DOWN] * 5)
    check(s.info(1, 5) == 'OFF', f'pattern row: {s.info(1, 5)!r}')
    s.keys(KEY_OK)
    check(not s.is_open() and s.info(1, 5) == 'RUNNING', f'pattern pending: {s.info(1, 5)!r}')
    m.time_ms += 1500
    m.call('info_poll')
    check(s.info(1, 5) == 'ON', f'pattern drawn: {s.info(1, 5)!r}')
    s.request(MENU_SETTINGS)
    s.keys(KEY_OK)
    m.time_ms += 1500
    m.call('info_poll')
    check(s.info(1, 5) == 'OFF', f'pattern removed: {s.info(1, 5)!r}')
    finish(ctx, s)


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
    """Every shipped recipe parses; the CFW lists them sorted by name."""
    check = ctx.check
    s = Session(ctx)
    names = s.recipe_names()
    check(names == [r['name'] for r in ctx.recipes], f'recipe list {names}')
    check(not [l for l in s.m.log_lines() if 'invalid line' in l or 'cannot read' in l], 'recipe parse errors')
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
    cfg = (m.file('H:/cfw/config.txt') or b'').decode('ascii')
    check('mauto=00000001' in cfg and 'tavmin=00000000' in cfg, f'config: {cfg!r}')
    finish(ctx, s)
    # Reboot with that config: M auto is applied at init. A foreign guard halfword blocks TAv min.
    s2 = Session(ctx, files={'H:/cfw/config.txt': cfg.encode('ascii')})
    m2 = s2.m
    check(m2.u16(MSV) == 0x000A, 'M auto not re-applied from the config')
    m2.w16(0xA00DC232, 0x0000)
    m2.call('settings_step', S_TAVMIN, 1)
    check(m2.u16(TAV) == 0x000A and s2.setting(S_TAVMIN) == 'ERR', 'patched over a foreign guard')
    errs = [l for l in m2.log_lines() if 'ERROR' in l]
    check(len(errs) == 1 and 'not as expected' in errs[0], f'guard error not logged: {errs}')
    m2.files.pop('H:/CFW/CFW_LOG.TXT')                 # the expected ERROR line (both variants log ERRORs)
    finish(ctx, s2)


def console_exec(m, line, size=8192):
    """Run one developer-console line in the CFW (console_exec); returns (rc, text)."""
    lp = m.obj_alloc(len(line) + 1)
    m.wr(lp, line.encode('latin-1') + b'\0')
    op = m.obj_alloc(size)
    rc = m.call('console_exec', lp, op, size)
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


def sc_loader(ctx):
    """Flashed loader: stage 1 header, hook(n) over a full boot (preload at 1, CFWldr task + disarm at 13),
    M10c patch-on-load at hook(10) and the MY slot's look as soon as the mode props are final, nothing written
    to H: before step 13, no double install by cfw_main; USB boot (section 1 only) writes nothing and starts
    nothing; missing stage 2 is logged only once the task runs; Startup.ttl refuses while the loader is
    active; no step 13 -> timeout, CFW not started."""
    import machine
    check = ctx.check
    s1 = (CFW / 'bin' / ctx.variant / 'gr2_cfw.bin').read_bytes()
    check(struct.unpack_from('>HHI', s1, 0)[2] == machine.LDR_MAGIC and s1[0] >> 3 == 0x1C,
          'stage 1 header: no b over the header or no GRL1 magic')
    check(struct.unpack_from('>I', s1, 8)[0] == ctx_sym1(ctx, 'cfw_ldr_hook') | 1, 'hook entry at +8')
    # full loader boot, Portra remembered for MY2 (Setting2) and the dial on MY2
    rec = ctx.recipes
    portra = next(r for r in rec if r['file'] == 'portra.txt')
    cfg = b'rc2.0=504F5254\r\nrc2.1=52412E54\r\nrc2.2=58540000\r\n'
    site, seen = 0xA03A1F92, {}                                 # Effect press BL (finding 16 section 4)

    def shown(mm, flags):
        view = object.__new__(Session)
        view.m = mm
        return view.look_shown(flags)

    def on_hook(mm, n):
        seen.setdefault('card', mm.file('H:/cfw/cfw_log.txt') is not None)
        seen['card'] |= mm.file('H:/cfw/cfw_log.txt') is not None
        if n == 4:                                              # JobSystem step 2's block has run
            seen['look_before_props'] = not shown(mm, {})
            mm.w32(machine.A_MODE_PROPS_FINAL, 1)
        if n == 2:
            seen['look_after_props'] = shown(mm, {3: portra['flags']})
        if n == 5:
            seen['bl_before_10'] = mm.u16(site)
        if n == 10:
            seen['bl_at_10'] = mm.u16(site)
    s = Session(ctx, files={'H:/cfw/config.txt': cfg}, props={0x2D: 4, 6: 2}, loader=True, on_hook=on_hook)
    m = s.m
    cfw_bl = encode_bl(site, m.syms1['cfw_veneer_press'])[0]
    ldr = [t for t in m.tasks if t['name'] == 'CFWldr']
    check(s.rc == 0 and len(ldr) == 1 and ldr[0]['entry'] & ~1 == m.syms1['ldr_task'] & ~1, 'CFWldr task')
    check(m.u32(machine.LDR_STATE) == 6, 'loader not disarmed after section 13')
    check(not seen['card'], 'H: written during the section-load hooks')
    check(seen['bl_before_10'] != cfw_bl and seen['bl_at_10'] == cfw_bl and m.u16(site) == cfw_bl,
          f'press BL not patched at hook(10): {seen.get("bl_before_10", 0):#x} {seen.get("bl_at_10", 0):#x}')
    check(not seen['look_before_props'] and seen['look_after_props'],
          'MY2 look not applied right after the mode props became final (or before)')
    check(s.look_shown({3: portra['flags']}), 'MY2 look lost after the task start')
    if ctx.variant == 'debug':
        log = m.log_lines()
        check(any('started by the flashed loader' in l for l in log), 'loader start not logged')
        check(sum('menu: CFW Menu / Recipes added' in l for l in log) == 1, 'menus installed twice or never')
        check(any(l.startswith('hook: (n, systime) 0x0000000A') for l in log), 'hook timeline not logged')
        check(log[0].startswith('=== gr2_cfw'), f'held lines out of order: {log[:2]}')
    finish(ctx, s)

    def bare(main=True):
        mm = machine.Machine(ctx.inputs, ctx.variant, str(CFW))
        ctx.machines.append(mm)
        if main:
            mm.put_file('H:/cfw/gr2_main.bin', mm.inputs_main())
        mm.wr(machine.WINDOW, s1)
        return mm

    # USB/PC boot: only section 1 loads
    mu = bare()
    check(mu.loader_hooks([1]) == [1] and mu.u32(machine.LDR_STATE) == 5, 'USB boot: state')
    check(not mu.tasks and mu.file('H:/cfw/cfw_log.txt') is None and mu.allocs, 'USB boot: task/log/preload')
    # stage 2 missing: muted at hook(1), logged by the task
    mm = bare(main=False)
    mm.loader_hooks(machine.LOADER_SHOOTING)
    check(mm.file('H:/cfw/cfw_log.txt') is None, 'card written at hook(1)')
    check([t['name'] for t in mm.tasks] == ['CFWldr'], 'CFWldr not started after a failed preload')
    mm.boot_step_done(13)
    rc = mm.run_ldr_task()
    log = mm.log_lines()
    check(rc == 0xFFFFFFFF and any('gr2_main.bin missing' in l for l in log) and
          any('not loaded at hook(1)' in l for l in log), f'missing stage 2: {log}')
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
    check(not [t for t in mt.tasks if t['name'] == 'CFW'], 'CFW started without step 13')
    mt.files.pop('H:/CFW/CFW_LOG.TXT')


def ctx_sym1(ctx, name):
    import machine
    return machine.Symbols(str(CFW / 'bin' / ctx.variant / 'gr2_cfw.elf'))[name]


SCENARIOS = [('boot', sc_boot), ('menu', sc_menu), ('snap', sc_snap), ('recipes', sc_recipes),
             ('palette', sc_palette), ('dev', sc_dev), ('recipe-files', sc_recipe_files), ('view', sc_view),
             ('ae-hooks', sc_ae_hooks), ('console', sc_console), ('loader', sc_loader), ('pattern', sc_pattern)]


# ---- main -----------------------------------------------------------------------------------------------

def run_variant(variant, inputs, renderer):
    import machine
    ctx = types.SimpleNamespace(variant=variant, inputs=inputs, renderer=renderer, check=Checks(),
                                machines=[], recipes=shipped_recipes())
    ok = True
    for name, fn in SCENARIOS:
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
    if variant == 'release':
        logged = [m.file('H:/cfw/cfw_log.txt') for m in ctx.machines if m.file('H:/cfw/cfw_log.txt') is not None]
        if logged:
            ok = False
            print(f'FAIL release/log: H:/cfw/cfw_log.txt written: {logged[0][:200]!r}')
        else:
            print('PASS release/log (no cfw_log.txt)')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--variant', choices=['release', 'debug'], help='default: both')
    ap.add_argument('--render', metavar='OUT_DIR', help='write PNGs of key screens')
    args = ap.parse_args()
    inputs, missing = load_inputs()
    if inputs is None:
        print('ERROR firmware inputs not found (run make fw):\n  ' + '\n  '.join(missing))
        return 1
    variants = [args.variant] if args.variant else ['release', 'debug']
    for v in variants:
        for f in ('gr2_cfw.bin', 'gr2_cfw.elf', 'gr2_main.bin', 'gr2_main.elf'):
            if not (CFW / 'bin' / v / f).is_file():
                print(f'ERROR bin/{v}/{f} missing: run make first')
                return 1
    renderer = None
    if args.render:
        import render
        renderer = render.Renderer(args.render)
    ok = all([run_variant(v, inputs, renderer) for v in variants])
    if renderer:
        print(f'rendered {len(renderer.written)} PNGs to {args.render}')
    print('emulator tests ' + ('passed' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
