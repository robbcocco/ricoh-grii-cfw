/* gr2_cfw site: latest release lookup. SPDX-License-Identifier: MIT
 * Asks the GitHub API for the latest release and points the download buttons at the release
 * zip (gr2_cfw-v<VERSION>.zip, not -debug). Without an answer (offline, rate limit) the buttons
 * keep linking to the releases page. */
(function () {
  'use strict';
  const REPO = 'robbcocco/ricoh-grii-cfw';
  const API = `https://api.github.com/repos/${REPO}/releases/latest`;
  const PAGE = `https://github.com/${REPO}/releases/latest`;
  const ZIP = /^gr2_cfw-v\d+(\.\d+)*\.zip$/;
  const $ = id => document.getElementById(id);

  const mb = n => `${(n / 1048576).toFixed(1)} MB`;

  function fallback(reason) {
    const s = $('release-status');
    if (!s) return;
    s.replaceChildren(
      `Couldn't look up the latest release (${reason}). `,
      Object.assign(document.createElement('a'), { href: PAGE, textContent: 'Open the releases page' }),
      ' and download gr2_cfw-v<VERSION>.zip.');
  }

  /* The SHA-256 of the zip: the API's asset digest, else the release's SHA256SUMS.txt (only
   * if the download host allows cross-origin reads). */
  async function sha256(rel, zip) {
    if (typeof zip.digest === 'string' && zip.digest.startsWith('sha256:')) return zip.digest.slice(7);
    const sums = rel.assets.find(a => /^SHA256SUMS(\.txt)?$/.test(a.name));
    if (!sums) return null;
    try {
      const r = await fetch(sums.browser_download_url);
      if (!r.ok) return null;
      const line = (await r.text()).split(/\r?\n/).find(l => l.trim().endsWith(` ${zip.name}`));
      return line ? line.trim().split(/\s+/)[0] : null;
    } catch { return null; }
  }

  async function lookup() {
    let rel;
    try {
      const r = await fetch(API, { headers: { Accept: 'application/vnd.github+json' } });
      if (!r.ok) throw new Error(r.status === 403 || r.status === 429 ? 'GitHub rate limit' : `HTTP ${r.status}`);
      rel = await r.json();
    } catch (err) {
      fallback(err.message || 'network error');
      return;
    }
    const zip = (rel.assets || []).find(a => ZIP.test(a.name));
    if (!zip) { fallback('no release zip in the latest release'); return; }
    for (const a of document.querySelectorAll('[data-release-link]')) {
      a.href = zip.browser_download_url;
      a.textContent = `Download ${rel.tag_name}`;
    }
    for (const p of document.querySelectorAll('[data-release-version]'))
      p.textContent = `${zip.name}, ${mb(zip.size)}, released ${(rel.published_at || '').slice(0, 10)}.`;
    if (!$('release-facts')) return;
    $('release-status').textContent = `Latest release: ${rel.name || rel.tag_name}.`;
    $('release-version').textContent = `${rel.tag_name}${rel.published_at ? ` (${rel.published_at.slice(0, 10)})` : ''}`;
    $('release-file').textContent = zip.name;
    $('release-size').textContent = `${mb(zip.size)} (${zip.size.toLocaleString('en')} bytes)`;
    const h = await sha256(rel, zip);
    $('release-sha').textContent = h || 'see SHA256SUMS.txt in the release';
    $('release-facts').hidden = false;
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', lookup);
  else lookup();
})();
