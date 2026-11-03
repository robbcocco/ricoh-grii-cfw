/* gr2_cfw LUT converter core: LUT parsing, colour maths, the GR II look model and the recipe fit.
 * SPDX-License-Identifier: MIT
 *
 * One plain function so the same source runs in the page, in a Web Worker built from a Blob
 * (works from file:// too) and in Node (tests/site/validate.mjs evaluates this file).
 * Port of the maintainer's fitting tool (forward model and fit; the tool that fits the shipped
 * film recipes) with an approximate model of the camera's Standard rendering; no Ricoh tables. */
function lut2grCore() {
  'use strict';

  const VERSION = '1.0.0';
  const MID_GREY = 0.184;            // scene-linear mid grey of the model (spektrafilm's)
  const LUT_MID = 0.098;             // GR II tone-LUT input of 18 % grey, fraction of 1023
  const EXPO = LUT_MID / MID_GREY;
  const KNOTS = 33;
  const LIM = 511;                   // recipe integer range
  const HUE_SMOOTH = 3.0;            // weight of (H_k - H_k+1) in the hue fit
  const HUE_RIDGE = 0.3;             // weight of (H_k - I)
  const MAT_RIDGE = 0.3;             // weight of (A - I), (P - I)

  /* Approximate model of the camera's Standard rendering: measured model parameters
   * (finding 20), not Ricoh's tables. Tone curve: code(x) = min(255, s*((1-w)*L(x)^g + w*x)),
   * L(x) = ln(1 + a*x) / ln(1 + a), x = linear tone-LUT input 0..1 (rms 0.6 code, max 2.3). */
  const STD_CURVE = { a: 20.07195, g: 1.04594, w: -0.47064, s: 253.0908 };
  /* Hue-sector split inside each Cb/Cr quadrant (degrees from the quadrant start; the second
   * boundary is 90 - b). Measured model parameters, rounded (finding 20). */
  const SECTOR_SPLIT_DEG = [30, 30, 30, 35];
  /* BT.601 RGB'->YCbCr x256 (rows Y, Cb, Cr): ITU-R BT.601 coefficients, rounded. */
  const BT601_256 = [77, 150, 29, -43, -85, 128, 128, -107, -21];
  const JFIF_INV = [1.0, 0.0, 1.402, 1.0, -0.344136, -0.714136, 1.0, 1.772, 0.0];

  // ---- small linear algebra ------------------------------------------------------------

  function mul3(a, b) {
    const o = new Float64Array(9);
    for (let i = 0; i < 3; i++)
      for (let j = 0; j < 3; j++)
        o[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
    return o;
  }

  function inv3(m) {
    const [a, b, c, d, e, f, g, h, i] = m;
    const A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const det = a * A + b * B + c * C;
    return Float64Array.from([A, -(b * i - c * h), b * f - c * e,
      B, a * i - c * g, -(a * f - c * d),
      C, -(a * h - b * g), a * e - b * d]).map(v => v / det);
  }

  /* Solve the n x n system M x = y (M row-major, copied) by Gaussian elimination. */
  function solve(M, y, n) {
    const a = Float64Array.from(M), x = Float64Array.from(y);
    for (let c = 0; c < n; c++) {
      let p = c;
      for (let r = c + 1; r < n; r++) if (Math.abs(a[r * n + c]) > Math.abs(a[p * n + c])) p = r;
      if (p !== c) {
        for (let k = 0; k < n; k++) { const t = a[c * n + k]; a[c * n + k] = a[p * n + k]; a[p * n + k] = t; }
        const t = x[c]; x[c] = x[p]; x[p] = t;
      }
      const d = a[c * n + c] || 1e-300;
      for (let r = c + 1; r < n; r++) {
        const f = a[r * n + c] / d;
        if (!f) continue;
        for (let k = c; k < n; k++) a[r * n + k] -= f * a[c * n + k];
        x[r] -= f * x[c];
      }
    }
    for (let c = n - 1; c >= 0; c--) {
      let s = x[c];
      for (let k = c + 1; k < n; k++) s -= a[c * n + k] * x[k];
      x[c] = s / (a[c * n + c] || 1e-300);
    }
    return x;
  }

  /* Non-negative least squares in Gram form (Lawson-Hanson): min x'Gx - 2h'x, x >= 0. */
  function nnlsGram(G, h, n) {
    const x = new Float64Array(n), passive = new Uint8Array(n);
    const grad = () => { const w = new Float64Array(n); for (let i = 0; i < n; i++) { let s = h[i]; for (let j = 0; j < n; j++) s -= G[i * n + j] * x[j]; w[i] = s; } return w; };
    const scale = Math.max(1e-30, ...Array.from(h, Math.abs));
    const tol = 1e-12 * scale * n;
    const solveP = () => {
      const idx = []; for (let i = 0; i < n; i++) if (passive[i]) idx.push(i);
      const m = idx.length, A = new Float64Array(m * m), b = new Float64Array(m);
      idx.forEach((r, a) => { b[a] = h[r]; idx.forEach((c, k) => { A[a * m + k] = G[r * n + c]; }); });
      const s = solve(A, b, m), z = new Float64Array(n);
      idx.forEach((r, a) => { z[r] = s[a]; });
      return z;
    };
    for (let outer = 0; outer < 3 * n; outer++) {
      const w = grad();
      let t = -1, best = tol;
      for (let i = 0; i < n; i++) if (!passive[i] && w[i] > best) { best = w[i]; t = i; }
      if (t < 0) break;
      passive[t] = 1;
      for (let inner = 0; inner < 3 * n; inner++) {
        const z = solveP();
        let ok = true;
        for (let i = 0; i < n; i++) if (passive[i] && z[i] <= 0) ok = false;
        if (ok) { x.set(z); break; }
        let alpha = Infinity;
        for (let i = 0; i < n; i++)
          if (passive[i] && z[i] <= 0) alpha = Math.min(alpha, x[i] / (x[i] - z[i]));
        for (let i = 0; i < n; i++) {
          if (!passive[i]) continue;
          x[i] += alpha * (z[i] - x[i]);
          if (x[i] <= 1e-12 * scale) { x[i] = 0; passive[i] = 0; }
        }
      }
    }
    return x;
  }

  // ---- colour --------------------------------------------------------------------------

  const clamp01 = v => (v < 0 ? 0 : v > 1 ? 1 : v);
  const srgbDecode = v => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
  const srgbEncode = v => { v = clamp01(v); return v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055; };
  const bt709Oetf = v => { v = clamp01(v); return v < 0.018 ? 4.5 * v : 1.099 * Math.pow(v, 0.45) - 0.099; };

  const D65 = [0.3127, 0.329];
  /* RGB -> XYZ matrix from xy primaries and white. */
  function rgbToXyz(prim, white) {
    const col = ([x, y]) => [x / y, 1, (1 - x - y) / y];
    const P = Float64Array.from([0, 1, 2].flatMap(r => [0, 1, 2].map(c => col(prim[c])[r])));
    const w = col(white), Pi = inv3(P);
    const s = [0, 1, 2].map(r => Pi[3 * r] * w[0] + Pi[3 * r + 1] * w[1] + Pi[3 * r + 2] * w[2]);
    return P.map((v, i) => v * s[i % 3]);
  }
  const PRIMARIES = {
    srgb: [[0.64, 0.33], [0.30, 0.60], [0.15, 0.06]],
    vgamut: [[0.730, 0.280], [0.165, 0.840], [0.100, -0.030]],
    sgamut3cine: [[0.766, 0.275], [0.225, 0.800], [0.089, -0.087]],
    awg3: [[0.6840, 0.3130], [0.2210, 0.8480], [0.0861, -0.1020]],
    bt2020: [[0.708, 0.292], [0.170, 0.797], [0.131, 0.046]],
  };
  /* scene-linear sRGB/Rec.709 -> scene-linear in gamut g (all D65, no adaptation). */
  const gamutMatrix = g => mul3(inv3(rgbToXyz(PRIMARIES[g], D65)), rgbToXyz(PRIMARIES.srgb, D65));

  /* Log curves: scene-linear reflectance (0.18 = 18 % grey) -> code value 0..1, full range. */
  const LOGS = {
    vlog: x => (x < 0.01 ? 5.6 * x + 0.125 : 0.241514 * Math.log10(x + 0.00873) + 0.598206),
    slog3: x => (x >= 0.01125 ? (420 + Math.log10((x + 0.01) / 0.19) * 261.5) / 1023
      : (x * (171.2102946929 - 95) / 0.01125 + 95) / 1023),
    logc3: x => (x > 0.010591 ? 0.247190 * Math.log10(5.555556 * x + 0.052272) + 0.385537 : 5.367655 * x + 0.092809),
    flog: x => (x >= 0.00089 ? 0.344676 * Math.log10(0.555556 * x + 0.009468) + 0.790453 : 8.735631 * x + 0.092864),
    flog2: x => (x >= 0.000889 ? 0.245281 * Math.log10(5.555556 * x + 0.064829) + 0.384316 : 8.799461 * x + 0.092864),
  };

  /* LUT input encodings. kind 'display': the LUT is applied to the camera's Standard
   * rendering (a display-referred image); kind 'scene': to scene-linear light, encoded.
   * out: the output encoding such LUTs usually have (photo looks sRGB, video looks Rec.709). */
  const INPUTS = {
    srgb_display: { label: 'sRGB photo look (sRGB in, sRGB out)', kind: 'display', out: 'srgb', enc: v => v },
    rec709_display: { label: 'Rec.709 video look (gamma 2.4 in and out)', kind: 'display', out: 'rec709', enc: v => Math.pow(clamp01(srgbDecode(v)), 1 / 2.4) },
    srgb_scene: { label: 'sRGB-encoded scene light (clipped at 1.0), sRGB out', kind: 'scene', out: 'srgb', gamut: 'srgb', oetf: srgbEncode },
    rec709_scene: { label: 'Rec.709 camera OETF in, Rec.709 out', kind: 'scene', out: 'rec709', gamut: 'srgb', oetf: bt709Oetf },
    linear: { label: 'Linear scene light (Rec.709 primaries), sRGB out', kind: 'scene', out: 'srgb', gamut: 'srgb', oetf: v => v },
    vlog: { label: 'Panasonic V-Log / V-Gamut to Rec.709', kind: 'scene', out: 'rec709', gamut: 'vgamut', oetf: LOGS.vlog },
    slog3: { label: 'Sony S-Log3 / S-Gamut3.Cine to Rec.709', kind: 'scene', out: 'rec709', gamut: 'sgamut3cine', oetf: LOGS.slog3 },
    logc3: { label: 'ARRI LogC3 (EI 800) / AWG3 to Rec.709', kind: 'scene', out: 'rec709', gamut: 'awg3', oetf: LOGS.logc3 },
    flog: { label: 'Fujifilm F-Log / F-Gamut to Rec.709', kind: 'scene', out: 'rec709', gamut: 'bt2020', oetf: LOGS.flog },
    flog2: { label: 'Fujifilm F-Log2 / F-Gamut to Rec.709', kind: 'scene', out: 'rec709', gamut: 'bt2020', oetf: LOGS.flog2 },
  };
  /* LUT output encodings -> display-linear light (re-encoded as sRGB by the camera). */
  const OUTPUTS = {
    srgb: { label: 'sRGB display', dec: srgbDecode },
    rec709: { label: 'Rec.709 / BT.1886 display, gamma 2.4', dec: v => Math.pow(clamp01(v), 2.4) },
  };

  /* Display-linear sRGB (N x 3) -> CIELAB, D65 white = (1, 1, 1), as the fitting tool. */
  const XYZ_M = [0.4124, 0.3576, 0.1805, 0.2126, 0.7152, 0.0722, 0.0193, 0.1192, 0.9505];
  const XYZ_W = [XYZ_M[0] + XYZ_M[1] + XYZ_M[2], XYZ_M[3] + XYZ_M[4] + XYZ_M[5], XYZ_M[6] + XYZ_M[7] + XYZ_M[8]];
  const labF = t => (t > 216 / 24389 ? Math.cbrt(t) : t / (3 * (6 / 29) ** 2) + 4 / 29);
  function toLab(rgb) {
    const n = rgb.length / 3, out = new Float64Array(rgb.length);
    for (let i = 0; i < n; i++) {
      const r = clamp01(rgb[3 * i]), g = clamp01(rgb[3 * i + 1]), b = clamp01(rgb[3 * i + 2]);
      const fx = labF((XYZ_M[0] * r + XYZ_M[1] * g + XYZ_M[2] * b) / XYZ_W[0]);
      const fy = labF((XYZ_M[3] * r + XYZ_M[4] * g + XYZ_M[5] * b) / XYZ_W[1]);
      const fz = labF((XYZ_M[6] * r + XYZ_M[7] * g + XYZ_M[8] * b) / XYZ_W[2]);
      out[3 * i] = 116 * fy - 16; out[3 * i + 1] = 500 * (fx - fy); out[3 * i + 2] = 200 * (fy - fz);
    }
    return out;
  }

  /* CIEDE2000 between two Lab arrays (N x 3). */
  function deltaE00(l1, l2) {
    const n = l1.length / 3, out = new Float64Array(n), rad = Math.PI / 180, p25 = 25 ** 7;
    for (let i = 0; i < n; i++) {
      const L1 = l1[3 * i], a1 = l1[3 * i + 1], b1 = l1[3 * i + 2];
      const L2 = l2[3 * i], a2 = l2[3 * i + 1], b2 = l2[3 * i + 2];
      const C1 = Math.hypot(a1, b1), C2 = Math.hypot(a2, b2), Cb7 = ((C1 + C2) / 2) ** 7;
      const G = 0.5 * (1 - Math.sqrt(Cb7 / (Cb7 + p25)));
      const a1p = (1 + G) * a1, a2p = (1 + G) * a2;
      const C1p = Math.hypot(a1p, b1), C2p = Math.hypot(a2p, b2);
      const h = (b, a) => (b === 0 && a === 0 ? 0 : (Math.atan2(b, a) / rad + 360) % 360);
      const h1p = h(b1, a1p), h2p = h(b2, a2p);
      const dL = L2 - L1, dC = C2p - C1p;
      let dh = 0;
      if (C1p * C2p !== 0) { dh = h2p - h1p; if (dh > 180) dh -= 360; else if (dh < -180) dh += 360; }
      const dH = 2 * Math.sqrt(C1p * C2p) * Math.sin(dh * rad / 2);
      const Lm = (L1 + L2) / 2, Cm = (C1p + C2p) / 2;
      let hm = h1p + h2p;
      if (C1p * C2p !== 0) {
        if (Math.abs(h1p - h2p) <= 180) hm /= 2; else hm = h1p + h2p < 360 ? (hm + 360) / 2 : (hm - 360) / 2;
      }
      const T = 1 - 0.17 * Math.cos((hm - 30) * rad) + 0.24 * Math.cos(2 * hm * rad)
        + 0.32 * Math.cos((3 * hm + 6) * rad) - 0.20 * Math.cos((4 * hm - 63) * rad);
      const dTheta = 30 * Math.exp(-(((hm - 275) / 25) ** 2));
      const Cm7 = Cm ** 7, RC = 2 * Math.sqrt(Cm7 / (Cm7 + p25));
      const SL = 1 + 0.015 * (Lm - 50) ** 2 / Math.sqrt(20 + (Lm - 50) ** 2);
      const SC = 1 + 0.045 * Cm, SH = 1 + 0.015 * Cm * T;
      const RT = -Math.sin(2 * dTheta * rad) * RC;
      out[i] = Math.sqrt((dL / SL) ** 2 + (dC / SC) ** 2 + (dH / SH) ** 2 + RT * (dC / SC) * (dH / SH));
    }
    return out;
  }

  function summary(d) {
    const s = Array.from(d).sort((a, b) => a - b), n = s.length;
    const at = q => { const x = q * (n - 1), i = Math.floor(x), f = x - i; return i + 1 < n ? s[i] * (1 - f) + s[i + 1] * f : s[i]; };
    return { mean: s.reduce((a, b) => a + b, 0) / n, p95: at(0.95), max: s[n - 1] };
  }

  // ---- LUT files -------------------------------------------------------------------------

  /* LUT: { kind: '3d' | '1d', size, data: Float32Array (red fastest), min, max, pre1d? } */
  function parseCube(text) {
    let size3 = 0, size1 = 0, title = '';
    let min = [0, 0, 0], max = [1, 1, 1], range1 = null;
    const vals = [];
    for (const raw of text.split(/\r\n|\r|\n/)) {
      const line = raw.trim();
      if (!line || line[0] === '#') continue;
      const tok = line.split(/\s+/);
      const key = tok[0].toUpperCase();
      if (/^[-+.\d]/.test(tok[0])) {
        if (tok.length < 3) throw new Error(`.cube: bad data line "${line.slice(0, 40)}"`);
        vals.push(+tok[0], +tok[1], +tok[2]);
      } else if (key === 'TITLE') title = line.slice(5).trim().replace(/^"|"$/g, '');
      else if (key === 'LUT_3D_SIZE') size3 = parseInt(tok[1], 10);
      else if (key === 'LUT_1D_SIZE') size1 = parseInt(tok[1], 10);
      else if (key === 'DOMAIN_MIN') min = tok.slice(1, 4).map(Number);
      else if (key === 'DOMAIN_MAX') max = tok.slice(1, 4).map(Number);
      else if (key === 'LUT_3D_INPUT_RANGE') { min = [+tok[1], +tok[1], +tok[1]]; max = [+tok[2], +tok[2], +tok[2]]; }
      else if (key === 'LUT_1D_INPUT_RANGE') range1 = [+tok[1], +tok[2]];
      // other keywords (LUT_IN_VIDEO_RANGE, ...) are ignored
    }
    if (vals.some(v => !Number.isFinite(v))) throw new Error('.cube: non-numeric value');
    if (!size3 && !size1) throw new Error('.cube: no LUT_3D_SIZE or LUT_1D_SIZE');
    const n1 = size1 * 3, n3 = size3 ** 3 * 3;
    if (vals.length !== n1 + n3) throw new Error(`.cube: expected ${(n1 + n3) / 3} entries, found ${vals.length / 3}`);
    if (size3 && (size3 < 2 || size3 > 256)) throw new Error('.cube: LUT_3D_SIZE out of range');
    if (!size3) return { kind: '1d', size: size1, data: Float32Array.from(vals), min, max, title };
    const lut = { kind: '3d', size: size3, data: Float32Array.from(vals.slice(n1)), min, max, title };
    if (size1) lut.pre1d = { size: size1, data: Float32Array.from(vals.slice(0, n1)), min: range1 ? [range1[0], range1[0], range1[0]] : [0, 0, 0], max: range1 ? [range1[1], range1[1], range1[1]] : [1, 1, 1] };
    return lut;
  }

  /* Autodesk/Lustre .3dl: optional "Mesh in out" header, a shaper line, N^3 integer triplets
   * with blue varying fastest. */
  function parse3dl(text) {
    let shaper = null, outBits = 0;
    const vals = [];
    for (const raw of text.split(/\r\n|\r|\n/)) {
      const line = raw.replace(/#.*/, '').trim();
      if (!line) continue;
      const tok = line.split(/\s+/);
      if (/^mesh$/i.test(tok[0])) { outBits = parseInt(tok[2], 10) || 0; continue; }
      if (/^3dmesh$/i.test(tok[0]) || /^[a-z]/i.test(tok[0])) continue;
      const nums = tok.map(Number);
      if (nums.some(v => !Number.isFinite(v))) throw new Error(`.3dl: bad line "${line.slice(0, 40)}"`);
      if (!shaper && nums.length > 3) { shaper = nums; continue; }
      if (nums.length !== 3) throw new Error(`.3dl: bad line "${line.slice(0, 40)}"`);
      vals.push(...nums);
    }
    const count = vals.length / 3, n = Math.round(Math.cbrt(count));
    if (n < 2 || n ** 3 !== count) throw new Error(`.3dl: ${count} entries is not a cube`);
    if (shaper && shaper.length !== n) throw new Error('.3dl: shaper length does not match the mesh');
    let peak = 0;
    for (const v of vals) peak = Math.max(peak, v);
    const scale = outBits ? 2 ** outBits - 1 : peak <= 1.0 ? 1 : [1023, 4095, 16383, 65535].find(m => peak <= m) || peak;
    const data = new Float32Array(count * 3);
    for (let r = 0; r < n; r++)
      for (let g = 0; g < n; g++)
        for (let b = 0; b < n; b++) {
          const src = 3 * ((r * n + g) * n + b), dst = 3 * ((b * n + g) * n + r);
          for (let c = 0; c < 3; c++) data[dst + c] = vals[src + c] / scale;
        }
    return { kind: '3d', size: n, data, min: [0, 0, 0], max: [1, 1, 1], title: '' };
  }

  /* Hald CLUT image (RGBA bytes, w x h): level L has w = h = L^3, cube size L^2, red fastest. */
  function parseHald(rgba, w, h) {
    const L = Math.round(Math.cbrt(w));
    if (w !== h || L ** 3 !== w || L < 2) throw new Error(`Hald CLUT: ${w}x${h} is not a level^3 square`);
    const n = L * L, count = n ** 3, data = new Float32Array(count * 3);
    for (let i = 0; i < count; i++)
      for (let c = 0; c < 3; c++) data[3 * i + c] = rgba[4 * i + c] / 255;
    return { kind: '3d', size: n, data, min: [0, 0, 0], max: [1, 1, 1], title: '' };
  }

  function identityCube(n) {
    const data = new Float32Array(n ** 3 * 3);
    for (let b = 0, i = 0; b < n; b++)
      for (let g = 0; g < n; g++)
        for (let r = 0; r < n; r++, i += 3) { data[i] = r / (n - 1); data[i + 1] = g / (n - 1); data[i + 2] = b / (n - 1); }
    return { kind: '3d', size: n, data, min: [0, 0, 0], max: [1, 1, 1], title: 'identity' };
  }

  function apply1d(t, v, c) {
    const n = t.size, x = clamp01((v - t.min[c]) / (t.max[c] - t.min[c])) * (n - 1);
    const i = Math.min(Math.floor(x), n - 2), f = x - i;
    return t.data[3 * i + c] * (1 - f) + t.data[3 * (i + 1) + c] * f;
  }

  /* Apply a LUT to one RGB triplet (input in the LUT's domain), tetrahedral for 3D. */
  function lutApply(lut, rgb, out) {
    let r = rgb[0], g = rgb[1], b = rgb[2];
    if (lut.pre1d) { r = apply1d(lut.pre1d, r, 0); g = apply1d(lut.pre1d, g, 1); b = apply1d(lut.pre1d, b, 2); }
    if (lut.kind === '1d') { out[0] = apply1d(lut, r, 0); out[1] = apply1d(lut, g, 1); out[2] = apply1d(lut, b, 2); return; }
    const n = lut.size, d = lut.data, m = n - 1;
    const x = clamp01((r - lut.min[0]) / (lut.max[0] - lut.min[0])) * m;
    const y = clamp01((g - lut.min[1]) / (lut.max[1] - lut.min[1])) * m;
    const z = clamp01((b - lut.min[2]) / (lut.max[2] - lut.min[2])) * m;
    const x0 = Math.min(Math.floor(x), m - 1), y0 = Math.min(Math.floor(y), m - 1), z0 = Math.min(Math.floor(z), m - 1);
    const fx = x - x0, fy = y - y0, fz = z - z0;
    const sx = 3, sy = 3 * n, sz = 3 * n * n, o = x0 * sx + y0 * sy + z0 * sz;
    const c000 = o, c111 = o + sx + sy + sz;
    let c1, c2, w0, w1, w2, w3;
    if (fx >= fy) {
      if (fy >= fz) { c1 = o + sx; c2 = o + sx + sy; w0 = 1 - fx; w1 = fx - fy; w2 = fy - fz; w3 = fz; }
      else if (fx >= fz) { c1 = o + sx; c2 = o + sx + sz; w0 = 1 - fx; w1 = fx - fz; w2 = fz - fy; w3 = fy; }
      else { c1 = o + sz; c2 = o + sx + sz; w0 = 1 - fz; w1 = fz - fx; w2 = fx - fy; w3 = fy; }
    } else {
      if (fz >= fy) { c1 = o + sz; c2 = o + sy + sz; w0 = 1 - fz; w1 = fz - fy; w2 = fy - fx; w3 = fx; }
      else if (fz >= fx) { c1 = o + sy; c2 = o + sy + sz; w0 = 1 - fy; w1 = fy - fz; w2 = fz - fx; w3 = fx; }
      else { c1 = o + sy; c2 = o + sx + sy; w0 = 1 - fy; w1 = fy - fx; w2 = fx - fz; w3 = fz; }
    }
    for (let c = 0; c < 3; c++) out[c] = w0 * d[c000 + c] + w1 * d[c1 + c] + w2 * d[c2 + c] + w3 * d[c111 + c];
  }

  // ---- the camera's Standard stage data ----------------------------------------------------

  /* The approximate Standard model (no Ricoh data). Hue tables = identity, camera matrix
   * unknown (the composite clamp of matrix x camera matrix is not modelled). */
  function approxStock() {
    const { a, g, w, s } = STD_CURVE, la = Math.log1p(a);
    const curve = new Int32Array(1024);
    for (let i = 0; i < 1024; i++) {
      const x = i / 1023, v = s * ((1 - w) * Math.pow(Math.log1p(a * x) / la, g) + w * x);
      curve[i] = Math.max(0, Math.min(255, Math.round(v)));
    }
    const tstd = new Int32Array(108);
    for (let k = 0; k < 12; k++) tstd[9 * k] = tstd[9 * k + 4] = tstd[9 * k + 8] = 1024;
    return {
      label: 'approximate Standard model (no Ricoh data)',
      set0: [curve, curve, curve], tstd, c4: Int32Array.from(BT601_256),
      cbcr: Int32Array.from(BT601_256.slice(3)),
      geom: SECTOR_SPLIT_DEG.map(b => [1, Math.tan(b * Math.PI / 180)]),
    };
  }

  // ---- GR II forward model (src/look.c, finding 20) ------------------------------------------

  const OFF = [1, 2, 3, 5, 6, 7];                      // off-diagonal indices of a 3x3
  const eye128 = () => Float64Array.from([128, 0, 0, 0, 128, 0, 0, 0, 128]);
  const identityTone = () => Float64Array.from({ length: 3 * KNOTS }, (_, i) => Math.min((i % KNOTS) * 8, 255));

  function identityLook() {
    const h = new Float64Array(108);
    for (let k = 0; k < 12; k++) h.set(eye128(), 9 * k);
    return { a: eye128(), p: eye128(), h, tone: identityTone() };
  }

  const cloneLook = l => ({ a: Float64Array.from(l.a), p: Float64Array.from(l.p), h: Float64Array.from(l.h), tone: Float64Array.from(l.tone) });

  /* Round to the recipe integers; rows sum to 128 (absorbed on the diagonal); monotone tone. */
  function quantise(look) {
    const warn = [];
    const mat = (m, name) => {
      const q = Float64Array.from(m, v => Math.max(-LIM, Math.min(LIM, Math.round(v))));
      for (let r = 0; r < 3; r++) {
        const d = 128 - (q[3 * r] + q[3 * r + 1] + q[3 * r + 2] - q[4 * r]);
        if (Math.abs(d) > LIM) warn.push(`${name}: row ${r + 1} clamped to the recipe range`);
        q[4 * r] = Math.max(-LIM, Math.min(LIM, d));
      }
      return q;
    };
    const h = new Float64Array(108);
    for (let k = 0; k < 12; k++) h.set(mat(look.h.subarray(9 * k, 9 * k + 9), `hue${k + 1}`), 9 * k);
    const tone = new Float64Array(3 * KNOTS);
    for (let c = 0; c < 3; c++) {
      let run = 0;
      for (let i = 0; i < KNOTS; i++) {
        run = Math.max(run, Math.max(0, Math.min(255, Math.round(look.tone[c * KNOTS + i]))));
        tone[c * KNOTS + i] = run;
      }
    }
    const q = { a: mat(look.a, 'matrix'), p: mat(look.p, 'post_matrix'), h, tone };
    q.warnings = warn;
    return q;
  }

  const shift7 = acc => (acc + 64) >> 7;               // look.c mul3 rounding (arithmetic shift)
  const clampI = (v, lim) => (v < -lim ? -lim : v > lim ? lim : v);

  function toneAt(k, off, x) {                          // look.c look_tone_at
    const i = x >> 3, f = x & 7;
    if (i >= KNOTS - 1) return k[off + KNOTS - 1];
    return k[off + i] + (((k[off + i + 1] - k[off + i]) * f) >> 3);
  }

  /* Effective float stages: A (9), hue tables (108), tone LUT (3 x 1024), display matrix Q (9). */
  function stages(stock, look, exact) {
    const jf = Float64Array.from(JFIF_INV), lut = new Float64Array(3072);
    if (!look) {
      for (let c = 0; c < 3; c++) for (let i = 0; i < 1024; i++) lut[1024 * c + i] = stock.set0[c][i];
      return { a: Float64Array.from([1, 0, 0, 0, 1, 0, 0, 0, 1]), t: Float64Array.from(stock.tstd, v => v / 1024),
        lut, q: mul3(jf, Float64Array.from(stock.c4, v => v / 256)) };
    }
    let a;
    const t = new Float64Array(108), c = new Float64Array(9);
    if (exact) {
      a = Float64Array.from(look.a, v => v / 128);
      for (let k = 0; k < 12; k++)
        for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
          let acc = 0;
          for (let m = 0; m < 3; m++) acc += look.h[9 * k + 3 * i + m] * stock.tstd[9 * k + 3 * m + j];
          t[9 * k + 3 * i + j] = clampI(shift7(acc), 8191) / 1024;
        }
      for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
        let acc = 0;
        for (let m = 0; m < 3; m++) acc += stock.c4[3 * i + m] * look.p[3 * m + j];
        c[3 * i + j] = clampI(shift7(acc), 255) / 256;
      }
      for (let ch = 0; ch < 3; ch++)
        for (let i = 0; i < 1024; i++) lut[1024 * ch + i] = toneAt(look.tone, ch * KNOTS, stock.set0[ch][i]);
    } else {
      a = Float64Array.from(look.a, v => v / 128);
      for (let k = 0; k < 12; k++)
        for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
          let acc = 0;
          for (let m = 0; m < 3; m++) acc += look.h[9 * k + 3 * i + m] * stock.tstd[9 * k + 3 * m + j];
          t[9 * k + 3 * i + j] = acc / (128 * 1024);
        }
      for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
        let acc = 0;
        for (let m = 0; m < 3; m++) acc += stock.c4[3 * i + m] * look.p[3 * m + j];
        c[3 * i + j] = acc / (128 * 256);
      }
      for (let ch = 0; ch < 3; ch++)
        for (let i = 0; i < 1024; i++) {
          const x = stock.set0[ch][i] / 8, j = Math.min(Math.floor(x), KNOTS - 2), f = x - j;
          lut[1024 * ch + i] = look.tone[ch * KNOTS + j] * (1 - f) + look.tone[ch * KNOTS + j + 1] * f;
        }
    }
    return { a, t, lut, q: mul3(jf, c) };
  }

  function sector(stock, r, g, b) {
    const cb = stock.cbcr[0] * r + stock.cbcr[1] * g + stock.cbcr[2] * b;
    const cr = stock.cbcr[3] * r + stock.cbcr[4] * g + stock.cbcr[5] * b;
    const q = cr >= 0 ? (cb >= 0 ? 0 : 1) : (cb < 0 ? 2 : 3);
    const u = [cb, cr, -cb, -cr][q], v = [cr, -cb, -cr, cb][q];
    const [ga, gb] = stock.geom[q];
    return 3 * q + (v * ga < u * gb ? 0 : v * gb < u * ga ? 1 : 2);
  }

  /* Scene-linear (N x 3, Standard-linear, mid grey 0.184) -> display-linear sRGB (N x 3). */
  function render(stock, look, scene, exact = true, st = null) {
    const { a, t, lut, q } = st || stages(stock, look, exact);
    const n = scene.length / 3, out = new Float64Array(scene.length);
    for (let i = 0; i < n; i++) {
      const s0 = scene[3 * i] * EXPO, s1 = scene[3 * i + 1] * EXPO, s2 = scene[3 * i + 2] * EXPO;
      const y0 = a[0] * s0 + a[1] * s1 + a[2] * s2;
      const y1 = a[3] * s0 + a[4] * s1 + a[5] * s2;
      const y2 = a[6] * s0 + a[7] * s1 + a[8] * s2;
      const k = 9 * sector(stock, y0, y1, y2);
      const code = [0, 0, 0];
      for (let c = 0; c < 3; c++) {
        const z = t[k + 3 * c] * y0 + t[k + 3 * c + 1] * y1 + t[k + 3 * c + 2] * y2;
        const x = clamp01(z) * 1023, j = Math.min(Math.floor(x), 1022), f = x - j;
        code[c] = lut[1024 * c + j] * (1 - f) + lut[1024 * c + j + 1] * f;
      }
      for (let c = 0; c < 3; c++) {
        const d = q[3 * c] * code[0] + q[3 * c + 1] * code[1] + q[3 * c + 2] * code[2];
        out[3 * i + c] = srgbDecode(Math.max(0, Math.min(255, d)) / 255);
      }
    }
    return out;
  }

  // ---- the LUT as a target --------------------------------------------------------------------

  /* opts: { input, output, ev, legalIn, legalOut }. Returns scene (N x 3) -> display-linear. */
  function makeTarget(lut, stock, opts) {
    const inp = INPUTS[opts.input], outp = OUTPUTS[opts.output];
    if (!inp || !outp) throw new Error('unknown encoding');
    const gain = Math.pow(2, opts.ev || 0);
    const gm = inp.kind === 'scene' && inp.gamut !== 'srgb' ? gamutMatrix(inp.gamut) : null;
    const std = inp.kind === 'display' ? stages(stock, null, true) : null;
    const lo = lut.min, hi = lut.max;
    return function target(scene) {
      const n = scene.length / 3, out = new Float64Array(scene.length), y = [0, 0, 0], z = [0, 0, 0];
      const pre = std ? render(stock, null, scene.map(v => v * gain), true, std) : null;
      for (let i = 0; i < n; i++) {
        let r = scene[3 * i] * gain, g = scene[3 * i + 1] * gain, b = scene[3 * i + 2] * gain;
        if (pre) {
          y[0] = inp.enc(srgbEncode(pre[3 * i])); y[1] = inp.enc(srgbEncode(pre[3 * i + 1])); y[2] = inp.enc(srgbEncode(pre[3 * i + 2]));
        } else {
          if (gm) { const r2 = gm[0] * r + gm[1] * g + gm[2] * b, g2 = gm[3] * r + gm[4] * g + gm[5] * b; b = gm[6] * r + gm[7] * g + gm[8] * b; r = r2; g = g2; }
          y[0] = inp.oetf(r); y[1] = inp.oetf(g); y[2] = inp.oetf(b);
        }
        for (let c = 0; c < 3; c++) {
          let v = y[c];
          if (opts.legalIn) v = (v * 876 + 64) / 1023;
          y[c] = lo[c] + clamp01((v - lo[c]) / (hi[c] - lo[c])) * (hi[c] - lo[c]);
        }
        lutApply(lut, y, z);
        for (let c = 0; c < 3; c++) {
          let v = z[c];
          if (opts.legalOut) v = (v * 1023 - 64) / 876;
          out[3 * i + c] = outp.dec(clamp01(v));
        }
      }
      return out;
    };
  }

  // ---- samples ------------------------------------------------------------------------------

  /* ColorChecker 24 ("After November 2014", colour-science), adapted to D65 (Bradford),
   * scene-linear sRGB/Rec.709 primaries. */
  const CC24 = [[0.173683, 0.078793, 0.052856], [0.559784, 0.277447, 0.210892], [0.104201, 0.188915, 0.328930],
    [0.105744, 0.150754, 0.050837], [0.227439, 0.212408, 0.426446], [0.115293, 0.507441, 0.411129],
    [0.745903, 0.202143, 0.029868], [0.058742, 0.101182, 0.387748], [0.560130, 0.080153, 0.114586],
    [0.109167, 0.042000, 0.138137], [0.332750, 0.497386, 0.042596], [0.771037, 0.357918, 0.020449],
    [0.020956, 0.047534, 0.284129], [0.046062, 0.292048, 0.061479], [0.446171, 0.036406, 0.040551],
    [0.841684, 0.574408, 0.004662], [0.522342, 0.077817, 0.289313], [-0.043918, 0.233662, 0.377190],
    [0.879529, 0.885028, 0.834247], [0.584493, 0.592168, 0.584478], [0.357694, 0.367077, 0.365266],
    [0.190094, 0.190866, 0.189814], [0.085929, 0.088735, 0.089804], [0.031354, 0.031496, 0.032323]];

  function mulberry32(seed) {
    return function () {
      seed |= 0; seed = seed + 0x6D2B79F5 | 0;
      let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
      t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
      return ((t ^ t >>> 14) >>> 0) / 4294967296;
    };
  }

  function fitSamples() {
    const v = [];
    for (const ev of [-1, 0, 1]) for (const p of CC24) v.push(...p.map(x => x * 2 ** ev));
    const lv = [0, 0.2, 0.4, 0.6, 0.8, 1];
    for (const e of [0.25, 1.0, 1.75])
      for (const r of lv) for (const g of lv) for (const b of lv)
        if (r + g + b > 0) v.push(srgbDecode(r) * e, srgbDecode(g) * e, srgbDecode(b) * e);
    return Float64Array.from(v);
  }

  function evalSamples() {
    const v = [], rnd = mulberry32(20261103);
    for (const ev of [-2, -1, 0, 1]) for (const p of CC24) v.push(...p.map(x => x * 2 ** ev));
    for (let i = 0; i < 600; i++) {
      const c = [rnd(), rnd(), rnd()].map(srgbDecode), e = 2 ** (-2 + 2.8 * rnd());
      v.push(c[0] * e, c[1] * e, c[2] * e);
    }
    return Float64Array.from(v);
  }

  // ---- fit ------------------------------------------------------------------------------------

  /* Neutral ramp: tone-LUT input 0..1023 in 4093 steps; target display codes. */
  function makeRamp(target) {
    const n = 4093, idx = new Float64Array(n), scene = new Float64Array(3 * n);
    for (let i = 0; i < n; i++) { idx[i] = 1023 * i / (n - 1); scene[3 * i] = scene[3 * i + 1] = scene[3 * i + 2] = idx[i] / 1023 / EXPO; }
    const lin = target(scene);
    return { idx, code: lin.map(v => 255 * srgbEncode(v)) };
  }

  /* Monotone least squares of the 33 knots per channel on the neutral ramp, inverted through
   * the Standard curve and the display matrix (as the fitting tool). */
  function fitTone(stock, ramp, look, exact) {
    const qi = inv3(stages(stock, look, exact).q);
    const n = ramp.idx.length, tone = new Float64Array(3 * KNOTS);
    for (let ch = 0; ch < 3; ch++) {
      const s0 = stock.set0[ch], std = new Float64Array(n);
      for (let i = 0; i < n; i++) {
        const x = ramp.idx[i], j = Math.min(Math.floor(x), 1022), f = x - j;
        std[i] = s0[j] * (1 - f) + s0[j + 1] * f;
      }
      const G = new Float64Array(KNOTS * KNOTS), h = new Float64Array(KNOTS), v = new Float64Array(KNOTS);
      for (let i = 0; i < n; i++) {
        const grad = i === 0 ? std[1] - std[0] : i === n - 1 ? std[n - 1] - std[n - 2] : (std[i + 1] - std[i - 1]) / 2;
        const w = grad + 0.02;
        const c = ramp.code;
        let y = qi[3 * ch] * c[3 * i] + qi[3 * ch + 1] * c[3 * i + 1] + qi[3 * ch + 2] * c[3 * i + 2];
        y = Math.max(0, Math.min(255, y));
        const j = Math.min(Math.floor(std[i] / 8), KNOTS - 2), f = std[i] / 8 - j;
        v.fill(0);
        for (let k = 0; k <= j; k++) v[k] = 1;
        v[j + 1] = f;
        const last = j + 1;
        for (let r = 0; r <= last; r++) {
          if (!v[r]) continue;
          h[r] += w * v[r] * y;
          for (let c2 = 0; c2 <= last; c2++) G[r * KNOTS + c2] += w * v[r] * v[c2];
        }
      }
      for (let k = 0; k < KNOTS; k++) G[k * KNOTS + k] += 1e-9;
      const x = nnlsGram(G, h, KNOTS);
      let run = 0;
      for (let k = 0; k < KNOTS; k++) { run += x[k]; tone[ch * KNOTS + k] = Math.min(run, 255); }
    }
    return tone;
  }

  function pack(look, hue) {
    const x = [];
    for (const i of OFF) x.push(look.a[i] / 128);
    for (const i of OFF) x.push(look.p[i] / 128);
    if (hue) for (let k = 0; k < 12; k++) for (const i of OFF) x.push(look.h[9 * k + i] / 128);
    return Float64Array.from(x);
  }

  function unpack(x, look, hue) {
    const mat = (o, dst, at) => {
      for (let j = 0; j < 6; j++) dst[at + OFF[j]] = x[o + j] * 128;
      for (let r = 0; r < 3; r++) dst[at + 4 * r] = 128 - (dst[at + 3 * r] + dst[at + 3 * r + 1] + dst[at + 3 * r + 2] - dst[at + 4 * r]);
    };
    const l = cloneLook(look);
    mat(0, l.a, 0); mat(6, l.p, 0);
    if (hue) for (let k = 0; k < 12; k++) mat(12 + 6 * k, l.h, 9 * k);
    return l;
  }

  /* Levenberg-Marquardt with a forward-difference Jacobian over the free parameters. */
  function levmar(fun, x0, free, maxIter, onIter) {
    let x = Float64Array.from(x0), r = fun(x), cost = r.reduce((s, v) => s + v * v, 0);
    let lambda = 1e-3;
    const m = free.length;
    for (let it = 0; it < maxIter; it++) {
      const J = free.map(p => {
        const hstep = 1e-4 * Math.max(1, Math.abs(x[p])), xs = Float64Array.from(x);
        xs[p] += hstep;
        const rs = fun(xs);
        return rs.map((v, i) => (v - r[i]) / hstep);
      });
      const JtJ = new Float64Array(m * m), Jtr = new Float64Array(m);
      for (let a = 0; a < m; a++) {
        const ja = J[a];
        let s = 0;
        for (let i = 0; i < r.length; i++) s += ja[i] * r[i];
        Jtr[a] = s;
        for (let b = a; b < m; b++) {
          const jb = J[b];
          let t = 0;
          for (let i = 0; i < r.length; i++) t += ja[i] * jb[i];
          JtJ[a * m + b] = JtJ[b * m + a] = t;
        }
      }
      let improved = false, step = 0;
      while (lambda < 1e12) {
        const M = Float64Array.from(JtJ);
        for (let a = 0; a < m; a++) M[a * m + a] += lambda * Math.max(JtJ[a * m + a], 1e-9);
        const dx = solve(M, Jtr.map(v => -v), m);
        const xn = Float64Array.from(x);
        free.forEach((p, a) => { xn[p] += dx[a]; });
        const rn = fun(xn), cn = rn.reduce((s, v) => s + v * v, 0);
        if (cn < cost) {
          step = Math.sqrt(dx.reduce((s, v) => s + v * v, 0));
          const rel = (cost - cn) / cost;
          x = xn; r = rn; cost = cn; lambda = Math.max(lambda / 3, 1e-9); improved = true;
          if (rel < 1e-7) step = 0;
          break;
        }
        lambda *= 4;
      }
      if (onIter) onIter(it, cost);
      if (!improved || step < 1e-7) break;
    }
    return x;
  }

  function fitColour(stock, fitLab, scene, look, hue, post, onIter) {
    const nres = fitLab.length;
    const fun = x => {
      const lk = unpack(x, look, hue);
      const lab = toLab(render(stock, lk, scene, false));
      const extra = 12 + (hue ? 144 : 0);
      const r = new Float64Array(nres + extra);
      for (let i = 0; i < nres; i++) r[i] = lab[i] - fitLab[i];
      for (let i = 0; i < 12; i++) r[nres + i] = MAT_RIDGE * 100 * x[i];
      if (hue)
        for (let k = 0; k < 12; k++)
          for (let j = 0; j < 6; j++) {
            const v = x[12 + 6 * k + j], nb = x[12 + 6 * ((k + 1) % 12) + j];
            r[nres + 12 + 6 * k + j] = HUE_SMOOTH * 100 * (v - nb);
            r[nres + 84 + 6 * k + j] = HUE_RIDGE * 100 * v;
          }
      return r;
    };
    const free = [];
    for (let i = 0; i < 6; i++) free.push(i);
    if (post) for (let i = 6; i < 12; i++) free.push(i);
    if (hue) for (let i = 12; i < 84; i++) free.push(i);
    const x = levmar(fun, pack(look, hue), free, hue ? 40 : 60, onIter);
    return unpack(x, look, hue);
  }

  function stats(stock, look, scene, refLab) {
    return summary(deltaE00(toLab(render(stock, look, scene, true)), refLab));
  }

  /* The whole fit (as the fitting tool): returns the integer recipe look and its errors.
   * opts: { hue: bool, post: bool }; progress(stage, fraction). */
  function fitLut(stock, target, opts, progress = () => {}) {
    const ramp = makeRamp(target);
    const fitScene = fitSamples(), fitLab = toLab(target(fitScene));
    const evScene = evalSamples(), evRef = toLab(target(evScene));
    const post = opts.post !== false;
    let look = identityLook();
    const steps = 4 + (opts.hue ? 4 : 0);
    let done = 0;
    const tick = name => progress(name, ++done / steps);
    look.tone = fitTone(stock, ramp, look, false);
    for (let i = 0; i < 2; i++) {
      look = fitColour(stock, fitLab, fitScene, look, false, post);
      look.tone = fitTone(stock, ramp, look, false);
      tick('matrix and tone curves');
    }
    let plain = quantise(look);
    plain.tone = fitTone(stock, ramp, plain, true);
    plain = Object.assign(quantise(plain), { warnings: plain.warnings });
    tick('quantise');
    let full = null;
    if (opts.hue) {
      for (let i = 0; i < 2; i++) {
        look = fitColour(stock, fitLab, fitScene, look, true, post);
        look.tone = fitTone(stock, ramp, look, false);
        tick('hue sectors');
      }
      full = quantise(look);
      full.tone = fitTone(stock, ramp, full, true);
      full = Object.assign(quantise(full), { warnings: full.warnings });
      tick('quantise');
    }
    const de = { standard: stats(stock, null, evScene, evRef), plain: stats(stock, plain, evScene, evRef) };
    if (full) de.full = stats(stock, full, evScene, evRef);
    const useHue = !!full && de.full.mean < de.plain.mean;
    const result = useHue ? full : plain;
    const ccRef = evRef.subarray(2 * 72, 3 * 72);   // ColorChecker at 0 EV
    const cc = Array.from(deltaE00(toLab(render(stock, result, evScene.subarray(2 * 72, 3 * 72), true)), ccRef));
    const warnings = [...(result.warnings || [])];
    if (result.a.some(v => Math.abs(v) > 300))
      warnings.push('large matrix coefficients: the camera clamps matrix x its own colour matrix (not modelled)');
    for (let ch = 0; ch < 3; ch++) {
      const t = result.tone.subarray(ch * KNOTS, ch * KNOTS + KNOTS);
      if (t[KNOTS - 1] - t[0] < 64) warnings.push(`tone_${'rgb'[ch]}: very flat curve (range ${t[KNOTS - 1] - t[0]} codes)`);
    }
    return { look: result, useHue, de, cc, warnings };
  }

  // ---- recipe file ----------------------------------------------------------------------------

  const ascii = s => String(s).normalize('NFKD').replace(/[\u0300-\u036f]/g, '').replace(/[^\x20-\x7e]/g, '?');

  function recipeName(s) {
    return ascii(s).replace(/#/g, '').replace(/\s+/g, ' ').trim().slice(0, 23).trim() || 'LUT look';
  }

  function fileStem(s) {
    return (ascii(s).toLowerCase().replace(/[^a-z0-9_-]+/g, '').slice(0, 8)) || 'lutlook';
  }

  const isEye = m => m[0] === 128 && m[4] === 128 && m[8] === 128 && OFF.every(i => m[i] === 0);
  const ints = a => Array.from(a, v => String(Math.round(v))).join(',');
  const fmt = (v, d = 1) => v.toFixed(d);

  /* meta: { name, lutName, lutSize, input, output, ev, legalIn, legalOut, attribution, de, stockLabel } */
  function recipeText(look, useHue, meta) {
    const lines = [
      `# gr2_cfw recipe: ${recipeName(meta.name)} (Ricoh GR II, firmware 3.00)`,
      '# Keys: ricoh-grii-cfw README, section "Recipe file format". ASCII, CRLF.',
      `# Converted from the LUT "${ascii(meta.lutName || 'unnamed').replace(/"/g, "'").slice(0, 80)}"${meta.lutSize ? ` (${meta.lutSize})` : ''} by the gr2_cfw LUT converter ${VERSION}.`,
      `# LUT input: ${INPUTS[meta.input].label}${meta.legalIn ? ', legal range' : ''}; exposure offset ${meta.ev >= 0 ? '+' : ''}${fmt(meta.ev || 0, 2)} EV.`,
      `# LUT output: ${OUTPUTS[meta.output].label}${meta.legalOut ? ', legal range' : ''}.`,
      `# GR II model: ${meta.stockLabel}. Fit CIEDE2000 mean/95th: Standard ${fmt(meta.de.standard.mean)}/${fmt(meta.de.standard.p95)}, this recipe ${fmt(meta.de.recipe.mean)}/${fmt(meta.de.recipe.p95)}.`,
      '# Derived from the LUT above: the LUT\'s own licence and attribution apply to this file.',
    ];
    const attr = String(meta.attribution || '').split(/\r\n|\r|\n/).map(l => ascii(l).trimEnd()).filter((l, i, a) => l || (i > 0 && i < a.length - 1));
    if (attr.length) {
      lines.push('#');
      for (const l of attr) lines.push(l ? `# ${l}` : '#');
    }
    lines.push('', `name=${recipeName(meta.name)}`, 'base=setting2', 'saturation=0', 'contrast=0', 'sharpness=0', 'vignetting=off');
    const head = lines.length;
    lines.push('# look relative to Standard (README, section Recipes)');
    if (!isEye(look.a)) lines.push('# colour matrix, linear RGB, x128, rows R G B', `matrix=${ints(look.a)}`);
    const tr = look.tone.subarray(0, KNOTS), tg = look.tone.subarray(KNOTS, 2 * KNOTS), tb = look.tone.subarray(2 * KNOTS);
    const idTone = identityTone().subarray(0, KNOTS);
    const same = (x, y) => x.every((v, i) => v === y[i]);
    if (!(same(tr, idTone) && same(tg, idTone) && same(tb, idTone))) {
      lines.push('# tone curves: output code at Standard code 0, 8, 16, ..., 256');
      if (same(tr, tg) && same(tg, tb)) lines.push(`tone=${ints(tr)}`);
      else lines.push(`tone_r=${ints(tr)}`, `tone_g=${ints(tg)}`, `tone_b=${ints(tb)}`);
    }
    if (!isEye(look.p)) lines.push('# post matrix, display RGB, x128, rows R G B', `post_matrix=${ints(look.p)}`);
    if (useHue) {
      const hs = [];
      for (let k = 0; k < 12; k++) {
        const m = look.h.subarray(9 * k, 9 * k + 9);
        if (!isEye(m)) hs.push(`hue${k + 1}=${ints(m)}`);
      }
      if (hs.length) lines.push('# hue sectors (linear RGB, x128; 1-3 magenta, 4-6 red/orange/yellow, 7-9 green, 10-12 cyan/blue)', ...hs);
    }
    if (lines.length === head + 1) lines[head] = '# look: identical to Standard (no look keys)';
    return lines.join('\r\n') + '\r\n';
  }

  /* The recipe parser's limits (src/recipes.c). Returns a list of problems. */
  function checkRecipe(text, fileName) {
    const errs = [];
    if (/[^\x09\x0a\x0d\x20-\x7e]/.test(text)) errs.push('non-ASCII characters');
    if (text.length >= 4095) errs.push(`${text.length} bytes: over the 4 KB recipe limit`);
    if (/[^\r]\n/.test(text)) errs.push('line endings are not CRLF');
    if (fileName !== undefined && !/^[a-z0-9_-]{1,8}\.txt$/.test(fileName)) errs.push('file name is not 8.3');
    let name = null, base = false;
    for (const raw of text.split('\r\n')) {
      const line = raw.replace(/#.*/, '').trim();
      if (!line) continue;
      const eq = line.indexOf('=');
      if (eq < 0) { errs.push(`no "=" in "${line}"`); continue; }
      const k = line.slice(0, eq).trim(), v = line.slice(eq + 1).trim();
      if (k === 'name') { name = v; if (!v || v.length > 23) errs.push('name must be 1..23 characters'); continue; }
      if (k === 'base') { base = v === 'setting2'; continue; }
      if (!base) errs.push(`${k} before base`);
      const list = (n, lo, hi) => {
        const p = v.split(',');
        if (p.length !== n || p.some(s => !/^[+-]?\d{1,5}$/.test(s.trim()) || +s < lo || +s > hi)) errs.push(`${k}: needs ${n} integers ${lo}..${hi}`);
      };
      if (k === 'matrix' || k === 'post_matrix' || /^hue([1-9]|1[0-2])$/.test(k)) list(9, -511, 511);
      else if (/^tone(_[rgb])?$/.test(k)) list(KNOTS, 0, 255);
      else if (['saturation', 'contrast', 'sharpness'].includes(k)) { if (!/^[+-]?[0-4]$/.test(v)) errs.push(`${k} out of range`); }
      else if (k === 'vignetting') { if (!['off', 'weak', 'medium', 'strong'].includes(v)) errs.push('vignetting value'); }
      else errs.push(`unexpected key ${k}`);
    }
    if (!name) errs.push('missing name');
    if (!base) errs.push('missing base=setting2');
    return errs;
  }

  /* Parse the look keys of a recipe text back into a look (for comparisons). */
  function parseRecipeLook(text) {
    const l = identityLook();
    let hue = false;
    for (const raw of text.split(/\r?\n/)) {
      const line = raw.replace(/#.*/, '').trim(), eq = line.indexOf('=');
      if (eq < 0) continue;
      const k = line.slice(0, eq).trim(), v = line.slice(eq + 1).split(',').map(Number);
      if (k === 'matrix') l.a = Float64Array.from(v);
      else if (k === 'post_matrix') l.p = Float64Array.from(v);
      else if (k === 'tone') for (let c = 0; c < 3; c++) l.tone.set(v, c * KNOTS);
      else if (/^tone_[rgb]$/.test(k)) l.tone.set(v, 'rgb'.indexOf(k[5]) * KNOTS);
      else if (/^hue\d+$/.test(k)) { l.h.set(v, 9 * (parseInt(k.slice(3), 10) - 1)); hue = true; }
    }
    return { look: l, hue };
  }

  return {
    VERSION, MID_GREY, EXPO, KNOTS, INPUTS, OUTPUTS, CC24,
    srgbDecode, srgbEncode, toLab, deltaE00, summary, gamutMatrix, LOGS,
    parseCube, parse3dl, parseHald, identityCube, lutApply,
    approxStock, identityLook, quantise, stages, render,
    makeTarget, fitSamples, evalSamples, fitLut, stats,
    recipeName, fileStem, recipeText, checkRecipe, parseRecipeLook,
  };
}
if (typeof module === 'object' && module.exports) module.exports = lut2grCore;
