#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Checks of the site's LUT converter (site/js/core.js), no npm dependencies:
//
//   node tests/site/validate.mjs [recipe.txt ...]
//
//  1. parsers and colour maths: .cube / .3dl / Hald round trips, log curves at 18 % grey,
//     a gamut matrix, CIEDE2000;
//  2. identity LUT -> identity recipe (no look keys, dE00 ~ 0);
//  3. a synthetic film-like look (sRGB in/out .cube, written to the output directory for manual
//     or browser tests) -> recipe: fit error and the recipe file rules;
//  4. the recipe file rules of the README ("Recipe file format"), checked independently of the
//     converter's own check: every shipped recipes/*.txt, the converter's output and any recipe
//     files given as arguments must pass.
// Output directory: $SITE_TEST_OUT, default <tmpdir>/gritty-site-test.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../..');
const core = new Function(fs.readFileSync(path.join(ROOT, 'site/js/core.js'), 'utf8') + '\nreturn lut2grCore();')();
const OUT_DIR = process.env.SITE_TEST_OUT || path.join(os.tmpdir(), 'gritty-site-test');
fs.mkdirSync(OUT_DIR, { recursive: true });

let failures = 0;
const ok = (cond, msg) => { console.log(`${cond ? 'PASS' : 'FAIL'}  ${msg}`); if (!cond) failures++; };
const f2 = d => `${d.mean.toFixed(2)} / ${d.p95.toFixed(2)}`;
const close = (a, b, tol) => a.length === b.length && a.every((v, i) => Math.abs(v - b[i]) <= tol);
const stock = core.approxStock();

// ---- recipe file rules (README, "Recipe file format") ----------------------------------------
const ISO = [100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000,
  6400, 8000, 10000, 12800, 16000, 20000, 25600];
const BASES = ['standard', 'vivid', 'setting1', 'setting2', 'bright', 'bw', 'bw_te', 'hicontrast_bw', 'cross_process',
  'positive_film', 'bleach_bypass', 'retro', 'miniature', 'high_key', 'slight', 'shift_crop', 'clarity', 'brilliance',
  'vibrant', 'hdr_tone', 'portrait'];
const LOOK_BASES = ['standard', 'vivid', 'setting1', 'setting2'];
const WB = ['auto', 'multi_auto', 'daylight', 'shade', 'cloudy', 'incandescent1', 'incandescent2', 'fl_daylight',
  'fl_neutral', 'fl_cool', 'fl_warm', 'cte', 'kelvin'];
const SHUTTER = ['auto', '1/2', '1/2.5', '1/3', '1/4', '1/5', '1/6', '1/8', '1/10', '1/13', '1/15', '1/20', '1/25', '1/30',
  '1/40', '1/50', '1/60', '1/80', '1/100', '1/125', '1/160', '1/200', '1/250'];
const LEVELS = ['off', 'weak', 'medium', 'strong'];

