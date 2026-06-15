// ── Context-menu definitions + click resolution (pure, unit-tested) ──────────
// One "Stencil" group on the 'all' context, always visible. It covers both:
//   • <img> / <svg><image> — Chrome fills info.srcUrl, used directly.
//   • background-image elements — there's no native context and info.srcUrl is
//     empty, so we use the URL the content-script probe recorded for the element
//     under the cursor (src/content/ctxTarget.js).
// Why always visible (rather than toggling visibility per right-click): in MV3
// the service worker is frequently asleep when the menu opens, so a
// contextMenus.update() fired from the probe message loses the race and the item
// never shows. Recording the URL, by contrast, is reliable — the click happens
// long after the probe ran. The trade-off is that "Stencil" shows on every
// right-click; on a spot with no image the actions resolve to nothing (no-op).
export const MENU = {
  parent: 'stencil-parent',
  open: 'stencil-open',
  openIncognito: 'stencil-open-incognito',
  crop: 'stencil-crop'
};

// Action item id → what clicking it does.
const ACTIONS = {
  [MENU.open]: { action: 'open', incognito: false },
  [MENU.openIncognito]: { action: 'open', incognito: true },
  [MENU.crop]: { action: 'crop' }
};

const CONTEXTS = ['all'];

// Flat list passed straight to chrome.contextMenus.create (in order).
export const MENU_ITEMS = [
  { id: MENU.parent, title: 'Stencil', contexts: CONTEXTS },
  { id: MENU.open, parentId: MENU.parent, title: '✎ Open image in Stencil editor', contexts: CONTEXTS },
  { id: MENU.openIncognito, parentId: MENU.parent, title: '🕶 Open in Stencil (incognito)', contexts: CONTEXTS },
  { id: MENU.crop, parentId: MENU.parent, title: '✂ Crop image in Stencil…', contexts: CONTEXTS }
];

// Decide what a context-menu click should do. `info` is the onClicked payload;
// `recordedUrl` is the image URL the content-script probe last saw under the
// cursor (a background-image, used when info.srcUrl is empty). info.srcUrl wins
// (it's the real <img>). Returns null when the id isn't ours or there's no URL —
// e.g. the user invoked Stencil on a spot with no image.
export const resolveContextAction = (info = {}, recordedUrl = null) => {
  const spec = ACTIONS[info.menuItemId];
  if (!spec) return null;
  const src = info.srcUrl || recordedUrl || null;
  if (!src) return null;
  return spec.action === 'crop'
    ? { action: 'crop', src }
    : { action: 'open', src, incognito: spec.incognito };
};
