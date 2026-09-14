// Server connections; the popup reads the same chrome.storage.local list.
import { PINS_KEY } from '../lib/pins.js';
import { CONNECTIONS_KEY, loadConnections, addServer, removeServer, listProjects, reconnectServer, normalizeUrl, filterConnections, isAdminConnection } from '../lib/connections.js';
import { leaveThenRemove, materialize, scatterGridFor, createListHold, emptyStateVisible, createFilterTransition } from '../lib/motion.js';
import { icon } from '../lib/icons.js';
import { setTip } from '../lib/tip.js';
import { hostLabel } from './pinsDom.js';
import { renderPins, refreshServerPins } from './pins.js';

const connUrl = document.getElementById('conn-url');
const connToken = document.getElementById('conn-token');
const connStatus = document.getElementById('conn-status');
const connListEl = document.getElementById('conn-list');
const connEmptyEl = document.getElementById('conn-empty');
const connFiltersEl = document.getElementById('conn-filters');
// View-only kind filter ('all' | 'admin' | 'other') — not persisted, unlike the pin filters.
const connKind = () => {
  const on = connFiltersEl && connFiltersEl.querySelector('input:checked');
  return on ? on.value : 'all';
};

// While a row's leave/materialize plays, the storage.onChanged echo is deferred and the
// empty state hidden — a rebuild mid-wipe would cut the animation short.
const connHold = createListHold({ settle: () => { renderConnections(); connListEl.style.minHeight = ''; } });

// Keyed by data-url, the attribute the leave/materialize animations already find a row by.
const connTransition = createFilterTransition({ list: connListEl, keyAttr: 'url' });
// The ADD flow materializes its own row, so the transition must not also ramp it in.
let materializingUrl = null;
// Newest render wins — rapid filter clicks could otherwise land out of order.
let connRenderSeq = 0;

const renderConnections = async () => {
  const seq = ++connRenderSeq;
  const all = await loadConnections();
  if (seq !== connRenderSeq) return;   // a newer render is already in flight
  const conns = filterConnections(all, connKind());
  connTransition.begin();
  connListEl.innerHTML = '';
  for (const c of conns) {
    const li = document.createElement('li');
    // Same .pin-row shell as the pinned-image rows; .conn-row carries the thumb-less padding.
    li.className = 'pin-row conn-row' + (isAdminConnection(c) ? ' conn-admin' : '');
    if (isAdminConnection(c)) setTip(li, 'Admin connection — its credential can mint session tokens');
    li.dataset.url = c.url;   // the leave/materialize animations find the row by url
    const info = document.createElement('div');
    info.className = 'pin-info';
    const name = document.createElement('div');
    name.className = 'pin-name conn-name';
    const dot = document.createElement('span');
    dot.className = 'conn-status conn-status-connecting';
    dot.setAttribute('role', 'img');   // a bare <span> may not be named; the state is information
    setTip(dot, 'Checking…', { label: true });
    name.append(dot);
    name.insertAdjacentHTML('beforeend', icon('server', { size: 14 }));
    const label = document.createElement('span');
    label.className = 'conn-label';
    label.textContent = hostLabel(c.url);
    name.append(label);
    setTip(name, c.url);
    info.appendChild(name);
    listProjects(c)
      .then(() => { dot.className = 'conn-status conn-status-connected'; setTip(dot, 'Connected', { label: true }); })
      .catch(() => { dot.className = 'conn-status conn-status-error'; setTip(dot, 'Not reachable', { label: true }); });
    // Out of .conn-name, so the badge can't eat the host label's ellipsis budget.
    let badge = null;
    if (isAdminConnection(c)) {
      badge = document.createElement('span');
      badge.className = 'pin-badge-server conn-badge-admin';
      badge.textContent = 'admin';
      setTip(badge, 'Admin credential — this connection can mint session tokens');
    }
    const actions = document.createElement('div');
    actions.className = 'pin-actions';
    const reconnect = document.createElement('button');
    reconnect.className = 'pin-btn';
    setTip(reconnect, 'Reconnect (re-validate / reissue the token)', { label: true });
    reconnect.innerHTML = icon('refresh', { size: 15 });
    reconnect.addEventListener('click', async () => {
      dot.className = 'conn-status conn-status-connecting';
      setTip(dot, 'Reconnecting…', { label: true });
      try { await reconnectServer(c.url); } catch { /* stays red on re-probe */ }
      renderConnections();
    });
    const remove = document.createElement('button');
    remove.className = 'pin-btn danger';
    // A trash glyph, not an x — this deletes the connection and its saved token.
    setTip(remove, 'Remove connection — forgets its saved token', { label: true });
    remove.innerHTML = icon('trash', { size: 15 });
    remove.addEventListener('click', async () => {
      // The list's height is pinned and the re-render deferred until the dust has settled, so
      // the empty state can't land under it (browser connect-modal parity).
      const held = connListEl.getBoundingClientRect().height;
      if (held) connListEl.style.minHeight = `${held}px`;
      const settle = connHold.begin();
      // Leaves the DOM with its scatter, so the rebuild's filter transition can't fade it again.
      await leaveThenRemove(li, () => li.remove(), scatterGridFor(1));
      await removeServer(c.url);
      await settle();
    });
    actions.append(reconnect, remove);
    li.append(info, ...(badge ? [badge] : []), actions);
    connListEl.appendChild(li);
  }
  connTransition.end({ skipEnter: materializingUrl ? [materializingUrl] : [] });
  connEmptyEl.hidden = !emptyStateVisible(conns.length, connHold.holding);
  connEmptyEl.textContent = all.length
    ? 'No connections match this filter.'
    : 'No servers connected yet.';
  if (connFiltersEl) connFiltersEl.hidden = all.length === 0;
  const reconnectAll = document.getElementById('conn-reconnect-all');
  if (reconnectAll) reconnectAll.hidden = all.length === 0;
};

