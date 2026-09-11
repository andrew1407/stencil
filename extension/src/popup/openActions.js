import { fetchAsDataUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff, resumeInOpenEditor } from '../lib/stencil.js';
import { sourceOf, editableSrc } from '../lib/imageModel.js';
import { rasterizeToPngDataUrl, isSvgType, isSvgUrl, mediaTypeOf } from '../lib/rasterize.js';
import { buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink, INLINE_WARN_CHARS, INLINE_MAX_CHARS } from '../lib/openIn.js';
import { statusEl, dismiss } from './panelDom.js';
import { state, rowResource, surfaceTabId } from './model.js';
import { sharedDataUrl } from './sharedPins.js';
import { editorMode } from './editorHandle.js';

// The image bytes to hand to the editor / crop: a shared row pulls them (authed) from its
// server, a page image through the extension's host permissions. An SVG is RASTERISED
// first (lib/rasterize.js) — raw markup has no pixels to hand the editor.
export const imageDataUrl = async (image) => {
  if (image.shared) return sharedDataUrl(image);
  const src = editableSrc(image);
  const dataUrl = await fetchAsDataUrl(src, { pageUrl: rowResource(image) });
  if (!isSvgType(mediaTypeOf(dataUrl)) && !isSvgUrl(src)) return dataUrl;
  return rasterizeToPngDataUrl({ dataUrl, width: image.w || 0, height: image.h || 0 });
};

// Hand a URL to the OS / browser from a user gesture. A CUSTOM scheme (stencil://) goes
// through a transient IN-DOCUMENT anchor click — chrome.tabs.create on it leaves a dead
// blank tab (mirrors browser/js/ui/openInModal.js); http(s) opens as a normal new tab.
const openExternalUrl = (url) => {
  if (/^https?:/i.test(url)) { chrome.tabs.create({ url }); return; }
  const a = document.createElement('a');
  a.href = url;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  a.remove();
};

// "Open in… ▸ Desktop app": a `stencil://open?…` link the OS routes to the desktop app.
// A shared row sends only its server reference (no token in the link); any other row embeds
// its bytes inline, refusing absurdly large payloads (same guards as the browser's modal).
export const openInDesktop = async (image) => {
  // Read the scheme SYNCHRONOUSLY from the cached config: a stencil:// launch needs the
  // click's transient user activation, which an `await getSettings()` would spend.
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
  // Do NOT dismiss() here: window.close() would destroy the document before Chrome acts on
  // the stencil:// anchor navigation — nothing would open. The popup closes on its own when
  // the OS "Open Stencil?" prompt takes focus.
  statusEl.textContent = `Opening in the desktop app…${warn}`;
};

// "Open in… ▸ Telegram bot": a t.me deep link carrying (server, project id) in the 64-char
// ?start= payload — shared rows only (a start payload can't carry bytes); an overflowing
// host points at /connect + /fetch. Only targets the user-connected host of the row.
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

// Resume an already-opened image: jump to the editor tab that's ALREADY open (focus it +
// switch to the matching project, no new tab / no reload). Falls back to the classic new-tab
// resume when no editor tab is open (or its bridge didn't answer).
export const resumeInEditor = async (image) => {
  if (await resumeInOpenEditor({ source: sourceOf(image), name: image.name })) {
    dismiss();
    return;
  }
  await sendToEditor(image, false, 'resume');
};

// Same as sendToEditor, but frames the editor in an in-page modal on the active
// page instead of opening a new tab (mirrors the quick-crop modal).
const sendToEditorModal = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await launchEditorModal({ ...buildHandoff(image, { dataUrl, page, resource: rowResource(image), incognito, open }), tabId: surfaceTabId() });
  dismiss();
};

// The "open it where I'm looking" action: the in-page modal normally, an import INTO the
// editor tab in editor mode. `anchor` (the asking control) pins the occupied-editor
// chooser next to it; only editor mode uses it.
export const openHere = (image, incognito, open, anchor) => (state.mode === 'editor'
  ? editorMode.importHere(image, { incognito, anchor })
  : sendToEditorModal(image, incognito, open));

// Crop opens its in-page modal on the page in FRONT of the user — in editor mode the editor
// tab, not the source page being listed, which the user would never see it on.
export const openCrop = async (image) => {
  const src = image.shared ? await sharedDataUrl(image) : editableSrc(image);
  const { source, resource } = buildHandoff(image, { resource: rowResource(image) });
  await launchCrop({ src, source, resource, tabId: surfaceTabId() });
  dismiss();
};
