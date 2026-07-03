import { getSettings, setSettings, DEFAULT_EDITOR_URL, fetchAsDataUrl } from '../lib/stencil.js';
import { pageSizeOptions } from '../lib/cropGeometry.js';
import { PINS_KEY, loadPins, matchPinsForSite, sitesOf, setPinned } from '../lib/pins.js';
import { CONNECTIONS_KEY, loadConnections, addServer, removeServer, listProjects, collectSharedPins, reconnectServer } from '../lib/connections.js';
import { icon } from '../lib/icons.js';

// Theme accent — persisted separately in localStorage (window.StencilAccent, set
// up by lib/accent.js) so it applies flash-free across the extension's pages. It
// is independent of the Save button: changing it applies + persists instantly.
const accent = window.StencilAccent;
if (accent) {
  // Custom colour-swatch dropdown — a colour RECT + name per option, since a
  // native <select> can't paint per-option swatches on every OS (e.g. macOS).
  const mount = document.getElementById('accent');
  const meta = (key) => accent.list.find((a) => a.key === key) || accent.list[0];
  let value = accent.get();

  mount.innerHTML =
    '<button type="button" class="accent-dd-trigger" aria-haspopup="listbox" aria-expanded="false">' +
    '<span class="accent-swatch js-cur-sw"></span><span class="accent-dd-name js-cur-name"></span>' +
    '<span class="accent-dd-caret" aria-hidden="true">' + icon('chevron-down', { size: 13 }) + '</span></button>' +
    '<ul class="accent-dd-menu" role="listbox" hidden></ul>';
  const trigger = mount.querySelector('.accent-dd-trigger');
  const menu = mount.querySelector('.accent-dd-menu');
  const curSw = mount.querySelector('.js-cur-sw');
  const curName = mount.querySelector('.js-cur-name');

  for (const a of accent.list) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML = `<span class="accent-swatch" style="background:${a.hex}"></span><span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => choose(a.key));
    menu.appendChild(li);
  }

  const sync = () => {
    const m = meta(value);
    curSw.style.background = m.hex;
    curName.textContent = m.label;
    for (const li of menu.children)
      li.setAttribute('aria-selected', li.dataset.key === value ? 'true' : 'false');
  };
  const onDocPtr = (e) => { if (!mount.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') close(); };
  const open = () => { menu.hidden = false; trigger.setAttribute('aria-expanded', 'true'); document.addEventListener('pointerdown', onDocPtr, true); document.addEventListener('keydown', onKey); };
  const close = () => { menu.hidden = true; trigger.setAttribute('aria-expanded', 'false'); document.removeEventListener('pointerdown', onDocPtr, true); document.removeEventListener('keydown', onKey); };
  const choose = (key) => { value = accent.set(key); sync(); close(); };

  trigger.addEventListener('click', () => { menu.hidden ? open() : close(); });
  sync();
}

// ── On-page highlight colour: "theme" (follow the accent) or a custom hex ─────
const hlMode = document.getElementById('hl-mode');
const hlColor = document.getElementById('hl-color');
const hlCustomRow = document.getElementById('hl-custom-row');
const accentHex = () => { try { return window.StencilAccent.hexOf(window.StencilAccent.get()); } catch { return '#7c3aed'; } };
const syncHlCustomRow = () => { hlCustomRow.style.display = hlMode.value === 'custom' ? 'flex' : 'none'; };
hlMode.addEventListener('change', () => {
  // Seed the picker from the current accent the first time you switch to custom.
  if (hlMode.value === 'custom' && !hlColor.dataset.touched) hlColor.value = accentHex();
  syncHlCustomRow();
});
hlColor.addEventListener('input', () => { hlColor.dataset.touched = '1'; });

// Default page size — every ISO A/B/C format from the shared table (canonical
// order), labelled with its cm dimensions; the stored value is the bare name.
document.getElementById('page').innerHTML = pageSizeOptions();

(async () => {
  const { editorUrl, page, markOpened, openedFirst, highlightColor, exposeWindowStencil } = await getSettings();
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('page').value = page;
  document.getElementById('markOpened').checked = markOpened;
  document.getElementById('openedFirst').checked = openedFirst;
  document.getElementById('exposeWindowStencil').checked = exposeWindowStencil;
  // A hex means custom; 'theme' (or anything else) means follow the accent.
  if (/^#[0-9a-f]{3,8}$/i.test(highlightColor)) { hlMode.value = 'custom'; hlColor.value = highlightColor; hlColor.dataset.touched = '1'; }
  else { hlMode.value = 'theme'; hlColor.value = accentHex(); }
  syncHlCustomRow();
})();

document.getElementById('save').addEventListener('click', async () => {
  const editorUrl = (document.getElementById('editorUrl').value || '').trim() || DEFAULT_EDITOR_URL;
  const highlightColor = hlMode.value === 'custom' ? hlColor.value : 'theme';
  await setSettings({ editorUrl, page: document.getElementById('page').value, markOpened: document.getElementById('markOpened').checked, openedFirst: document.getElementById('openedFirst').checked, highlightColor, exposeWindowStencil: document.getElementById('exposeWindowStencil').checked });
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('status').innerHTML = icon('check', { size: 13 }) + ' Saved';
  setTimeout(() => { document.getElementById('status').textContent = ''; }, 1500);
});

// ── Pinned-images viewer ─────────────────────────────────────────────────────
// Browse every pin (chrome.storage.local, written by the popup / page API), grouped
// by the site it was pinned on, with open-in-new-tab and unpin. Re-fetches each
// thumbnail through the extension's host permissions when a bare <img> can't load it
// (hotlink-protected) — same recovery the popup uses.
const siteSel = document.getElementById('pin-site');
const pinListEl = document.getElementById('pin-list');
const pinEmptyEl = document.getElementById('pin-empty');
// Host label for a site origin (e.g. https://example.com → example.com).
const hostLabel = (origin) => { try { return new URL(origin).host; } catch { return origin || '(unknown site)'; } };

// Lazily recover a thumbnail a plain <img> couldn't load (hotlink-protected http(s));
// videos and unfetchable sources keep the neutral placeholder.
const recoverThumb = (img, source, kind) => {
  img.addEventListener('error', async () => {
    if (img.dataset.recovered || kind === 'video' || !/^https?:/i.test(source)) { img.style.visibility = 'hidden'; return; }
    img.dataset.recovered = '1';
    try { img.src = await fetchAsDataUrl(source); } catch { img.style.visibility = 'hidden'; }
  });
};

const renderPinRow = (pin, serverSources) => {
  const li = document.createElement('li');
  li.className = 'pin-row';
  // Golden outline + server badge when this pinned image is also stored on a server.
  const onServer = !!(serverSources && pin.source && serverSources.has(pin.source));
  if (onServer) li.classList.add('shared');

  const thumb = document.createElement('img');
  thumb.className = 'pin-thumb';
  thumb.loading = 'lazy';
  thumb.alt = '';
  thumb.src = pin.source;
  recoverThumb(thumb, pin.source, pin.kind);

  const info = document.createElement('div');
  info.className = 'pin-info';
  const name = document.createElement('div');
  name.className = 'pin-name';
  name.textContent = pin.name || pin.source;
  name.title = pin.source;
  const sub = document.createElement('div');
  sub.className = 'pin-sub';
  const kindEl = document.createElement('span');
  kindEl.className = 'pin-kind';
  kindEl.textContent = pin.kind || 'image';
  const siteEl = document.createElement('span');
  siteEl.className = 'site';
  siteEl.textContent = hostLabel(pin.site);
  siteEl.title = pin.site;
  sub.append(kindEl, siteEl);
  info.append(name, sub);

  const actions = document.createElement('div');
  actions.className = 'pin-actions';
  const open = document.createElement('button');
  open.className = 'pin-btn';
  open.title = 'Open in new tab';
  open.innerHTML = icon('external', { size: 15 });
  open.addEventListener('click', () => { if (pin.source) chrome.tabs.create({ url: pin.source }); });
  const unpin = document.createElement('button');
  unpin.className = 'pin-btn danger';
  unpin.title = 'Unpin';
  unpin.innerHTML = icon('x', { size: 15 });
  unpin.addEventListener('click', async () => {
    await setPinned({ source: pin.source, site: pin.site, pinned: false });
    renderPins();   // storage.onChanged also fires, but re-render now for instant feedback
  });
  actions.append(open, unpin);

  li.append(thumb, info, actions);
  return li;
};

const storeSel = document.getElementById('pin-store');
const showServerChk = document.getElementById('pin-show-server');

// Cached cross-reference of which connected servers store each pinned source URL.
// Drives the gold outline + the "stored on" filter; refreshed on load + connection changes.
const emptyServerPins = () => ({ sources: new Set(), byOrigin: new Map(), hosts: [] });
let serverPins = emptyServerPins();

const refreshServerPins = async () => {
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

const renderPins = async () => {
  const pins = await loadPins();
  const sites = sitesOf(pins);

  // Site filter (where the image was pinned). Keep the chosen site if it still has pins.
  const prevSite = siteSel.value || 'all';
  siteSel.innerHTML = `<option value="all">All sites (${pins.length})</option>` +
    sites.map((s) => `<option value="${s}">${hostLabel(s)} (${matchPinsForSite(pins, s).length})</option>`).join('');
  siteSel.value = (prevSite === 'all' || sites.includes(prevSite)) ? prevSite : 'all';

  // Storage filter (which server a pin is stored on) — populated from connected servers,
  // hidden (with the "show server pins" checkbox) when none are connected.
  const hasServers = serverPins.hosts.length > 0;
  showServerChk.closest('.chk').hidden = !hasServers;
  storeSel.hidden = !hasServers || !showServerChk.checked;
  if (hasServers) {
    const prevStore = storeSel.value || 'all';
    storeSel.innerHTML = '<option value="all">Any storage</option><option value="local">Local only</option>' +
      serverPins.hosts.map((u) => `<option value="${u}">On ${hostLabel(u)}</option>`).join('');
    storeSel.value = [...storeSel.options].some((o) => o.value === prevStore) ? prevStore : 'all';
  }

  const serversOf = (p) => serverPins.byOrigin.get(p.source) || null;   // Set<serverUrl> | null
  let shown = siteSel.value === 'all' ? pins : matchPinsForSite(pins, siteSel.value);
  shown = shown.filter((p) => {
    const servers = serversOf(p);
    if (!showServerChk.checked && servers) return false;                 // hide server pins
    if (!showServerChk.checked) return true;
    if (storeSel.value === 'local') return !servers;                     // local only
    if (storeSel.value !== 'all') return !!servers && servers.has(storeSel.value);  // a specific server
    return true;
  });
  pinListEl.innerHTML = '';
  shown.forEach((p) => pinListEl.appendChild(renderPinRow(p, serverPins.sources)));
  pinEmptyEl.hidden = pins.length > 0;
};

siteSel.addEventListener('change', renderPins);
storeSel.addEventListener('change', renderPins);
showServerChk.addEventListener('change', renderPins);

// ── Server connections ───────────────────────────────────────────────────────
// Add (connect + persist) / remove collaboration-server connections. The popup reads
// the same chrome.storage.local list to render shared pins and offer server pin targets.
const connUrl = document.getElementById('conn-url');
const connToken = document.getElementById('conn-token');
const connStatus = document.getElementById('conn-status');
const connListEl = document.getElementById('conn-list');
const connEmptyEl = document.getElementById('conn-empty');

const renderConnections = async () => {
  const conns = await loadConnections();
  connListEl.innerHTML = '';
  for (const c of conns) {
    const li = document.createElement('li');
    li.className = 'pin-row';
    const info = document.createElement('div');
    info.className = 'pin-info';
    const name = document.createElement('div');
    name.className = 'pin-name';
    // Status dot: yellow while we probe, green if reachable, red if not.
    const dot = document.createElement('span');
    dot.className = 'conn-status conn-status-connecting';
    dot.title = 'Checking…';
    name.append(dot);
    name.insertAdjacentHTML('beforeend', icon('server', { size: 14 }) + ' ' + hostLabel(c.url));
    name.title = c.url;
    info.appendChild(name);
    // Probe reachability (auth-checked via GET /projects) and recolor the dot.
    listProjects(c)
      .then(() => { dot.className = 'conn-status conn-status-connected'; dot.title = 'Connected'; })
      .catch(() => { dot.className = 'conn-status conn-status-error'; dot.title = 'Not reachable'; });
    const actions = document.createElement('div');
    actions.className = 'pin-actions';
    const reconnect = document.createElement('button');
    reconnect.className = 'pin-btn';
    reconnect.title = 'Reconnect (re-validate / reissue the token)';
    reconnect.innerHTML = icon('refresh', { size: 15 });
    reconnect.addEventListener('click', async () => {
      dot.className = 'conn-status conn-status-connecting';
      dot.title = 'Reconnecting…';
      try { await reconnectServer(c.url); } catch { /* stays red on re-probe */ }
      renderConnections();
    });
    const remove = document.createElement('button');
    remove.className = 'pin-btn danger';
    remove.title = 'Remove connection';
    remove.innerHTML = icon('x', { size: 15 });
    remove.addEventListener('click', async () => {
      await removeServer(c.url);
      renderConnections();
    });
    actions.append(reconnect, remove);
    li.append(info, actions);
    connListEl.appendChild(li);
  }
  connEmptyEl.hidden = conns.length > 0;
  const reconnectAll = document.getElementById('conn-reconnect-all');
  if (reconnectAll) reconnectAll.hidden = conns.length === 0;
};

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
    renderConnections();
    setTimeout(() => { connStatus.textContent = ''; }, 1500);
  } catch (err) {
    connStatus.textContent = `Failed: ${err.message}`;
  }
});

// Live-refresh when pins or connections change anywhere (popup, side panel, page API,
// another options tab).
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[PINS_KEY]) renderPins();
  // A connection change can flip which pins are server-stored → refresh the cross-ref.
  if (area === 'local' && changes[CONNECTIONS_KEY]) {
    renderConnections();
    refreshServerPins().then(renderPins);
  }
});
refreshServerPins().then(renderPins);
renderConnections();