function recipeRules(text, fileName) {
  const errs = [];
  if (!/^[a-z0-9_-]{1,8}\.txt$/i.test(fileName)) errs.push(`file name ${fileName} is not 8.3`);
  if (/[^\x09\x0a\x0d\x20-\x7e]/.test(text)) errs.push('non-ASCII bytes');
  if (text.length > 4095) errs.push(`${text.length} bytes, over 4 KB`);
  if (/(^|[^\r])\n/.test(text) || /\r(?!\n)/.test(text)) errs.push('line endings are not CRLF');
  const kv = {};
  let base = null;
  for (const raw of text.split('\r\n')) {
    const line = raw.replace(/#.*/, '').trim();
    if (!line) continue;
    const eq = line.indexOf('=');
    if (eq < 1) { errs.push(`no key=value: "${line}"`); continue; }
    const k = line.slice(0, eq).trim(), v = line.slice(eq + 1).trim();
    const bad = what => errs.push(`${k}=${v}: ${what}`);
    const oneOf = list => { if (!list.includes(v)) bad(`not one of ${list.join(', ')}`); };
    const int = (lo, hi) => { if (!/^[+-]?\d+$/.test(v) || +v < lo || +v > hi) bad(`not an integer ${lo}..${hi}`); };
    const ints = (n, lo, hi) => {
      const p = v.split(',').map(s => s.trim());
      if (p.length !== n || p.some(s => !/^[+-]?\d+$/.test(s) || +s < lo || +s > hi)) bad(`needs ${n} integers ${lo}..${hi}`);
    };
    const iso = (lo, hi, extra = []) => { if (!extra.includes(v) && !(ISO.includes(+v) && +v >= lo && +v <= hi)) bad(`not ISO ${lo}..${hi}`); };
    kv[k] = v;
    if (k === 'name') { if (!v || v.length > 23) bad('needs 1..23 characters'); continue; }
    if (k === 'base') { oneOf(BASES); base = v; continue; }
    if (!base) { bad('before base'); continue; }
    if (['saturation', 'sharpness'].includes(k)) int(-4, 4);
    else if (k === 'contrast') { if (base === 'hicontrast_bw') oneOf(['max', '-1', '-2']); else int(-4, 4); }
    else if (k === 'vignetting') oneOf(LEVELS);
    else if (k === 'hue') oneOf(base === 'cross_process' ? ['basic', 'magenta', 'yellow'] : ['normal', 'warm', 'cool']);
    else if (k === 'toning') oneOf(base === 'bw_te' ? ['sepia', 'red', 'green', 'blue', 'purple'] : ['bw', 'sepia', 'color']);
    else if (k === 'clarity' || k === 'glossiness') int(1, 9);
    else if (k === 'wb') oneOf(WB);
    else if (k === 'wb_kelvin') int(2500, 10000);
    else if (k === 'wb_ab' || k === 'wb_gm') int(-8, 8);
    else if (k === 'exposure') { if (!/^[+-]?\d+(\.\d+)?$/.test(v)) bad('not an EV value'); }
    else if (k === 'dr') oneOf([...LEVELS, 'auto']);
    else if (k === 'nr') oneOf([...LEVELS, 'auto', 'custom']);
    else if (k === 'nr_weak') iso(100, 25600);
    else if (k === 'nr_medium' || k === 'nr_strong') iso(100, 25600, ['off']);
    else if (k === 'iso') iso(100, 25600, ['auto', 'auto_hi']);
    else if (k === 'auto_hi_max') iso(200, 25600);
    else if (k === 'auto_hi_min') iso(100, 12800);
    else if (k === 'auto_hi_shutter') oneOf(SHUTTER);
    else if (k === 'matrix' || k === 'post_matrix' || /^hue([1-9]|1[0-2])$/.test(k) || /^tone(_[rgb])?$/.test(k)) {
      if (!LOOK_BASES.includes(base)) bad(`look key with base=${base}`);
      if (k.startsWith('tone')) ints(core.KNOTS, 0, 255); else ints(9, -511, 511);
    } else bad('unknown key');
  }
  if (!('name' in kv)) errs.push('missing name');
  if (!('base' in kv)) errs.push('missing base');
  if (kv.wb === 'kelvin' && !('wb_kelvin' in kv)) errs.push('wb=kelvin without wb_kelvin');
  return errs;
}

function convert(lut, opts, name) {
  opts = { hue: true, post: true, ev: 0, ...opts };
  const r = core.fitLut(stock, core.makeTarget(lut, stock, opts), opts);
  const text = core.recipeText(r.look, r.useHue, {
    name, lutName: `${name}.cube`, lutSize: `${lut.size}^3`, ...opts, attribution: 'tests/site/validate.mjs output',
    de: { standard: r.de.standard, recipe: r.useHue ? r.de.full : r.de.plain }, stockLabel: stock.label,
  });
  return { ...r, text };
}

// ---- 1. parsers and colour maths -------------------------------------------------------------
{
  const id = core.identityCube(9);
  const cubeText = `TITLE "t"\nLUT_3D_SIZE 9\n` + Array.from({ length: 729 }, (_, i) => Array.from(id.data.subarray(3 * i, 3 * i + 3)).join(' ')).join('\n');
  const cube = core.parseCube(cubeText);
  ok(cube.size === 9 && close(cube.data, id.data, 0), '.cube parser: 9^3 identity, red fastest');
  const lines = ['0 128 256 384 512 640 768 896 1023'];
  for (let r = 0; r < 9; r++) for (let g = 0; g < 9; g++) for (let b = 0; b < 9; b++) lines.push(`${Math.round(r * 4095 / 8)} ${Math.round(g * 4095 / 8)} ${Math.round(b * 4095 / 8)}`);
  const t3dl = core.parse3dl(lines.join('\n'));
  ok(t3dl.size === 9 && close(t3dl.data, id.data, 1 / 4095), '.3dl parser: blue-fastest 12-bit mesh reordered');
  const L = 3, w = L ** 3, rgba = new Uint8ClampedArray(4 * w * w);
  for (let i = 0; i < w * w; i++) { const n = L * L; rgba[4 * i] = Math.round(255 * (i % n) / (n - 1)); rgba[4 * i + 1] = Math.round(255 * (Math.floor(i / n) % n) / (n - 1)); rgba[4 * i + 2] = Math.round(255 * Math.floor(i / n / n) / (n - 1)); rgba[4 * i + 3] = 255; }
  const hald = core.parseHald(rgba, w, w);
  ok(hald.size === 9 && close(hald.data, id.data, 1 / 255), 'Hald CLUT parser: level 3 = 9^3');
  const out = [0, 0, 0];
  core.lutApply(core.identityCube(5), [0.3, 0.71, 0.05], out);
  ok(close(out, [0.3, 0.71, 0.05], 1e-6), 'tetrahedral interpolation reproduces an identity LUT');
  // reference: colour-science 0.4.7 log_encoding_* at 0.18
  const ref = { vlog: 0.423311, slog3: 0.410557, logc3: 0.391007, flog: 0.459318, flog2: 0.391007 };
  ok(Object.entries(ref).every(([k, v]) => Math.abs(core.LOGS[k](0.18) - v) < 2e-6), 'log curves: 18 % grey matches colour-science (V-Log, S-Log3, LogC3, F-Log, F-Log2)');
  const bt2020 = Array.from(core.gamutMatrix('bt2020'));
  ok(close(bt2020, [0.6274, 0.3293, 0.0433, 0.0691, 0.9195, 0.0114, 0.0164, 0.0880, 0.8956], 1e-4), 'gamut matrix: Rec.709 -> BT.2020 (ITU-R BT.2087)');
  const de = core.deltaE00(Float64Array.from([50, 2.6772, -79.7751]), Float64Array.from([50, 0, -82.7485]))[0];
  ok(Math.abs(de - 2.0425) < 1e-4, `CIEDE2000: Sharma et al. test pair 1 = ${de.toFixed(4)}`);
  ok(Object.values(core.INPUTS).every(v => v.out in core.OUTPUTS), 'every input encoding names a known default output');
}

// ---- 2. identity LUT -> identity recipe ------------------------------------------------------
console.log('');
{
  const r = convert(core.identityCube(33), { input: 'srgb_display', output: 'srgb' }, 'Identity');
  const keys = r.text.split('\r\n').filter(l => /^(matrix|tone|post_matrix|hue)/.test(l));
  const id = core.identityLook();
  const dev = Math.max(...['a', 'p', 'h', 'tone'].map(k => Math.max(...r.look[k].map((v, i) => Math.abs(v - id[k][i])))));
  ok(dev <= 1 && r.de.plain.mean < 0.1, `identity LUT: max deviation from the identity recipe ${dev}, dE00 mean ${r.de.plain.mean.toFixed(3)}, look keys written: ${keys.length ? keys.map(l => l.split('=')[0]).join(',') : 'none'}`);
  ok(core.checkRecipe(r.text, 'identity.txt').length === 0, `identity recipe meets the converter's check (${r.text.length} B)`);
  const errs = recipeRules(r.text, 'identity.txt');
  ok(errs.length === 0, `identity recipe meets the README rules${errs.length ? `: ${errs.join('; ')}` : ''}`);
}

// ---- 3. synthetic film-like look -------------------------------------------------------------
console.log('');
const outputs = [];
{
  // sRGB in -> sRGB out: lifted blacks, soft shoulder, warm highlights / cool shadows, -15 % chroma.
  const N = 33, data = new Float32Array(3 * N ** 3);
  const curve = x => 0.035 + 0.94 * (3 * x * x - 2 * x * x * x) * 0.55 + 0.94 * x * 0.45;
  let i = 0;
  for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++, i += 3) {
    const c = [r, g, b].map(v => curve(v / (N - 1)));
    const y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    const tint = [0.04 * (y - 0.4), 0.01 * (y - 0.4), -0.05 * (y - 0.4)];
    for (let k = 0; k < 3; k++) data[i + k] = Math.min(1, Math.max(0, y + 0.85 * (c[k] - y) + tint[k]));
  }
  const lines = ['TITLE "Warm Fade"', `LUT_3D_SIZE ${N}`];
  for (let j = 0; j < data.length; j += 3) lines.push(`${data[j].toFixed(6)} ${data[j + 1].toFixed(6)} ${data[j + 2].toFixed(6)}`);
  const cubeFile = path.join(OUT_DIR, 'warm_fade.cube');
  fs.writeFileSync(cubeFile, lines.join('\n') + '\n');
  const lut = core.parseCube(fs.readFileSync(cubeFile, 'utf8'));
  const t0 = Date.now();
  const r = convert(lut, { input: 'srgb_display', output: core.INPUTS.srgb_display.out }, 'Warm Fade');
  const de = r.useHue ? r.de.full : r.de.plain;
  console.log(`      ${cubeFile}: fitted in ${((Date.now() - t0) / 1000).toFixed(1)} s, Standard ${f2(r.de.standard)}, recipe ${f2(de)} (hue sectors ${r.useHue ? 'used' : 'not used'})`);
  ok(de.mean < 2.0 && de.mean < r.de.standard.mean / 2, `synthetic look: dE00 mean/95th vs LUT ${f2(de)} (< 2 mean, under half of Standard's ${r.de.standard.mean.toFixed(2)})`);
  const file = path.join(OUT_DIR, 'warmfade.txt');
  fs.writeFileSync(file, r.text);
  outputs.push(file);
  ok(core.checkRecipe(r.text, 'warmfade.txt').length === 0, `synthetic look recipe meets the converter's check (${r.text.length} B)`);
  const back = core.parseRecipeLook(r.text).look;
  ok(['a', 'p', 'tone'].every(k => close(back[k], r.look[k], 0)) && (!r.useHue || close(back.h, r.look.h, 0)), 'synthetic look recipe: look keys read back exactly');
}

