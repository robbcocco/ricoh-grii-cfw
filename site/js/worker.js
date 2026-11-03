/* GR II TTY LUT converter: fit worker. SPDX-License-Identifier: MIT
 * The page builds the worker from a Blob: lut2grCore's source plus this function, so it
 * also runs from file://. Messages:
 *   { type: 'fit', id, lut, opts }   -> 'progress'..., 'result' | 'error'
 */
function lut2grWorker(core) {
  'use strict';
  const plain = look => ({ a: Array.from(look.a), p: Array.from(look.p), h: Array.from(look.h), tone: Array.from(look.tone) });

  function fit(msg) {
    const stock = core.approxStock();
    const target = core.makeTarget(msg.lut, stock, msg.opts);
    const r = core.fitLut(stock, target, msg.opts, (stage, frac) => self.postMessage({ type: 'progress', id: msg.id, stage, frac }));
    const cc = Float64Array.from(core.CC24.flat());
    self.postMessage({
      type: 'result', id: msg.id, look: plain(r.look), useHue: r.useHue, de: r.de, cc: r.cc,
      warnings: r.warnings, stockLabel: stock.label,
      ccTarget: Array.from(target(cc)), ccRecipe: Array.from(core.render(stock, r.look, cc, true)),
    });
  }

  self.onmessage = e => {
    const msg = e.data;
    try {
      if (msg.type === 'fit') fit(msg);
    } catch (err) {
      self.postMessage({ type: 'error', id: msg.id, message: String(err && err.message || err) });
    }
  };
}
