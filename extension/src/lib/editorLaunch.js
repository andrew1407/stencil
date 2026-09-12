// The editor hand-off payload every surface sends, and the two launchers that open it.
import { mountStencilModal } from './overlay.js';
import { loadShellTheme } from './shellTheme.js';
import { recordOpened } from './ledger.js';
import { sourceOf } from './imageModel.js';
import { blobToDataUrl } from './imageData.js';
import { getSettings } from './settings.js';

export const buildLaunchUrl = (editorUrl, payload) => {
  const base = editorUrl.split('#')[0];
  return `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;
};

// { dataUrl, name, page:{size}, source, resource, incognito[, open] }. `open`
// ('resume'|'copy') is omitted when undefined so a plain open imports fresh.
export const buildHandoff = (image, { dataUrl, page, resource, incognito = false, open } = {}) => {
  const payload = {
    dataUrl,
    name: image.name,
    page: { size: page },
    // Shared (server) rows keep their own provenance; page images derive it.
    source: image.shared ? image.source : (image.source ?? sourceOf(image)),
    resource: image.shared ? image.resource : resource,
    incognito: !!incognito,
  };
  if (open !== undefined) payload.open = open;
  return payload;
};

// Past this URL length Chrome drops the navigation and the editor tab lands on about:blank.
export const MAX_PAYLOAD = 1_800_000;

const scaleRect = (r, k) => ({
  x: Math.round(r.x * k), y: Math.round(r.y * k),
  width: Math.max(1, Math.round(r.width * k)), height: Math.max(1, Math.round(r.height * k))
});

// Re-encodes payload.dataUrl smaller until the URL fits MAX_PAYLOAD; a crop rect
// (original-image pixels) is scaled by the same factor.
const fitLaunchPayload = async (editorUrl, payload) => {
  let url = buildLaunchUrl(editorUrl, payload);
  const data = payload.dataUrl;
  if (url.length <= MAX_PAYLOAD || typeof data !== 'string' || !data.startsWith('data:image')) return { payload, url };
  try {
    const bmp = await createImageBitmap(await (await fetch(data)).blob());
    for (let scale = 0.8; scale >= 0.12 && url.length > MAX_PAYLOAD; scale *= 0.8) {
      const w = Math.max(1, Math.round(bmp.width * scale)), h = Math.max(1, Math.round(bmp.height * scale));
      const c = new OffscreenCanvas(w, h);
      c.getContext('2d').drawImage(bmp, 0, 0, w, h);
      const next = { ...payload, dataUrl: await blobToDataUrl(await c.convertToBlob({ type: 'image/jpeg', quality: 0.85 })) };
      if (payload.crop) next.crop = scaleRect(payload.crop, w / bmp.width);
      const nextUrl = buildLaunchUrl(editorUrl, next);
      if (nextUrl.length < url.length) { payload = next; url = nextUrl; }
    }
  } catch {
    /* keep the original; the caller still warns */
  }
  return { payload, url };
};

const buildEditorLaunchUrl = async (payload) => {
  const { editorUrl } = await getSettings();
  const fitted = await fitLaunchPayload(editorUrl, payload);
  if (fitted.url.length > MAX_PAYLOAD)
    console.warn(`[stencil] launch URL is ${fitted.url.length} bytes — image may be too large.`);
  return fitted.url;
};

// Best-effort ledger entry; incognito launches are never persisted by the editor.
const noteOpened = async (payload) => {
  if (payload.incognito) return;
  const { editorUrl } = await getSettings();
  await recordOpened({ source: payload.source, resource: payload.resource, name: payload.name, editorUrl });
};

export const openEditorTab = async (payload) => {
  const tab = await chrome.tabs.create({ url: await buildEditorLaunchUrl(payload) });
  await noteOpened(payload);
  return tab;
};

// Falls back to a real tab when `tabId` is null or the modal can't be injected.
export const launchEditorModal = async ({ tabId, ...payload }) => {
  const url = await buildEditorLaunchUrl(payload);
  if (tabId == null) {
    const tab = await chrome.tabs.create({ url });
    await noteOpened(payload);
    return tab;
  }
  const title = payload.incognito ? 'Stencil editor (incognito)' : 'Stencil editor';
  try {
    await chrome.scripting.executeScript({
      // The shell can't read our CSS variables inside someone else's page, so the theme
      // travels as data (lib/shellTheme.js).
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, title, 8000, await loadShellTheme()]
    });
    await noteOpened(payload);
  } catch {
    return chrome.tabs.create({ url });
  }
};
