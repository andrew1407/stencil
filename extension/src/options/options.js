import { getSettings, setSettings, DEFAULT_EDITOR_URL, fetchAsDataUrl, originPattern } from '../lib/stencil.js';
import { pageSizeOptions } from '../lib/cropGeometry.js';
import { PINS_KEY, loadPins, matchPinsForSite, sitesOf, setPinned, clearPins, setPinKeywords, pinMatchesSearch, pinKeywords } from '../lib/pins.js';
import { CONNECTIONS_KEY, loadConnections, addServer, removeServer, listProjects, collectSharedPins, reconnectServer, normalizeUrl } from '../lib/connections.js';
import { leaveThenRemove, materialize, scatterGridFor, createListHold, emptyStateVisible } from '../lib/motion.js';
import { icon } from '../lib/icons.js';
import { loadLlmSettings, saveLlmSettings, PROVIDER_BASE_URLS, LLM_SETTINGS_KEY } from '../llm/llmSettings.js';
import { listModels } from '../llm/llmClient.js';
import { serverTokenFor } from '../llm/llmSurface.js';
import { initTooltips } from '../lib/controlTooltip.js';
import { enhanceSelect } from '../lib/customSelect.js';

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
  // The wipe starts at the dropdown TRIGGER, not at the option row: the menu is gone by
  // the time the palette floods, and a row near the top of a scrolled menu would look
  // like the colour came out of a corner.
  const choose = (key) => { value = accent.set(key, trigger); sync(); close(); };

  trigger.addEventListener('click', () => { menu.hidden ? open() : close(); });
  sync();
}

// Appearance — like the accent, it lives in localStorage via lib/accent.js rather than
// in the saved settings, so it applies instantly, independent of the Save button.
const themePref = window.StencilTheme;
const appearance = document.getElementById('appearance');
if (themePref && appearance) {
  const syncAppearance = () => { appearance.value = themePref.get(); };
  // Anchor the wipe to the <select> itself. Choosing from a native dropdown fires no
  // pointerdown in the page, so there is no press to read — and this page has no
  // #theme-toggle to fall back to either.
  appearance.addEventListener('change', () => { themePref.set(appearance.value, appearance); syncAppearance(); });
  themePref.onChange(syncAppearance);
  syncAppearance();
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
  const { editorUrl, page, markOpened, openedFirst, highlightColor, exposeWindowStencil, editorPageApi, desktopScheme, telegramBotUsername } = await getSettings();
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('page').value = page;
  document.getElementById('markOpened').checked = markOpened;
  document.getElementById('openedFirst').checked = openedFirst;
  document.getElementById('exposeWindowStencil').checked = exposeWindowStencil;
  document.getElementById('editorPageApi').checked = editorPageApi;
  document.getElementById('desktopScheme').value = desktopScheme;
  document.getElementById('telegramBotUsername').value = telegramBotUsername;
  // A hex means custom; 'theme' (or anything else) means follow the accent.
  if (/^#[0-9a-f]{3,8}$/i.test(highlightColor)) { hlMode.value = 'custom'; hlColor.value = highlightColor; hlColor.dataset.touched = '1'; }
  else { hlMode.value = 'theme'; hlColor.value = accentHex(); }
  syncHlCustomRow();
})();

document.getElementById('save').addEventListener('click', async () => {
  const editorUrl = (document.getElementById('editorUrl').value || '').trim() || DEFAULT_EDITOR_URL;
  const highlightColor = hlMode.value === 'custom' ? hlColor.value : 'theme';
  // Trim the "Open in…" operator config; a bare "@name" for the bot is tolerated.
  const desktopScheme = (document.getElementById('desktopScheme').value || '').trim();
  const telegramBotUsername = (document.getElementById('telegramBotUsername').value || '').trim().replace(/^@/, '');
  await setSettings({ editorUrl, page: document.getElementById('page').value, markOpened: document.getElementById('markOpened').checked, openedFirst: document.getElementById('openedFirst').checked, highlightColor, exposeWindowStencil: document.getElementById('exposeWindowStencil').checked, editorPageApi: document.getElementById('editorPageApi').checked, desktopScheme, telegramBotUsername });
  document.getElementById('telegramBotUsername').value = telegramBotUsername;
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('status').innerHTML = icon('check', { size: 13 }) + ' Saved';
  setTimeout(() => { document.getElementById('status').textContent = ''; }, 1500);
});

