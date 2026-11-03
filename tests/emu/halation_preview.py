#!/usr/bin/env python3
"""Halation preview: the CFW's own pass (src/halation.c, the built stage 2) in the emulator on real photos.

    python3 tests/emu/halation_preview.py [--variant debug] [--recipe recipes/c800t.txt] [--set KEY=VALUE ...]
                                          [--night] [--no-budget] [--profile] [--dump] [--out DIR] [IMAGE ...]

Each image (a camera JPEG/PNG of 4928x3264, 4352x3264 or 3264x3264; anything else is centre-cropped and scaled to
3:2) is taken as the developed still in the recipe's look: its RGB' codes go through the colour mode's YCbCr record
into the stock YCC buffer layout (pitches, margins), the still path's effect step runs through the patched call in a
`ProcessStill` task, and the result is decoded again. --night adds a synthetic night street (linear light through
the look's tone curves). --set overrides or adds recipe keys (e.g. --set halation_boost=4,0.3,2). --no-budget lifts
the cost budget, --profile prints instructions per stage (estimate, source, blur, add-back) and per function,
--dump saves the decoded frames. Writes per image DIR/<name>_before.png and _after.png (1600 px wide), _crops.png
(100 % crops where the halo is strongest: before | after) and prints status, instructions and the estimate (and
the camera time it models). Needs the firmware inputs of the emulator tests (make fw).
"""
import argparse
import pathlib
import re
import sys
import types

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run  # noqa: E402

FRAMES = [(4928, 3264), (4352, 3264), (3264, 3264)]
PREVIEW = 'zzhalo.txt'


def recipe(path, sets):
    """The recipe file with `sets` (KEY=VALUE) replacing or adding keys; named so it sorts last."""
    lines = pathlib.Path(path).read_bytes().decode('ascii').split('\r\n')
    over = dict(s.split('=', 1) for s in sets)
    out, seen = [], set()
    for line in lines:
        m = re.match(r'(\w+)=', line)
        if m and m.group(1) == 'name':
            line = 'name=~zz Preview'
        elif m and m.group(1) in over:
            line = f'{m.group(1)}={over[m.group(1)]}'
            seen.add(m.group(1))
        out.append(line)
    while out and not out[-1]:
        out.pop()
    out += [f'{k}={v}' for k, v in over.items() if k not in seen]
    return ('\r\n'.join(out) + '\r\n').encode('ascii')


