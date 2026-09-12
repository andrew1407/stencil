// The projects list's drag-out drop zones: a pointer-events:none overlay painted over the
// dialog while a row is dragged; the caller decides the action via zoneForPoint.
import { icon } from './icons.js';
import { pointInRect } from '../utils.js';

export const createDropZones = (overlay) => {
  let zonesEl = null;
  const buildZones = () => {
    const wrap = document.createElement('div');
    wrap.className = 'project-dropzones';
    wrap.innerHTML =
      '<div class="pdz pdz-here" data-action="here"><div class="pdz-label">' + icon('folder', { size: 22 }) + '<span>Open here</span></div></div>'
      + '<div class="pdz pdz-newtab" data-action="newtab"><div class="pdz-label">' + icon('external', { size: 22 }) + '<span>Open in a new tab</span></div></div>'
      + '<div class="pdz pdz-remove" data-action="remove"><div class="pdz-label">' + icon('trash', { size: 22 }) + '<span>Remove</span></div></div>';
    return wrap;
  };
  const ensureZones = () => { if (!zonesEl) { zonesEl = buildZones(); overlay.insertBefore(zonesEl, overlay.firstChild); } return zonesEl; };
  const showZones = () => ensureZones().classList.add('is-dragging');
  const hideZones = () => { if (zonesEl) { zonesEl.classList.remove('is-dragging'); zonesEl.querySelectorAll('.pdz-over').forEach((z) => z.classList.remove('pdz-over')); } };
  // Null when the point is over the dialog card. Bottom 30% = remove, else top split left/right.
  const zoneForPoint = (x, y) => {
    const card = overlay.querySelector('.app-modal');
    const r = card && card.getBoundingClientRect();
    if (r && pointInRect(x, y, r)) return null;  // over the dialog
    if (y > window.innerHeight * 0.7) return 'remove';
    return x < window.innerWidth / 2 ? 'here' : 'newtab';
  };
  const highlightZone = (zone) => {
    if (!zonesEl) return;
    for (const z of zonesEl.querySelectorAll('.pdz')) z.classList.toggle('pdz-over', z.dataset.action === zone);
  };

  return { showZones, hideZones, zoneForPoint, highlightZone };
};