// ── AI assistant (LLM) settings ──────────────────────────────────────────────
// Persisted under the chrome.storage key `llmSettings` (llm-contract.md §5/§8). The
// base/server URL is default-refilled per provider but stays editable;
// ensureLlmHostPermission stays as a guard in case <all_urls> ever narrows.
const llmProviderEl = document.getElementById('llm-provider');
const llmBaseUrlEl = document.getElementById('llm-baseurl');
const llmModelEl = document.getElementById('llm-model');
const llmApiKeyEl = document.getElementById('llm-apikey');
const llmServerUrlEl = document.getElementById('llm-serverurl');
const llmServerTokenEl = document.getElementById('llm-servertoken');
const llmShareTabsEl = document.getElementById('llm-sharetabs');
const llmStatusEl = document.getElementById('llm-status');
let llmStored = null;   // last-loaded settings, so switching providers restores saved URLs

const syncLlmRows = () => {
  const server = llmProviderEl.value === 'stencil-server';
  const off = llmProviderEl.value === 'none';
  document.getElementById('llm-base-rows').hidden = server || off;
  document.getElementById('llm-server-rows').hidden = !server;
  document.getElementById('llm-model-row').hidden = off;
  document.getElementById('llm-tabs-row').hidden = off;   // nothing is sent when off
};

// Refill the base URL for the chosen provider: the saved value when the saved
// provider matches, else the provider's default. Editable afterwards.
const refillLlmBaseUrl = () => {
  const p = llmProviderEl.value;
  llmBaseUrlEl.value = (llmStored && llmStored.provider === p && llmStored.baseUrl)
    ? llmStored.baseUrl
    : (PROVIDER_BASE_URLS[p] || '');
};

// Model suggestions from the provider itself (browser/desktop parity): the datalist
// refills from the CURRENTLY EDITED fields, best-effort — failures leave it empty.
// A request counter drops stale async answers when the fields change mid-fetch.
const llmModelListEl = document.getElementById('llm-model-list');
let llmModelsReq = 0;
const refreshLlmModels = async () => {
  const req = ++llmModelsReq;
  const settings = {
    provider: llmProviderEl.value,
    baseUrl: (llmBaseUrlEl.value || '').trim(),
    apiKey: (llmApiKeyEl.value || '').trim(),
    serverUrl: (llmServerUrlEl.value || '').trim(),
    serverToken: (llmServerTokenEl.value || '').trim(),
  };
  const names = await listModels(settings, {
    getToken: async (u) => serverTokenFor(u, { connections: await loadConnections(), settings }),
  });
  if (req !== llmModelsReq) return;   // fields changed while fetching
  llmModelListEl.textContent = '';
  for (const n of names) {
    const opt = document.createElement('option');
    opt.value = n;
    llmModelListEl.appendChild(opt);
  }
};

const loadLlmForm = async () => {
  llmStored = await loadLlmSettings();
  llmProviderEl.value = llmStored.provider;
  llmModelEl.value = llmStored.model;
  llmApiKeyEl.value = llmStored.apiKey;
  llmServerUrlEl.value = llmStored.serverUrl;
  llmServerTokenEl.value = llmStored.serverToken;
  llmShareTabsEl.checked = llmStored.shareTabs === true;
  refillLlmBaseUrl();
  syncLlmRows();
  refreshLlmModels();
};

llmProviderEl.addEventListener('change', () => { refillLlmBaseUrl(); syncLlmRows(); refreshLlmModels(); });
llmBaseUrlEl.addEventListener('change', refreshLlmModels);
llmApiKeyEl.addEventListener('change', refreshLlmModels);
llmServerUrlEl.addEventListener('change', refreshLlmModels);
llmServerTokenEl.addEventListener('change', refreshLlmModels);

// Make sure the extension may fetch the configured origin: covered origins pass
// silently; anything else is requested from the user (needs this click's gesture).
const ensureLlmHostPermission = async (url) => {
  const pattern = originPattern(url);
  if (!pattern || !chrome.permissions) return true;
  try {
    if (await chrome.permissions.contains({ origins: [pattern] })) return true;
    return await chrome.permissions.request({ origins: [pattern] });
  } catch {
    return true;   // permissions API unavailable — the fetch itself will surface any block
  }
};

document.getElementById('llm-save').addEventListener('click', async () => {
  const s = {
    provider: llmProviderEl.value,
    baseUrl: (llmBaseUrlEl.value || '').trim(),
    model: (llmModelEl.value || '').trim(),
    apiKey: (llmApiKeyEl.value || '').trim(),
    serverUrl: (llmServerUrlEl.value || '').trim(),
    serverToken: (llmServerTokenEl.value || '').trim(),
    shareTabs: llmShareTabsEl.checked === true,
  };
  llmStored = await saveLlmSettings(s);
  const active = s.provider === 'stencil-server' ? s.serverUrl : s.baseUrl;
  const granted = active ? await ensureLlmHostPermission(active) : true;
  llmStatusEl.innerHTML = icon('check', { size: 13 }) + ' Saved';
  if (!granted) llmStatusEl.textContent = 'Saved — but access to that origin was not granted, so calls to it will fail.';
  else setTimeout(() => { llmStatusEl.textContent = ''; }, 1500);
});