// ---- 4. recipe file rules: shipped recipes, converter output, arguments ----------------------
console.log('');
const shipped = fs.readdirSync(path.join(ROOT, 'recipes')).filter(f => f.endsWith('.txt')).map(f => path.join(ROOT, 'recipes', f));
for (const file of [...shipped, ...outputs, ...process.argv.slice(2)]) {
  const errs = recipeRules(fs.readFileSync(file, 'latin1'), path.basename(file));
  const shown = file.startsWith(ROOT + path.sep) ? path.relative(ROOT, file) : file;
  ok(errs.length === 0, `README rules: ${shown}${errs.length ? `: ${errs.join('; ')}` : ''}`);
}
// the rules must reject what the camera's parser rejects
const bad = [
  ['name=A\nbase=setting2\n', 'ok.txt', 'LF line endings'],
  ['name=A\r\nbase=bw\r\ntone=' + Array(33).fill(0).join(',') + '\r\n', 'ok.txt', 'look key on an effect base'],
  ['name=A\r\nbase=setting2\r\nmatrix=128,0,0,0,128,0,0,0,600\r\n', 'ok.txt', 'matrix value out of range'],
  ['name=A\r\nbase=setting2\r\ntone=0,8\r\n', 'ok.txt', 'short tone curve'],
  ['name=A\r\nsaturation=1\r\nbase=setting2\r\n', 'ok.txt', 'key before base'],
  ['name=A\r\nbase=setting2\r\n', 'toolongname.txt', 'long file name'],
  ['name=Abcdefghijklmnopqrstuvwx\r\nbase=setting2\r\n', 'ok.txt', '24-character name'],
];
ok(bad.every(([t, f]) => recipeRules(t, f).length > 0), `README rules reject: ${bad.map(b => b[2]).join(', ')}`);


