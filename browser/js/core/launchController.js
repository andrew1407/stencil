// External launch: `#stencil=<encodeURIComponent(JSON)>` and the extension bridge.
// Schema/precedence live in deepLink.js (normalizeLaunchPayload).
import { notify, shortName } from '../utils.js';
import { normalizeLaunchPayload, LAUNCH_DATA_URL_MAX } from './deepLink.js';
import { waitForImage } from './image/imageLoadFlow.js';
import { normalizePageSize } from './units.js';
import { normalizeUrl } from '../net/connectionManager.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { timeoutSignal } from '../net/abortable.js';
import { revealControls } from '../ui/motion.js';

export const stripExt = (name) => {
  const s = String(name || '');
  const dot = s.lastIndexOf('.');
  return dot > 0 ? s.slice(0, dot) : s;
};

// `mode`: 'new' = its own project (the only mode the naming options apply to); 'replace' /
// 'replace-keep' = swap the active project's image, dropping or keeping its lines.
export const importInlineImage = (app, launch, { mode = 'new' } = {}) => {
  const name = launch.name || 'image.png';
// Auto-numbered against same-source projects; incognito never persists, so it skips.
  const opts = launch.crop ? { crop: launch.crop } : {};
  if (!launch.crop && launch.noCrop) opts.noCrop = true;   // Open-Image dialog "Crop off" → full frame
  opts.source = launch.source;
  opts.resource = launch.resource;
  if (!app.storage.incognito && launch.source) opts.name = app.storage.store.copyName(stripExt(name), launch.source);
  if (launch.layout) {
    opts.layout = launch.layout;
    opts.adoptLayout = true;
  }

// `src` launches carry an http(s) image URL instead of inline bytes; the host must allow CORS.
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

// A .stc rides the fragment as a top-level `script`, OUTSIDE the normalized shape: the
// shared codec ignores unknown keys (pinned cross-surface), so desktop and bot stay put.
export const MAX_LAUNCH_SCRIPT = 200000;

const enterIncognito = (app) => {
  if (app.storage.incognito) return;
  app.storage.incognito = true;
  app.updateIncognitoUI();
};

const launchScript = (payload) => {
  const text = (payload && typeof payload === 'object' && typeof payload.script === 'string')
    ? payload.script : '';
  return text.length > 0 && text.length <= MAX_LAUNCH_SCRIPT ? text : '';
};

// Fragment (not query) keeps the payload off servers and logs; consumed once, then stripped.
export const applyExternalLaunch = (app) => {
  const hash = location.hash || '';
  const prefix = '#stencil=';
  if (!hash.startsWith(prefix)) return Promise.resolve();
// Stripped at once so a reload does not re-import.
  history.replaceState(null, '', location.pathname + location.search);

  let payload;
// Chrome caps fragments around 2M chars, lax environments don't: bound the raw hash
// before decode/parse (the cap normalizeLaunchPayload applies to the decoded dataUrl).
  if (hash.length > LAUNCH_DATA_URL_MAX) {
    notify('Stencil: could not read the shared image', 'fail');
    return Promise.resolve();
  }
  try {
    payload = JSON.parse(decodeURIComponent(hash.slice(prefix.length)));
  } catch {
    notify('Stencil: could not read the shared image', 'fail');
    return Promise.resolve();
  }
// Read before the image check: a script-only hand-off carries no picture at all, so it
// normalizes to nothing — and its incognito flag would go with it.
  app.pendingLaunchScript = launchScript(payload);
  if (payload && payload.incognito) enterIncognito(app);

  const launch = normalizeLaunchPayload(payload);
  if (!launch) return Promise.resolve();

// Page size BEFORE the load: the crop aspect and pixel↔page conversion must match the sender's.
  if (launch.page) setExternalPage(app, launch.page);

  if (launch.kind === 'server') {
    return applyServerLaunch(app, launch)
      .catch(err => notify(`Could not open the server project — ${err.message}`, 'fail'));
  }

  const name = launch.name || 'image.png';
  const source = launch.source;

// Resume: several matches open the projects list to pick; none falls through to an import.
  if (launch.open === 'resume' && !app.storage.incognito && (source || name)
      && resumeBySource(app, source, name)) {
    return Promise.resolve();
  }

  return importInlineImage(app, launch)
// The decode is asynchronous: a script sent with a picture must not run against the page behind it.
    .then(() => (app.pendingLaunchScript ? waitForImage(app) : undefined))
    .catch(() => notify('Stencil: failed to load the shared image', 'fail'));
};

// The extension bridge's entry: 'new' flushes and resets to a blank editor first
// (loadImageFromFile only promotes a TEMPORARY one) and applies the page size before the load.
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

// Switch to an existing project matching this image; true when it switched. Shared with the
// extension's "resume in the open editor tab" nudge (stencil:switch-to-source).
export const resumeBySource = (app, source, name) => {
  const baseName = stripExt(name || '');
  const matches = app.storage.store.findByImage(source, baseName);
  if (matches.length && app.switchToProject(matches[0].id)) {
    if (matches.length > 1) {
      notify(`Resumed "${shortName(matches[0].name)}" — ${matches.length} projects share this image`, 'ok');
// Fires from the boot path, before the toolbar has a laid-out box: a click now would send
// the modal's icon-origin flight measuring a 0×0 rect.
      requestAnimationFrame(() => document.getElementById('projects-btn')?.click());
    }
    return true;
  }
  return false;
};

// Connect the way the connect modal would (live connection, else saved token, else
// POST /auth/token), then open — an unlinked incognito copy when the launch asked for it.
export const applyServerLaunch = async (app, launch) => {
  let url;
  try { url = normalizeUrl(launch.server.url); } catch { throw new Error('bad server URL'); }
  if (!app.connections.has(url)) {
    const saved = loadSavedServers().find(s => {
      try { return normalizeUrl(s.url) === url; } catch { return false; }
    });
// A link can name ANY server: never silently persist a connection to an unknown origin.
    if (!saved && !(await app.confirm(
      `This link opens a shared project on ${url}. Connect to that server?`,
      { title: 'Open shared project', confirmLabel: 'Connect', confirmIcon: 'link' }))) {
      return;
    }
    try {
      await app.connections.connect({ url, token: (saved && saved.token) || '' });
    } catch (err) {
// The normal connect error path: toast, then the connect modal for a token / URL fix.
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