def load(path):
    import numpy as np
    from PIL import Image
    im = Image.open(path).convert('RGB')
    if im.size not in FRAMES:
        w, h = im.size
        cw, ch = (w, w * 2 // 3) if w * 2 <= h * 3 else (h * 3 // 2, h)
        im = im.crop(((w - cw) // 2, (h - ch) // 2, (w + cw) // 2, (h + ch) // 2)).resize((4928, 3264))
    return np.asarray(im, float)


def night_street(tone, w=4928, h=3264, seed=3):
    """A synthetic night street in linear light (1023 = the tone curve's top), coded through the look's curves:
    dark sky and facades, lit windows (mostly below white, a few clipped), clipped sodium and LED street lamps with
    their light pools, a neon sign, headlights and tail lights, a lit shop front below white: point lights and
    bright edges against dark ground, exposed as a camera meters a night street."""
    import numpy as np
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    E = np.zeros((h, w, 3), np.float32)
    E[:] = (np.array([3, 4, 9], np.float32) * (1 + 0.6 * (1 - yy / h))[..., None])          # sky
    horizon = int(h * 0.62)
    for x0 in range(0, w, 620):                                                               # facades
        top = int(h * rng.uniform(0.12, 0.35))
        x1 = min(w, x0 + int(rng.uniform(450, 600)))
        E[top:horizon, x0:x1] = np.array([10, 9, 8]) * rng.uniform(0.6, 1.4)
        for wy in range(top + 60, horizon - 80, 150):                                         # windows
            for wx in range(x0 + 40, x1 - 80, 110):
                if rng.random() < 0.35:
                    col = rng.choice([np.array([900, 620, 300]), np.array([700, 700, 650]), np.array([400, 330, 250])])
                    E[wy:wy + 70, wx:wx + 50] = col * (rng.uniform(1.2, 1.6) if rng.random() < 0.1 else
                                                       rng.uniform(0.25, 0.8))
    E[horizon:] = np.array([7, 7, 8])                                                         # street
    for i, x in enumerate(np.linspace(300, w - 300, 8)):                                      # lamp posts
        ly = int(h * 0.30) + 25 * (i % 2)
        E[ly:horizon + 300, int(x) - 6:int(x) + 6] = np.array([6, 6, 7])
        col = np.array([5000, 2600, 700]) if i % 2 else np.array([4000, 4000, 4300])          # sodium / LED
        r2 = (xx - x) ** 2 + (yy - ly) ** 2
        E += col * np.exp(-r2 / (2 * 14 ** 2))[..., None] * 1.0
        E[r2 < 18 ** 2] = np.maximum(E[r2 < 18 ** 2], col)
        pool = np.exp(-((xx - x) ** 2 / (2 * 260 ** 2) + (yy - (horizon + 260)) ** 2 / (2 * 110 ** 2)))
        E += (col / col.max() * 90)[None, None, :] * pool[..., None] * (yy > horizon)[..., None]
    sx, sy = int(w * 0.18), int(h * 0.40)                                                    # neon sign
    E[sy:sy + 14, sx:sx + 520] = [2500, 120, 300]
    E[sy + 60:sy + 74, sx:sx + 520] = [150, 600, 2800]
    E[int(h * 0.50):horizon, int(w * 0.58):int(w * 0.80)] = [520, 470, 400]                 # shop front
    for cx in (int(w * 0.30), int(w * 0.62)):                                                 # a car
        cy = int(h * 0.74)
        E[cy - 120:cy + 40, cx - 330:cx + 330] = [5, 5, 6]
        for dx in (-250, 250):
            r2 = (xx - (cx + dx)) ** 2 + (yy - cy) ** 2
            col = np.array([6000, 6000, 5500]) if cx < w // 2 else np.array([3000, 60, 40])   # head / tail lights
            E[r2 < 30 ** 2] = col
    E *= rng.normal(1, 0.04, E.shape[:2])[..., None].astype(np.float32)
    idx = np.clip(np.rint(E), 0, 1023).astype(int)
    return np.stack([tone[c][idx[..., c]] for c in range(3)], -1).astype(float)


def save(ctx_out, name, before, after):
    import numpy as np
    from PIL import Image
    b8, a8 = (np.clip(np.rint(x), 0, 255).astype(np.uint8) for x in (before, after))
    for tag, img in (('before', b8), ('after', a8)):
        im = Image.fromarray(img)
        im.resize((1600, round(1600 * img.shape[0] / img.shape[1])), Image.LANCZOS).save(ctx_out / f'{name}_{tag}.png')
    diff = np.abs(a8.astype(int) - b8.astype(int)).sum(-1).astype(float)
    tiles, used = [], []
    blk = 64
    hb, wb = diff.shape[0] // blk, diff.shape[1] // blk
    score = diff[:hb * blk, :wb * blk].reshape(hb, blk, wb, blk).sum((1, 3))
    for _ in range(3):                                                    # three crops 480 px apart at least
        i, j = np.unravel_index(np.argmax(score), score.shape)
        if score[i, j] <= 0:
            break
        cy, cx = i * blk + blk // 2, j * blk + blk // 2
        score[max(0, i - 8):i + 9, max(0, j - 8):j + 9] = 0
        y0 = int(np.clip(cy - 200, 0, b8.shape[0] - 400))
        x0 = int(np.clip(cx - 200, 0, b8.shape[1] - 400))
        used.append((x0, y0))
        tiles.append(np.concatenate([b8[y0:y0 + 400, x0:x0 + 400], np.full((400, 8, 3), 255, np.uint8),
                                     a8[y0:y0 + 400, x0:x0 + 400]], 1))
    if tiles:
        Image.fromarray(np.concatenate([np.concatenate([t, np.full((8, t.shape[1], 3), 255, np.uint8)], 0)
                                        for t in tiles], 0)).save(ctx_out / f'{name}_crops.png')
    return int((diff > 0).sum()), used


def main():
    import numpy as np
    from unicorn import UC_HOOK_BLOCK
    from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_LR
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('images', nargs='*')
    ap.add_argument('--variant', default='debug', choices=('debug', 'release'))
    ap.add_argument('--recipe', default=str(run.CFW / 'recipes' / 'c800t.txt'))
    ap.add_argument('--set', action='append', default=[], metavar='KEY=VALUE')
    ap.add_argument('--night', action='store_true', help='add the synthetic night street')
    ap.add_argument('--out', default='/tmp/halation/preview')
    ap.add_argument('--no-budget', action='store_true', help='lift the cost budget (to see what it would skip)')
    ap.add_argument('--dump', action='store_true', help='also save the decoded frames (before, after) as .npz')
    ap.add_argument('--profile', action='store_true', help='print the instructions per function of the pass')
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    inputs, missing = run.load_inputs()
    if missing:
        sys.exit(f'missing emulator inputs: {missing} (make fw)')
    ctx = types.SimpleNamespace(variant=a.variant, inputs=inputs, renderer=None, check=run.Checks(), machines=[])
    data = recipe(a.recipe, a.set)
    s = run.Session(ctx, files={'H:/cfw/recipes/' + PREVIEW: data}, props={0x2D: 4})
    m = s.m
    names = run.by_name([run.recipe_info(p.name, p.read_bytes()) for p in (run.CFW / 'recipes').glob('*.txt')] +
                        [run.recipe_info(PREVIEW, data)])
    s.request(run.MENU_RECIPES)
    s.keys(*[run.KEY_RIGHT] * ([r['file'] for r in names].index(PREVIEW) - s.recipe_sel()))
    fr = run.FxFrames(m)
    if a.no_budget:
        m.w32(m.syms['g_hx_budget'], 0xF0000000)
    tone, C = run.still_model(m)
    jobs = [(pathlib.Path(p).stem, load(p)) for p in a.images]
    if a.night:
        jobs.append(('night_street', night_street(tone)))
    lo, hi = m.main_base, m.main_end
    stage_of = {m.syms[f] & ~1: s_ for f, s_ in (('over_budget', 'estimate'), ('source_row', 'source'),
                                                 ('blur_row', 'blur'), ('addback_rows', 'add-back'))}
    for name, rgb in jobs:
        h, w = rgb.shape[:2]
        planes = run.ycc_encode(rgb, C)
        n, per, stages, stack = [0], {}, {}, []

        def count(uc, addr, size, _, n=n, per=per, stages=stages, stack=stack):
            k = size >> (1 if uc.reg_read(UC_ARM_REG_CPSR) & 0x20 else 2)
            n[0] += k
            if a.profile:                              # a stage runs from its entry until its caller resumes
                if stack and addr == stack[-1][1]:
                    stack.pop()
                if addr in stage_of:
                    stack.append((stage_of[addr], uc.reg_read(UC_ARM_REG_LR) & ~1))
                st = stack[-1][0] if stack else 'other'
                stages[st] = stages.get(st, 0) + k
                per[addr] = per.get(addr, 0) + k
        hk = m.uc.hook_add(UC_HOOK_BLOCK, count, begin=lo, end=hi)
        m.uc.ctl_remove_cache(lo, hi)
        fr.shoot(ctx, fr.ctx(w, h), (w, h, planes), check_stack=False)
        m.uc.hook_del(hk)
        if a.profile:
            fn = {}
            for addr, k in per.items():
                f = m.syms.name_at(addr).split('+')[0]
                fn[f] = fn.get(f, 0) + k
            print(f'{name}: stages ' + ', '.join(f'{s_} {stages.get(s_, 0) / 1e6:.1f} M' for s_ in
                                                 ('estimate', 'source', 'blur', 'add-back', 'other')))
            print(f'{name}: functions ' + ', '.join(f'{f} {k / 1e6:.1f} M' for f, k in
                                                    sorted(fn.items(), key=lambda x: -x[1])[:8]))
        got = fr.get(w, h)
        rec = [m.u32(m.syms['g_rec'] + o) for o in (run.REC_STATUS, run.REC_CELLS, run.REC_CELLS + 8, run.REC_WORDS,
                                                    run.REC_DEC, run.REC_DEC + 4, run.REC_DEC + 8, run.REC_DEC + 12)]
        before, after = run.ycc_decode(*planes, C), run.ycc_decode(*got, C)
        changed, crops = save(out, name, before, after)
        if a.dump:
            np.savez_compressed(out / f'{name}.npz', before=before.astype(np.float32), after=after.astype(np.float32))
        est, cam = run.est_ms(rec[4], rec[5], rec[7]), n[0] * run.CAM_NS / 1e6 + run.CAM_MS0
        print(f'{name}: {w}x{h} status {rec[0]}, {n[0] / 1e6:.1f} M instructions = {cam:.0f} ms on the camera, '
              f'estimate {rec[4]} decoded / {rec[6]} source / {rec[5]} halo / {rec[7]} boosted cells = {est:.0f} ms, '
              f'{rec[1]} source cells, {rec[2]} groups, {rec[3]} words; {changed} pixels changed, least channel change '
              f'{(after - before).min():.2f} codes; crops at (x, y) {crops}')
    print(f'wrote {out}')


if __name__ == '__main__':
    main()
