import { fetchAsDataUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff, resumeInOpenEditor } from '../lib/stencil.js';
import { sourceOf, editableSrc } from '../lib/image/imageModel.js';
import { rasterizeToPngDataUrl, isSvgType, isSvgUrl, mediaTypeOf } from '../lib/image/rasterize.js';
import { buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink, INLINE_WARN_CHARS, INLINE_MAX_CHARS } from '../lib/menu/openIn.js';
import { statusEl, dismiss } from './panelDom.js';
import { state, rowResource, surfaceTabId } from './model.js';
import { sharedDataUrl } from './sharedPins.js';
import { editorMode } from './editorHandle.js';

// An SVG is rasterised first: raw markup has no pixels to hand the editor.
export const imageDataUrl = async (image) => {
  if (image.shared) return sharedDataUrl(image);
  const src = editableSrc(image);
  const dataUrl = await fetchAsDataUrl(src, { pageUrl: rowResource(image) });
  if (!isSvgType(mediaTypeOf(dataUrl)) && !isSvgUrl(src)) return dataUrl;
  return rasterizeToPngDataUrl({ dataUrl, width: image.w || 0, height: image.h || 0 });
};

// A custom scheme goes through an in-document anchor click: chrome.tabs.create on it
// leaves a dead blank tab (mirrors browser/js/ui/modal/openInModal.js).
const openExternalUrl = (url) => {
  if (/^https?:/i.test(url)) { chrome.tabs.create({ url }); return; }
  const a = document.createElement('a');
  a.href = url;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  a.remove();
};

// A shared row sends only its server reference: no token in the link.
export const openInDesktop = async (image) => {
  // Read synchronously from the cached config: a stencil:// launch needs the click's
  // transient user activation, which an `await getSettings()` would spend.
  const desktopScheme = state.openIn && state.openIn.desktopScheme;
  if (!desktopScheme) { statusEl.textContent = 'No desktop app scheme configured (set one in Options).'; return; }
  let url, warn = '';
  if (image.shared && image.serverUrl && image.projectId) {
    url = buildStencilSchemeUrl({ scheme: desktopScheme, server: image.serverUrl, id: image.projectId });
  } else {
    statusEl.textContent = 'Loading image…';
    const dataUrl = await imageDataUrl(image);
    url = buildStencilSchemeUrl({ scheme: desktopScheme, src: dataUrl });
    if (url.length > INLINE_MAX_CHARS) {
      statusEl.textContent = 'Image too large to hand off inline — save it to a server and share the server project instead.';
      return;
    }
    if (url.length > INLINE_WARN_CHARS) warn = ' (large image — if it doesn’t open, save it to a server instead)';
  }
  openExternalUrl(url);
  // No dismiss() here: window.close() would destroy the document before Chrome acts on
  // the stencil:// anchor navigation.
  statusEl.textContent = `Opening in the desktop app…${warn}`;
};

// Shared rows only: a 64-char ?start= payload cannot carry bytes.
export const openInTelegram = (image) => {
  const telegramBotUsername = state.openIn && state.openIn.telegramBotUsername;
  if (!telegramBotUsername || !image.shared || !image.serverUrl || !image.projectId) return;
  const payload = encodeTelegramStartPayload(image.serverUrl, image.projectId);
  if (!payload) {
    statusEl.textContent = 'The server address is too long for a Telegram link — open the bot and use /connect + /fetch.';
    return;
  }
  openExternalUrl(buildTelegramLink(telegramBotUsername, payload));
  dismiss();
};

export const sendToEditor = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await openEditorTab(buildHandoff(image, { dataUrl, page, resource: rowResource(image), incognito, open }));
  dismiss();
};

// Falls back to the new-tab resume when no open editor tab answers.
export const resumeInEditor = async (image) => {
  if (await resumeInOpenEditor({ source: sourceOf(image), name: image.name })) {
    dismiss();
    return;
  }
  await sendToEditor(image, false, 'resume');
};

const sendToEditorModal = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await launchEditorModal({ ...buildHandoff(image, { dataUrl, page, resource: rowResource(image), incognito, open }), tabId: surfaceTabId() });
  dismiss();
};

// `anchor` pins the occupied-editor chooser next to the asking control.
export const openHere = (image, incognito, open, anchor) => (state.mode === 'editor'
  ? editorMode.importHere(image, { incognito, anchor })
  : sendToEditorModal(image, incognito, open));

// The modal opens on the page in FRONT of the user: in editor mode the editor tab.
export const openCrop = async (image) => {
  const src = image.shared ? await sharedDataUrl(image) : editableSrc(image);
  const { source, resource } = buildHandoff(image, { resource: rowResource(image) });
  await launchCrop({ src, source, resource, tabId: surfaceTabId() });
  dismiss();
};
