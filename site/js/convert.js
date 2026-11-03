/* GR II TTY LUT converter: page logic. SPDX-License-Identifier: MIT */
(function () {
  'use strict';
  const core = lut2grCore();
  const $ = id => document.getElementById(id);

  // The fit runs in a worker made from the already loaded sources (works from file:// too).
  const workerSrc = `${lut2grCore}\n(${lut2grWorker})(lut2grCore());`;
  const worker = new Worker(URL.createObjectURL(new Blob([workerSrc], { type: 'text/javascript' })));

  let lut = null, lutName = '', lutSize = '', fit = null, jobId = 0, nameEdited = false;

  for (const [k, v] of Object.entries(core.INPUTS)) $('enc-in').add(new Option(v.label, k));

  const status = (msg, cls = 'muted') => { $('status').textContent = msg; $('status').className = `status ${cls}`; };
  const stem = name => name.replace(/\.[^.]+$/, '').replace(/[_-]+/g, ' ').trim();
  const recipeName = () => core.recipeName($('r-name').value || stem(lutName));
  const fileName = () => core.fileStem($('r-name').value || stem(lutName)) + '.txt';

  // ---- LUT input ---------------------------------------------------------------------------
  function guessInput(name) {
    const n = name.toLowerCase();
    const rules = [[/v-?log/, 'vlog'], [/s-?log-?3/, 'slog3'], [/log-?c/, 'logc3'], [/f-?log-?2/, 'flog2'], [/f-?log/, 'flog']];
    for (const [re, k] of rules) if (re.test(n)) return k;
    return 'srgb_display';
  }

  function setLut(l, name, desc) {
    lut = l; lutName = name;
    lutSize = l.kind === '1d' ? `1D ${l.size}` : `${l.size}^3 ${desc}`;
    $('drop-file').textContent = `${name} \u00b7 ${l.kind === '1d' ? `1D, ${l.size} points` : `${l.size}\u00b3`}`;
    $('enc-in').value = guessInput(`${name} ${l.title || ''}`);
    if (!nameEdited) $('r-name').value = core.recipeName(l.title || stem(name));
    showFile();
    convert();
  }

  async function loadLutFile(file) {
    try {
      const ext = file.name.split('.').pop().toLowerCase();
      if (ext === 'png') {
        const bmp = await createImageBitmap(file, { colorSpaceConversion: 'none', premultiplyAlpha: 'none' });
        const cv = new OffscreenCanvas(bmp.width, bmp.height), cx = cv.getContext('2d');
        cx.drawImage(bmp, 0, 0);
        setLut(core.parseHald(cx.getImageData(0, 0, bmp.width, bmp.height).data, bmp.width, bmp.height), file.name, 'Hald CLUT');
      } else {
        const text = await file.text();
        setLut(ext === '3dl' ? core.parse3dl(text) : core.parseCube(text), file.name, ext === '3dl' ? '.3dl' : '.cube');
      }
    } catch (err) {
      status(`Could not read ${file.name}: ${err.message}`, 'bad');
    }
  }

  $('lut-file').addEventListener('change', e => { if (e.target.files[0]) loadLutFile(e.target.files[0]); });
  const drop = $('drop');
  drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('over'));
  drop.addEventListener('drop', e => {
    e.preventDefault(); drop.classList.remove('over');
    if (e.dataTransfer.files[0]) loadLutFile(e.dataTransfer.files[0]);
  });
  $('enc-in').addEventListener('change', () => { if (lut) convert(); });

  // ---- convert -----------------------------------------------------------------------------
  function convert() {
    const input = $('enc-in').value;
    const opts = { input, output: core.INPUTS[input].out, ev: 0, legalIn: false, legalOut: false, hue: true, post: true };
    jobId++;
    fit = { pending: true, opts, lutName, lutSize };
    $('download').disabled = true;
    $('result').hidden = true;
    progress(0);
    status(`Fitting ${lutName}\u2026`);
    worker.postMessage({ type: 'fit', id: jobId, lut, opts });
  }

  function progress(frac) {
    $('progress').hidden = frac === null;
    if (frac === null) return;
    $('bar').firstElementChild.style.width = `${Math.round(100 * frac)}%`;
    $('bar').setAttribute('aria-valuenow', String(Math.round(100 * frac)));
  }

  worker.onmessage = e => {
    const m = e.data;
    if (m.id !== jobId) return;
    if (m.type === 'progress') { progress(m.frac); status(`Fitting ${lutName}: ${m.stage}\u2026`); }
    else if (m.type === 'error') { progress(null); status(`Could not convert ${lutName}: ${m.message}`, 'bad'); }
    else if (m.type === 'result') onResult(m);
  };

  function verdict(d) {
    if (d < 1) return 'Practically identical to the LUT';
    if (d < 2) return 'Close match';
    if (d < 3.5) return 'Good approximation';
    if (d < 5) return 'Rough approximation';
    return 'Loose approximation: the LUT does things the camera cannot follow';
  }

  function onResult(m) {
    const look = { a: Float64Array.from(m.look.a), p: Float64Array.from(m.look.p), h: Float64Array.from(m.look.h), tone: Float64Array.from(m.look.tone) };
    const de = m.useHue ? m.de.full : m.de.plain;
    Object.assign(fit, { pending: false, look, useHue: m.useHue, de: m.de, stockLabel: m.stockLabel });
    progress(null);
    status(`Converted ${lutName} (${lutSize}).`, 'ok');
    $('result-title').textContent = verdict(de.mean);
    $('badge').replaceChildren(`\u0394E ${de.mean.toFixed(1)}`, Object.assign(document.createElement('small'), { textContent: 'mean' }));
    $('result-detail').textContent =
      `Average colour difference to the LUT: \u0394E ${de.mean.toFixed(1)} (95 % of colours within ${de.p95.toFixed(1)}). ` +
      `The camera without the recipe: \u0394E ${m.de.standard.mean.toFixed(1)}.`;
    const pos = de.mean <= 2 ? 25 * de.mean : Math.min(100, 50 + 12.5 * (de.mean - 2));
    $('meter-dot').style.left = `${pos}%`;
    const css = (lin, i) => `rgb(${[0, 1, 2].map(c => Math.round(255 * core.srgbEncode(lin[3 * i + c]))).join(',')})`;
    $('swatches').replaceChildren(...Array.from({ length: 24 }, (_, i) => {
      const d = document.createElement('div');
      for (const src of [m.ccTarget, m.ccRecipe]) d.appendChild(Object.assign(document.createElement('span'), { style: `background:${css(src, i)}` }));
      return d;
    }));
    $('warnings').replaceChildren(...m.warnings.map(w => Object.assign(document.createElement('li'), { textContent: w })));
    $('result').hidden = false;
    showFile();
  }

  // ---- recipe ------------------------------------------------------------------------------
  function recipeText() {
    const o = fit.opts;
    return core.recipeText(fit.look, fit.useHue, {
      name: recipeName(), lutName: fit.lutName, lutSize: fit.lutSize, input: o.input, output: o.output,
      ev: o.ev, legalIn: o.legalIn, legalOut: o.legalOut, attribution: $('r-attr').value,
      de: { standard: fit.de.standard, recipe: fit.useHue ? fit.de.full : fit.de.plain }, stockLabel: fit.stockLabel,
    });
  }

  function showFile() {
    $('r-file').textContent = lut ? `Shown in the camera (up to 23 characters). File: ${fileName()}` : 'Shown in the camera (up to 23 characters).';
    if (!fit || fit.pending) return;
    const text = recipeText(), errs = core.checkRecipe(text, fileName());
    $('check').textContent = errs.length ? `Problem: ${errs.join('; ')}` : `${fileName()}, ${text.length} bytes: ready for the card.`;
    $('check').className = `small ${errs.length ? 'bad' : 'ok'}`;
    $('download').disabled = errs.length > 0;
  }

  $('r-name').addEventListener('input', () => { nameEdited = true; showFile(); });
  $('r-attr').addEventListener('input', showFile);

  $('download').addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob([recipeText()], { type: 'text/plain' }));
    const a = Object.assign(document.createElement('a'), { href: url, download: fileName() });
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
})();
