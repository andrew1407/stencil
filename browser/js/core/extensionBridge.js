import { normalizeLaunchPayload } from './deepLink.js';
import { scaledDataUrl } from '../utils.js';

// Editor-page side of "editor mode": the extension posts `stencil-ext-req` and we answer
// `stencil-ext-res`, always and only after validating — it is data, not an authority.
// Other half: extension/src/content/editorBridge.js.

// Mirror of extension/src/lib/messages.js (SRC.EXT_REQ / SRC.EXT_RES); the app can't import it.
const EXT_REQ = 'stencil-ext-req';
const EXT_RES = 'stencil-ext-res';

// Small on purpose: the thumbnail rides page → content script → service worker → panel on every refresh.
const THUMB_MAX = 256;

// 'new' = its own project (what `#stencil=` does); 'replace'/'replace-keep' swap the active image.
const IMPORT_MODES = ['new', 'replace', 'replace-keep'];

// Never throws: a blank editor, a tainted canvas and Node's missing canvas all give ''.
const canvasThumbnail = (canvas, max = THUMB_MAX) => {
  try {
    const w = canvas?.width || 0;
    const h = canvas?.height || 0;
    if (!w || !h) return '';
    return scaledDataUrl(canvas, w, h, max, 'image/jpeg', 0.72);
  } catch {
    return '';
  }
};

// The panel row's view of this tab.
const editorState = (app, { thumbnail = true, thumbMax } = {}) => {
// A caller-supplied size is DATA: clamped so a hostile value can't ask for a 32k-pixel data URL.
  const max = Math.min(2048, Math.max(64, Math.round(Number(thumbMax) || THUMB_MAX)));
  const store = app.storage?.store || null;
  const activeId = app.activeProjectId;
  const meta = (activeId != null && store) ? store.getMeta(activeId) : null;
  const img = app.image || null;
  const w = img ? (img.naturalWidth || img.width || 0) : 0;
  const h = img ? (img.naturalHeight || img.height || 0) : 0;
  return {
    projectId: activeId != null ? String(activeId) : '',
    projectName: (meta && meta.name) || '',
    hasImage: !!img,
    imageName: app.imageBaseName || '',
    imageSize: (w > 0 && h > 0) ? { w, h } : null,
// Never persisted — the panel warns before importing over it.
    incognito: !!app.storage?.incognito,
    thumbnail: thumbnail ? canvasThumbnail(app.canvas, max) : '',
    projects: (store ? store.list() : []).map(m => ({
      id: m.id, name: m.name || '', active: m.id === activeId,
    })),
  };
};

// The id is assigned synchronously but the NAME is written when the decode finishes.
const identity = (app) => {
  const id = app.activeProjectId;
  const meta = (id != null && app.storage?.store) ? app.storage.store.getMeta(id) : null;
  return { projectId: id != null ? String(id) : '', projectName: (meta && meta.name) || '' };
};

// Same payload and validation as `#stencil=`; a `server:{…}` hand-off is launch-time only.
const runImport = async (app, { handoff, mode } = {}) => {
  if (!IMPORT_MODES.includes(mode)) throw new Error('unknown import mode');
  const launch = normalizeLaunchPayload(handoff);
  if (!launch || launch.kind === 'server') throw new Error('the request carried no image');
  if (mode !== 'new' && !app.image) throw new Error('the editor holds no image to replace');
// Incognito only applies to a fresh import into a still-blank editor (the toolbar's rule).
  if (mode === 'new' && launch.incognito && !app.image && !app.storage.incognito) {
    app.storage.incognito = true;
    app.updateIncognitoUI();
  }
  await app.importExternalImage(launch, { mode });
  return identity(app);
};

// An unknown id is refused (switchToProject would leave the editor showing nothing).
const switchProject = (app, { projectId } = {}) => {
  const store = app.storage?.store || null;
  if (!projectId || !store || !store.list().some(m => m.id === projectId)) throw new Error('unknown project');
// false = already active: a fulfilled request, not a failure.
  const switched = app.switchToProject(projectId);
  return { ...identity(app), switched: !!switched };
};

// The toolbar button's exact path; used by the extension's drop zones after an import.
const openCropDialog = (app) => {
  if (!app.image) throw new Error('no image to crop');
  const btn = document.getElementById('crop-image');
  if (!btn) throw new Error('crop is unavailable');
  btn.click();
  return identity(app);
};

const handleRequest = (app, request, payload) => {
  switch (request) {
    case 'state': return editorState(app, payload);
    case 'import': return runImport(app, payload);
    case 'switch': return switchProject(app, payload);
    case 'crop': return openCropDialog(app);
// Answered, not dropped: a newer extension on an older editor reads this, not a timeout.
    default: throw new Error('unknown request');
  }
};

// Same-window messages only; replies are id-correlated (the panel keeps several in flight).
// `target` is injectable for tests (no window = no-op).
export const wireExtensionBridge = (app, target = (typeof window !== 'undefined' ? window : null)) => {
  if (!target || typeof target.addEventListener !== 'function') return;
  target.addEventListener('message', (e) => {
    if (e.source !== target) return;
    const req = e.data;
    if (!req || req.source !== EXT_REQ || req.id == null) return;
    const reply = (body) => {
      try { target.postMessage({ source: EXT_RES, id: req.id, ...body }, '*'); } catch { /* window gone */ }
    };
// One chain, so a sync throw and a rejected import both give exactly one error reply.
    Promise.resolve()
      .then(() => handleRequest(app, req.request, req.payload || {}))
      .then(result => reply({ ok: true, result }))
      .catch(err => reply({ ok: false, error: (err && err.message) || 'the request failed' }));
  });
};