// Another surface (or window) changed the assistant settings — reload the form.
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LLM_SETTINGS_KEY]) loadLlmForm();
});
loadLlmForm();

// ── Pinned-images viewer ─────────────────────────────────────────────────────
// Browse every pin (chrome.storage.local), grouped by pinning site, with open and
// unpin. Thumbnails a bare <img> can't load (hotlink-protected) are re-fetched
// through the extension's host permissions — the popup's recovery.
const siteSel = document.getElementById('pin-site');
const pinListEl = document.getElementById('pin-list');
const pinEmptyEl = document.getElementById('pin-empty');
const pinClearBtn = document.getElementById('pin-clear');
const pinSearchEl = document.getElementById('pin-search');
const pinSearchModeEl = document.getElementById('pin-search-mode');
// Host label for a site origin (e.g. https://example.com → example.com).
const hostLabel = (origin) => { try { return new URL(origin).host; } catch { return origin || '(unknown site)'; } };

// Lazily recover a thumbnail a plain <img> couldn't load (hotlink-protected http(s));
// videos and unfetchable sources keep the neutral placeholder.
const recoverThumb = (img, source, kind, resource = '') => {
  img.addEventListener('error', async () => {
    if (img.dataset.recovered || kind === 'video' || !/^https?:/i.test(source)) { img.style.visibility = 'hidden'; return; }
    img.dataset.recovered = '1';
    // `resource` = the page the pin was made on (recorded at pin time) — same-host carve-out.
    try { img.src = await fetchAsDataUrl(source, { pageUrl: resource }); } catch { img.style.visibility = 'hidden'; }
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
  recoverThumb(thumb, pin.source, pin.kind, pin.resource || '');

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

  // Keyword chips + an inline editor (comma/space separated). Saved via setPinKeywords.
  const kwRow = document.createElement('div');
  kwRow.className = 'pin-keywords';
  const kws = pinKeywords(pin);
  for (const k of kws) {
    const chip = document.createElement('span');
    chip.className = 'pin-kw';
    chip.textContent = k;
    kwRow.appendChild(chip);
  }
  const editBtn = document.createElement('button');
  editBtn.type = 'button';
  editBtn.className = 'pin-kw-edit';
  editBtn.textContent = kws.length ? 'edit keywords' : '+ keywords';
  editBtn.addEventListener('click', () => {
    const input = document.createElement('input');
    input.type = 'text';
    input.className = 'pin-kw-input';
    input.value = kws.join(', ');
    input.placeholder = 'comma or space separated';
    let done = false;
    const save = async () => {
      if (done) return;
      done = true;
      await setPinKeywords(pin.site, pin.source, input.value.split(/[\s,]+/));
      renderPins();
    };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); save(); }
      else if (e.key === 'Escape') { done = true; renderPins(); }
    });
    input.addEventListener('blur', save);
    kwRow.replaceWith(input);
    input.focus();
    input.select();
  });
  kwRow.appendChild(editBtn);
  info.append(name, sub, kwRow);

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

  // The Clear button targets whatever the site filter shows: every pin, or just the
  // selected site. Its label + enabled-state track that scope so it matches what it removes.
  const clearScoped = siteSel.value !== 'all';
  const clearCount = clearScoped ? matchPinsForSite(pins, siteSel.value).length : pins.length;
  pinClearBtn.textContent = clearScoped ? 'Clear site' : 'Clear all';
  pinClearBtn.title = clearScoped
    ? `Remove all pinned images for ${hostLabel(siteSel.value)}`
    : 'Remove every pinned image, on all sites';
  pinClearBtn.disabled = clearCount === 0;

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
  pinListEl.innerHTML = '';
  shown.forEach((p) => pinListEl.appendChild(renderPinRow(p, serverPins.sources)));
  pinEmptyEl.hidden = pins.length > 0;
};

siteSel.addEventListener('change', renderPins);
storeSel.addEventListener('change', renderPins);
showServerChk.addEventListener('change', renderPins);
if (pinSearchEl) pinSearchEl.addEventListener('input', renderPins);
if (pinSearchModeEl) pinSearchModeEl.addEventListener('change', renderPins);