if (connFiltersEl)
  connFiltersEl.querySelectorAll('input').forEach((r) => r.addEventListener('change', renderConnections));

// Reconnect every saved server (re-validate / reissue tokens), then re-render the dots.
document.getElementById('conn-reconnect-all').addEventListener('click', async () => {
  const conns = await loadConnections();
  connStatus.textContent = 'Reconnecting…';
  await Promise.all(conns.map((c) => reconnectServer(c.url).catch(() => {})));
  connStatus.textContent = '';
  renderConnections();
});

document.getElementById('conn-add').addEventListener('click', async () => {
  const url = (connUrl.value || '').trim();
  if (!url) { connStatus.textContent = 'Enter a server URL.'; return; }
  connStatus.textContent = 'Connecting…';
  try {
    await addServer(url, (connToken.value || '').trim());
    connUrl.value = '';
    connToken.value = '';
    connStatus.innerHTML = icon('check', { size: 13 }) + ' Connected';
    // Back to All, or the filter in force could hide the row that was just added.
    const allPill = connFiltersEl && connFiltersEl.querySelector('input[value="all"]');
    if (allPill) allPill.checked = true;
    // On the same hold as a removal, so the storage.onChanged echo can't rebuild mid-animation.
    const settle = connHold.begin();
    materializingUrl = normalizeUrl(url);   // this row's arrival is the materialize, not the filter ramp
    await renderConnections();
    materialize(connListEl.querySelector(`li[data-url="${CSS.escape(materializingUrl)}"]`), scatterGridFor(1));
    materializingUrl = null;
    setTimeout(() => { connStatus.textContent = ''; }, 1500);
    await settle();
  } catch (err) {
    connStatus.textContent = `Failed: ${err.message}`;
  }
});

chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[PINS_KEY]) renderPins();
  // A connection change can flip which pins are server-stored → refresh the cross-ref.
  if (area === 'local' && changes[CONNECTIONS_KEY]) {
    // Never mid-wipe: the hold's settle render covers the change.
    if (!connHold.holding) renderConnections();
    refreshServerPins().then(renderPins);
  }
});
refreshServerPins().then(renderPins);
renderConnections();
