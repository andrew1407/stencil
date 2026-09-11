// ── External launch: `#stencil=<encodeURIComponent(JSON)>` and the extension bridge ──
// Extracted from drawingApp.js; each function takes the app, which keeps only thin
// delegators. Schema/precedence live in deepLink.js (normalizeLaunchPayload).
import { notify, shortName } from '../utils.js';
import { normalizeLaunchPayload, LAUNCH_DATA_URL_MAX } from './deepLink.js';
import { normalizePageSize } from './units.js';
import { normalizeUrl } from '../net/connectionManager.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { timeoutSignal } from '../net/abortable.js';
import { revealControls } from '../ui/motion.js';

// Base name without its file extension (for project naming / source matching).
export const stripExt = (name) => {
  const s = String(name || '');
  const dot = s.lastIndexOf('.');
  return dot > 0 ? s.slice(0, dot) : s;
};

// ── The external-import tail, shared by every surface handing an image to THIS tab ──
// `mode`: 'new' = its own project (the only mode the naming options apply to); 'replace' /
// 'replace-keep' = swap the active project's image, dropping or keeping its lines. Rejects on
// a failed fetch. A free function (not a method) so tests can drive plain stand-in apps.
export const importInlineImage = (app, launch, { mode = 'new' } = {}) => {
  const name = launch.name || 'image.png';
  // Auto-number the name against existing same-source projects ("name (1)", …);
  // skipped for incognito, which never persists.
  const opts = launch.crop ? { crop: launch.crop } : {};
  if (!launch.crop && launch.noCrop) opts.noCrop = true;   // Open-Image dialog "Crop off" → full frame
  opts.source = launch.source;
  opts.resource = launch.resource;
  if (!app.storage.incognito && launch.source) opts.name = app.storage.store.copyName(stripExt(name), launch.source);
  // An inline layout (desktop/bot hand-off) restores annotations + filter + crop + page.
  if (launch.layout) {
    opts.layout = launch.layout;
    opts.adoptLayout = true;
  }

  // `src` launches carry an http(s) image URL instead of inline bytes (kept short for
  // links sent through chat). The fetch is best-effort: the host must allow CORS.
  const imageUrl = launch.kind === 'src' ? launch.src : launch.dataUrl;
  if (launch.kind === 'src' && !opts.source) opts.source = launch.src;
  return fetch(imageUrl, { signal: timeoutSignal(), ...(launch.kind === 'src' ? { mode: 'cors' } : null) })
    .then(r => { if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.blob(); })
    .then(blob => {
      const file = new File([blob], name, { type: blob.type || 'image/png' });
      if (mode === 'new') app.loadImageFromFile(file, opts);
      else app.replaceProjectImage(file, { keepAnnotations: mode === 'replace-keep', crop: opts.crop });
    });
};

// Fragment (not query) keeps the payload off servers/logs; consumed once, stripped, routed
// through the normal upload. `open:'resume'` switches to an existing same-source project;
// else import a new one.
export const applyExternalLaunch = (app) => {
  const hash = location.hash || '';
  const prefix = '#stencil=';
  if (!hash.startsWith(prefix)) return;
  // Strip the fragment immediately so a reload doesn't re-import the image.
  history.replaceState(null, '', location.pathname + location.search);

  let payload;
  // Chrome caps fragments around 2M chars, but lax environments don't: bound
  // the raw hash before decode/parse (same cap normalizeLaunchPayload applies
  // to the decoded dataUrl).
  if (hash.length > LAUNCH_DATA_URL_MAX) {
    notify('Stencil: could not read the shared image', 'fail');
    return;
  }
  try {
    payload = JSON.parse(decodeURIComponent(hash.slice(prefix.length)));
  } catch {
    notify('Stencil: could not read the shared image', 'fail');
    return;
  }
  const launch = normalizeLaunchPayload(payload);
  if (!launch) return;

  // Page size must be applied BEFORE loading so the crop aspect and pixel↔page
  // conversion match the size the image was cropped for by the sender.
  if (launch.page) setExternalPage(app, launch.page);

  if (launch.incognito) {
    app.storage.incognito = true;
    app.updateIncognitoUI();
  }

  if (launch.kind === 'server') {
    applyServerLaunch(app, launch)
      .catch(err => notify(`Could not open the server project — ${err.message}`, 'fail'));
    return;
  }

  const name = launch.name || 'image.png';
  const source = launch.source;

  // Resume: if we hold project(s) for this source, switch instead of re-importing.
  // Several matches → open the projects list to pick. No match (stale ledger / expired
  // project) falls through to a fresh import.
  if (launch.open === 'resume' && !app.storage.incognito && (source || name)
      && resumeBySource(app, source, name)) {
    return;
  }

  // Fresh import (`open:'copy'` takes the same path).
  importInlineImage(app, launch)
    .catch(() => notify('Stencil: failed to load the shared image', 'fail'));
};