// ---- 5. site brand and release asset selection -------------------------------------------------
console.log('');
{
  const rel = fs.readFileSync(path.join(ROOT, 'site/js/release.js'), 'utf8');
  const m = rel.match(/const ZIP = \/(.+)\/;/);
  const zip = m && new RegExp(m[1]);
  ok(!!zip && zip.test('gritty-v1.1.0.zip') && !zip.test('gritty-v1.1.0-debug.zip') && !zip.test('gritty-v1.1.0.zip.sha256'),
    'release.js picks gritty-v1.1.0.zip, not the -debug zip');
  ok(rel.includes("REPO = 'robbcocco/gritty'"), 'release.js asks the robbcocco/gritty repository');
  for (const page of ['index.html', 'convert.html']) {
    const html = fs.readFileSync(path.join(ROOT, 'site', page), 'utf8');
    const repos = [...html.matchAll(/github\.com\/robbcocco\/([\w.-]+)/g)].map((r) => r[1]);
    ok(/<title>[^<]*GR II TTY/.test(html) && repos.length > 0 && repos.every((r) => r === 'gritty'),
      `${page}: GR II TTY title, links point at robbcocco/gritty`);
  }
}
console.log(`\n${failures ? `${failures} FAILED` : 'all passed'}; files written to ${OUT_DIR}`);
process.exit(failures ? 1 : 0);
