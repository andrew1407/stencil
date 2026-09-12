// Shared by pins.js (the render pass) and pinRow.js (one row).
import { fetchAsDataUrl } from '../lib/stencil.js';
import { createFilterTransition } from '../lib/motion.js';

export const siteSel = document.getElementById('pin-site');
export const pinListEl = document.getElementById('pin-list');
export const pinEmptyEl = document.getElementById('pin-empty');
export const pinClearBtn = document.getElementById('pin-clear');
export const pinSearchEl = document.getElementById('pin-search');
export const pinSearchModeEl = document.getElementById('pin-search-mode');
export const hostLabel = (origin) => { try { return new URL(origin).host; } catch { return origin || '(unknown site)'; } };

// Both lists rebuild wholesale on every filter change; a pin is keyed by the pair that
// identifies it in storage, a connection by its URL (the data-url the animations find it by).
export const pinKey = (pin) => `${pin.site}\n${pin.source}`;
export const pinTransition = createFilterTransition({ list: pinListEl });

// Moved to <body> so the list rebuild underneath can't take the dust with it.
export const liftDust = (el) => {
  const parent = el.parentElement;
  if (!parent) return;
  for (const host of parent.querySelectorAll('.disintegrate-host')) document.body.appendChild(host);
};

// Hotlink-protected http(s) thumbnails are re-fetched; videos and unfetchable sources keep the placeholder.
export const recoverThumb = (img, source, kind, resource = '') => {
  img.addEventListener('error', async () => {
    if (img.dataset.recovered || kind === 'video' || !/^https?:/i.test(source)) { img.style.visibility = 'hidden'; return; }
    img.dataset.recovered = '1';
    // `resource` = the page the pin was made on (recorded at pin time) — same-host carve-out.
    try { img.src = await fetchAsDataUrl(source, { pageUrl: resource }); } catch { img.style.visibility = 'hidden'; }
  });
};
