// ── Pinned-images viewer: the list's elements and its small helpers ─────────
// Shared by pins.js (the render pass) and pinRow.js (one row).
import { fetchAsDataUrl } from '../lib/stencil.js';
import { createFilterTransition } from '../lib/motion.js';

export const siteSel = document.getElementById('pin-site');
export const pinListEl = document.getElementById('pin-list');
export const pinEmptyEl = document.getElementById('pin-empty');
export const pinClearBtn = document.getElementById('pin-clear');
export const pinSearchEl = document.getElementById('pin-search');
export const pinSearchModeEl = document.getElementById('pin-search-mode');
// Host label for a site origin (e.g. https://example.com → example.com).
export const hostLabel = (origin) => { try { return new URL(origin).host; } catch { return origin || '(unknown site)'; } };

// Both lists on this page are rebuilt wholesale on every filter change, so the shared
// transition (lib/motion.js) fades out the rows the filters dropped, where they stood,
// and ramps the arriving ones in. A pin is keyed by the pair that identifies it in
// storage; a connection by its URL (the data-url the animations already find it by).
export const pinKey = (pin) => `${pin.site}\n${pin.source}`;
export const pinTransition = createFilterTransition({ list: pinListEl });

// The particle layer is appended to the row's parent — move it to <body> so the list
// rebuild underneath can't take the dust with it.
export const liftDust = (el) => {
  const parent = el.parentElement;
  if (!parent) return;
  for (const host of parent.querySelectorAll('.disintegrate-host')) document.body.appendChild(host);
};

// Lazily recover a thumbnail a plain <img> couldn't load (hotlink-protected http(s));
// videos and unfetchable sources keep the neutral placeholder.
export const recoverThumb = (img, source, kind, resource = '') => {
  img.addEventListener('error', async () => {
    if (img.dataset.recovered || kind === 'video' || !/^https?:/i.test(source)) { img.style.visibility = 'hidden'; return; }
    img.dataset.recovered = '1';
    // `resource` = the page the pin was made on (recorded at pin time) — same-host carve-out.
    try { img.src = await fetchAsDataUrl(source, { pageUrl: resource }); } catch { img.style.visibility = 'hidden'; }
  });
};