// Themed Yes/No confirmation. The options page has no native modal of its own, so this
// stands in for window.confirm() and matches the editor's look (theme.css vars). Resolves
// true on Yes/Enter, false on No/Esc/backdrop click.
const confirmDialog = (message) => new Promise((resolve) => {
  const overlay = document.getElementById('confirm-overlay');
  document.getElementById('confirm-msg').textContent = message;
  const yes = document.getElementById('confirm-yes');
  const no = document.getElementById('confirm-no');
  const done = (val) => {
    overlay.hidden = true;
    yes.removeEventListener('click', onYes);
    no.removeEventListener('click', onNo);
    overlay.removeEventListener('mousedown', onBackdrop);
    document.removeEventListener('keydown', onKey, true);
    resolve(val);
  };
  const onYes = () => done(true);
  const onNo = () => done(false);
  const onBackdrop = (e) => { if (e.target === overlay) done(false); };
  const onKey = (e) => {
    if (e.key === 'Escape') { e.stopPropagation(); done(false); }
    else if (e.key === 'Enter') { e.preventDefault(); done(true); }
  };
  yes.addEventListener('click', onYes);
  no.addEventListener('click', onNo);
  overlay.addEventListener('mousedown', onBackdrop);
  document.addEventListener('keydown', onKey, true);
  overlay.hidden = false;
  yes.focus();
});

// Scoped bulk clear: wipe every pin ("all" scope) or just the selected site's pins.
// Confirmed first — it's destructive and can't be undone.
pinClearBtn.addEventListener('click', async () => {
  const pins = await loadPins();
  const scope = siteSel.value || 'all';
  if (scope === 'all') {
    if (!pins.length) return;
    if (!(await confirmDialog(`Are you sure? Remove ALL ${pins.length} pinned image(s) from every site? This cannot be undone.`))) return;
    await clearPins('all');
  } else {
    const n = matchPinsForSite(pins, scope).length;
    if (!n) return;
    if (!(await confirmDialog(`Are you sure? Remove all ${n} pinned image(s) for ${hostLabel(scope)}? This cannot be undone.`))) return;
    await clearPins(scope);
  }
  renderPins();   // storage.onChanged also fires; re-render now for instant feedback
});

// ── Server connections ───────────────────────────────────────────────────────
// Add (connect + persist) / remove collaboration-server connections. The popup reads
// the same chrome.storage.local list to render shared pins and offer server pin targets.
const connUrl = document.getElementById('conn-url');
const connToken = document.getElementById('conn-token');
const connStatus = document.getElementById('conn-status');
const connListEl = document.getElementById('conn-list');
const connEmptyEl = document.getElementById('conn-empty');

// Wipe hold + refresh gate (browser connect modal's pattern, via createListHold): while
// a row's leave/materialize plays, the storage.onChanged echo is deferred and the empty
// state hidden — a rebuild mid-wipe would cut the animation and pop the placeholder in.
const connHold = createListHold({ settle: () => { renderConnections(); connListEl.style.minHeight = ''; } });

const renderConnections = async () => {
  const conns = await loadConnections();
  connListEl.innerHTML = '';
  for (const c of conns) {
    const li = document.createElement('li');
    li.className = 'pin-row';
    li.dataset.url = c.url;   // the leave/materialize animations find the row by url
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
      // The row scatters before the list is rebuilt without it (browser connect
      // modal parity): the list's height is pinned and the re-render deferred until
      // the dust has really settled, so the empty state can't land under it.
      const held = connListEl.getBoundingClientRect().height;
      if (held) connListEl.style.minHeight = `${held}px`;
      const settle = connHold.begin();
      await leaveThenRemove(li, () => {}, scatterGridFor(1));
      await removeServer(c.url);
      await settle();
    });
    actions.append(reconnect, remove);
    li.append(info, actions);
    connListEl.appendChild(li);
  }
  // Mid-wipe the empty state stays hidden — it waits for the hold's settle render.
  connEmptyEl.hidden = !emptyStateVisible(conns.length, connHold.holding);
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
    // The new row materializes — the removal played backwards (its box expands while
    // a dust copy gathers into it). On the same hold as a removal, so the
    // storage.onChanged echo can't rebuild the list mid-animation.
    const settle = connHold.begin();
    await renderConnections();
    materialize(connListEl.querySelector(`li[data-url="${CSS.escape(normalizeUrl(url))}"]`), scatterGridFor(1));
    setTimeout(() => { connStatus.textContent = ''; }, 1500);
    await settle();
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
    // Never mid-wipe: the rebuild would cut the leave/materialize short and pop the
    // empty state in under the dust — the hold's settle render covers the change.
    if (!connHold.holding) renderConnections();
    refreshServerPins().then(renderPins);
  }
});
refreshServerPins().then(renderPins);
renderConnections();

// Instant, structured tooltips everywhere on this page (the native `title` waits ~1s
// and never shows on a disabled control). lib/tipContent.js gives them their shape.
initTooltips();

// Every <select> on this page gets our own list: the native one is drawn by the OS, in
// system type, ignoring this panel's theme (see lib/customSelect.js). The page-size list
// is long enough to want its filter input.
for (const el of document.querySelectorAll('select'))
  enhanceSelect(el, { search: el.id === 'page' });
