// Pinned-images viewer, grouped by pinning site. Thumbnails a bare <img> can't load
// (hotlink-protected) are re-fetched through the extension's host permissions.
import { loadPins, matchPinsForSite, sitesOf, clearPins, pinMatchesSearch } from '../lib/pins.js';
import { loadConnections, collectSharedPins } from '../lib/connections.js';
import { leaveThenRemove, scatterGridFor } from '../lib/motion.js';
import { setTip } from '../lib/tip.js';
import { siteSel, pinListEl, pinEmptyEl, pinClearBtn, pinSearchEl, pinSearchModeEl, hostLabel, pinTransition, liftDust } from './pinsDom.js';
import { renderPinRow } from './pinRow.js';
import { confirmDialog } from './confirmDialog.js';
import { escapeHtml } from '../lib/escapeHtml.js';

const storeSel = document.getElementById('pin-store');
const showServerChk = document.getElementById('pin-show-server');

// Which connected servers store each pinned source URL: the gold outline + the "stored on" filter.
const emptyServerPins = () => ({ sources: new Set(), byOrigin: new Map(), hosts: [] });
let serverPins = emptyServerPins();

export const refreshServerPins = async () => {
  try {
    const conns = await loadConnections();
    if (!conns.length) { serverPins = emptyServerPins(); return; }
    const sources = new Set();
    const byOrigin = new Map();   // origin URL -> Set(serverUrl)
    for (const p of await collectSharedPins(conns)) {
      if (!p.origin) continue;
      sources.add(p.origin);
      if (!byOrigin.has(p.origin)) byOrigin.set(p.origin, new Set());
      byOrigin.get(p.origin).add(p.serverUrl);
    }
    serverPins = { sources, byOrigin, hosts: conns.map((c) => c.url) };
  } catch { serverPins = emptyServerPins(); }
};

// Newest render wins — a burst of filter changes could otherwise land out of order.
let pinRenderSeq = 0;

export const renderPins = async () => {
  const seq = ++pinRenderSeq;
  const pins = await loadPins();
  if (seq !== pinRenderSeq) return;   // a newer render is already in flight
  const sites = sitesOf(pins);

  // Keep the chosen site if it still has pins.
  const prevSite = siteSel.value || 'all';
  siteSel.innerHTML = `<option value="all">All sites (${pins.length})</option>` +
    sites.map((s) => `<option value="${escapeHtml(s)}">${escapeHtml(hostLabel(s))} (${matchPinsForSite(pins, s).length})</option>`).join('');
  siteSel.value = (prevSite === 'all' || sites.includes(prevSite)) ? prevSite : 'all';

  // The Clear button's label + enabled state track the site filter's scope, so it matches what it removes.
  const clearScoped = siteSel.value !== 'all';
  const clearCount = clearScoped ? matchPinsForSite(pins, siteSel.value).length : pins.length;
  pinClearBtn.textContent = clearScoped ? 'Clear site' : 'Clear all';
  setTip(pinClearBtn, clearScoped
    ? `Remove all pinned images for ${hostLabel(siteSel.value)}`
    : 'Remove every pinned image, on all sites');
  pinClearBtn.disabled = clearCount === 0;

  // Hidden (with the "show server pins" checkbox) when no server is connected.
  const hasServers = serverPins.hosts.length > 0;
  showServerChk.closest('.chk').hidden = !hasServers;
  storeSel.hidden = !hasServers || !showServerChk.checked;
  if (hasServers) {
    const prevStore = storeSel.value || 'all';
    storeSel.innerHTML = '<option value="all">Any storage</option><option value="local">Local only</option>' +
      serverPins.hosts.map((u) => `<option value="${escapeHtml(u)}">On ${escapeHtml(hostLabel(u))}</option>`).join('');
    storeSel.value = [...storeSel.options].some((o) => o.value === prevStore) ? prevStore : 'all';
  }

  const serversOf = (p) => serverPins.byOrigin.get(p.source) || null;   // Set<serverUrl> | null
  const query = pinSearchEl ? pinSearchEl.value : '';
  const searchMode = pinSearchModeEl ? pinSearchModeEl.value : 'common';
  let shown = siteSel.value === 'all' ? pins : matchPinsForSite(pins, siteSel.value);
  shown = shown.filter((p) => {
    if (!pinMatchesSearch(p, query, searchMode)) return false;           // name/keyword search
    const servers = serversOf(p);
    if (!showServerChk.checked && servers) return false;                 // hide server pins
    if (!showServerChk.checked) return true;
    if (storeSel.value === 'local') return !servers;                     // local only
    if (storeSel.value !== 'all') return !!servers && servers.has(storeSel.value);  // a specific server
    return true;
  });
  pinTransition.begin();
  pinListEl.innerHTML = '';
  shown.forEach((p) => pinListEl.appendChild(renderPinRow(p, serverPins.sources)));
  pinTransition.end();
  pinEmptyEl.hidden = pins.length > 0;
};

siteSel.addEventListener('change', renderPins);
storeSel.addEventListener('change', renderPins);
showServerChk.addEventListener('change', renderPins);
// One render per typing burst keeps the fades from stacking.
const PIN_SEARCH_DEBOUNCE_MS = 150;
let pinSearchTimer = null;
if (pinSearchEl) pinSearchEl.addEventListener('input', () => {
  clearTimeout(pinSearchTimer);
  pinSearchTimer = setTimeout(renderPins, PIN_SEARCH_DEBOUNCE_MS);
});
if (pinSearchModeEl) pinSearchModeEl.addEventListener('change', renderPins);


const wipePinRows = async () => {
  pinTransition.clear();   // a row already fading out of the filter isn't there to destroy
  const rows = [...pinListEl.children];
  await Promise.all(rows.map((el, i) => {
    const played = leaveThenRemove(el, () => el.remove(), scatterGridFor(rows.length, i));
    liftDust(el);
    return played;
  }));
};

pinClearBtn.addEventListener('click', async () => {
  const pins = await loadPins();
  const scope = siteSel.value || 'all';
  if (scope === 'all') {
    if (!pins.length) return;
    if (!(await confirmDialog(`Are you sure? Remove ALL ${pins.length} pinned image(s) from every site? This cannot be undone.`, pinClearBtn))) return;
    await wipePinRows();
    await clearPins('all');
  } else {
    const n = matchPinsForSite(pins, scope).length;
    if (!n) return;
    if (!(await confirmDialog(`Are you sure? Remove all ${n} pinned image(s) for ${hostLabel(scope)}? This cannot be undone.`, pinClearBtn))) return;
    await wipePinRows();
    await clearPins(scope);
  }
  renderPins();   // storage.onChanged also fires; re-render now for instant feedback
});