// The extension bridge's entry point: import a hand-off into THIS tab instead of a fresh one.
// For 'new', do explicitly what the fragment path gets free from a fresh page: flush + reset
// to a blank editor (loadImageFromFile only promotes a TEMPORARY one), and apply the page
// size BEFORE the load so the crop aspect matches the sender's. A replace keeps identity.
export const importExternalImage = (app, launch, { mode = 'new' } = {}) => {
  if (mode === 'new') {
    const incognito = app.storage.incognito;
    if (!incognito) app.storage.save();
    app.newEditor();
    if (incognito) { app.storage.incognito = true; app.updateIncognitoUI(); }
    if (launch.page) setExternalPage(app, launch.page);
  }
  return importInlineImage(app, launch, { mode });
};

// Switch to an existing project matching this image, without importing. Returns true when
// it switched (so the caller can stop). Shared by the resume launch path above and the
// extension's "resume in the open editor tab" nudge (stencil:switch-to-source), which lets
// the extension re-focus this tab instead of spawning a new one.
export const resumeBySource = (app, source, name) => {
  const baseName = stripExt(name || '');
  const matches = app.storage.store.findByImage(source, baseName);
  if (matches.length && app.switchToProject(matches[0].id)) {
    if (matches.length > 1) {
      notify(`Resumed "${shortName(matches[0].name)}" — ${matches.length} projects share this image`, 'ok');
      // This fires from the BOOT path (an external launch, before the very first
      // frame has necessarily painted) — a click landing before the toolbar has a
      // real, laid-out box sends the modal's icon-origin flight measuring a 0×0
      // rect, and it falls back to dropping in from above instead of the icon a
      // moment later shows as perfectly visible. One frame is enough to be sure.
      requestAnimationFrame(() => document.getElementById('projects-btn')?.click());
    }
    return true;
  }
  return false;
};

// Open a server project referenced by an external launch: connect to the server the
// way a user would from the connect modal (reuse the live connection, else a saved
// token, else mint one via POST /auth/token), then open the project — as an unlinked
// incognito copy when the launch asked for incognito, else as the normal linked open.
export const applyServerLaunch = async (app, launch) => {
  let url;
  try { url = normalizeUrl(launch.server.url); } catch { throw new Error('bad server URL'); }
  if (!app.connections.has(url)) {
    const saved = loadSavedServers().find(s => {
      try { return normalizeUrl(s.url) === url; } catch { return false; }
    });
    // A link can name ANY server — don't let a drive-by URL silently add a
    // (persisted) connection to an origin this browser has never used. Known
    // origins (live or saved) skip the prompt.
    if (!saved && !(await app.confirm(
      `This link opens a shared project on ${url}. Connect to that server?`,
      { title: 'Open shared project', confirmLabel: 'Connect', confirmIcon: 'link' }))) {
      return;
    }
    try {
      await app.connections.connect({ url, token: (saved && saved.token) || '' });
    } catch (err) {
      // The normal connect error path: surface the failure and open the connect modal
      // so the user can supply a token / fix the URL.
      notify(`Could not connect to ${url} — ${err.message}`, 'fail');
      document.getElementById('connect-btn')?.click();
      return;
    }
  }
  if (launch.incognito) {
    await app.copyServerProjectToIncognito({ serverUrl: url, id: launch.server.id }, {});
  } else {
    await app.openRemoteProject({ serverUrl: url, id: launch.server.id });
  }
};

// Apply a page size handed in by the external launch and reflect it in the UI.
// page.width/height are in cm (only used for the 'custom' size).
export const setExternalPage = (app, page) => {
  const size = normalizePageSize(page.size) || 'A3';
  app.pageSize = size;
  if (size === 'custom') {
    const w = parseFloat(page.width), h = parseFloat(page.height);
    if (!isNaN(w) && w > 0) app.customPageWidth = w;
    if (!isNaN(h) && h > 0) app.customPageHeight = h;
  }
  const sel = document.getElementById('page-size');
  if (sel) sel.value = size;
  revealControls(document.getElementById('custom-size-group'), size === 'custom');
  app.applyUnitToUI();   // refresh the custom width/height inputs in the active unit
  app.coordTable.update();
};
