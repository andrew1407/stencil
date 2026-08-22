import { normalizeLaunchPayload } from './deepLink.js';
import { scaledDataUrl } from '../utils.js';

// ── Extension bridge (editor-page side of "editor mode") ─────────
// The extension posts `stencil-ext-req` on the page's bus (list / switch project / import) and
// we answer `stencil-ext-res` — always, and only after validating: it's data, not an authority.
// Routes into existing core methods only. Other half: extension/src/content/editorBridge.js.

// mirror of extension/src/lib/messages.js (SRC.EXT_REQ / SRC.EXT_RES) — the app ships
// separately from the extension and can't import it. Keep in sync.
const EXT_REQ = 'stencil-ext-req';
const EXT_RES = 'stencil-ext-res';

// Long edge (px) of the preview each editor row shows. Small on purpose: the thumbnail is a
// data: URL that travels page → content script → service worker → panel on every refresh.
const THUMB_MAX = 256;

// DrawingApp's two loader paths: 'new' = its own project (what `#stencil=` does),
// 'replace'/'replace-keep' = swap the active project's image, dropping or keeping its lines.
const IMPORT_MODES = ['new', 'replace', 'replace-keep'];

// The live #canvas (unsaved edits included) as a small data: URL, '' when there's nothing.
// Never throws: a blank editor, a tainted canvas and Node's missing canvas all give ''.
// `max` is the long edge: the row previews ask for THUMB_MAX, a hover magnifier for more.
const canvasThumbnail = (canvas, max = THUMB_MAX) => {
  try {
    const w = canvas?.width || 0;
    const h = canvas?.height || 0;
    if (!w || !h) return '';
    // JPEG, not PNG: this rides three message hops on a poll.
    return scaledDataUrl(canvas, w, h, max, 'image/jpeg', 0.72);
  } catch {
    return '';
  }
};

// The panel row's view of this tab: active project, whether an image is loaded (which decides
// if an import must ask first), and the project list its ⋯ menu offers.
const editorState = (app, { thumbnail = true, thumbMax } = {}) => {
  // A caller-supplied size is DATA: clamped to something a canvas can actually encode and a
  // message can carry, so a bad (or hostile) value can't ask for a 32k-pixel data URL.
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
    // An incognito session is never persisted — the panel warns before importing over it.
    incognito: !!app.storage?.incognito,
    thumbnail: thumbnail ? canvasThumbnail(app.canvas, max) : '',
    projects: (store ? store.list() : []).map(m => ({
      id: m.id, name: m.name || '', active: m.id === activeId,
    })),
  };
};

// Identity reported after an import/switch. The id is assigned synchronously but the NAME is
// written when the decode finishes, so a new project may answer with its old name for a tick.
const identity = (app) => {
  const id = app.activeProjectId;
  const meta = (id != null && app.storage?.store) ? app.storage.store.getMeta(id) : null;
  return { projectId: id != null ? String(id) : '', projectName: (meta && meta.name) || '' };
};

// Import one hand-off into THIS tab — same payload and validation as the `#stencil=` fragment.
// A `server:{…}` hand-off is refused: that's a launch-time flow, not an import into a live tab.
const runImport = async (app, { handoff, mode } = {}) => {
  if (!IMPORT_MODES.includes(mode)) throw new Error('unknown import mode');
  const launch = normalizeLaunchPayload(handoff);
  if (!launch || launch.kind === 'server') throw new Error('the request carried no image');
  if (mode !== 'new' && !app.image) throw new Error('the editor holds no image to replace');
  // Incognito only applies to a fresh import into a still-blank editor (the toolbar's rule),
  // so an import can't quietly stop an editor holding work from saving it.
  if (mode === 'new' && launch.incognito && !app.image && !app.storage.incognito) {
    app.storage.incognito = true;
    app.updateIncognitoUI();
  }
  await app.importExternalImage(launch, { mode });
  return identity(app);
};

// Switch this tab to one of its own projects; an unknown id is refused rather than passed on
// (switchToProject would leave the editor showing nothing).
const switchProject = (app, { projectId } = {}) => {
  const store = app.storage?.store || null;
  if (!projectId || !store || !store.list().some(m => m.id === projectId)) throw new Error('unknown project');
  // false = it was already the active project; that's a fulfilled request, not a failure.
  const switched = app.switchToProject(projectId);
  return { ...identity(app), switched: !!switched };
};

// 'crop': open THIS editor's own crop dialog (the toolbar button's exact path),
// used by the extension's drop zones after importing the dragged image here.
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

/**
 * Listen for the extension's editor-mode requests on this page and answer them. Same-window
 * messages only; replies are id-correlated (the panel keeps several requests in flight).
 * @param {object} app - The live DrawingApp.
 * @param {object} [target] - The window to listen on; injectable for tests (no window = no-op).
 */
export const wireExtensionBridge = (app, target = (typeof window !== 'undefined' ? window : null)) => {
  if (!target || typeof target.addEventListener !== 'function') return;
  target.addEventListener('message', (e) => {
    if (e.source !== target) return;
    const req = e.data;
    if (!req || req.source !== EXT_REQ || req.id == null) return;
    const reply = (body) => {
      try { target.postMessage({ source: EXT_RES, id: req.id, ...body }, '*'); } catch { /* window gone */ }
    };
    // One chain so a sync throw and a rejected import both give exactly one error reply.
    Promise.resolve()
      .then(() => handleRequest(app, req.request, req.payload || {}))
      .then(result => reply({ ok: true, result }))
      .catch(err => reply({ ok: false, error: (err && err.message) || 'the request failed' }));
  });
};
