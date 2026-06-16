// ── Context-menu definitions + click resolution (pure, unit-tested) ──────────
// One always-visible "Stencil" group on the 'all' context. It covers <img>/<svg>
// (Chrome fills info.srcUrl) and background-image elements (no native context, so
// the probe in ctxTarget.js records the URL under the cursor for us to use).
// Always-visible rather than toggled per right-click because in MV3 the worker is
// often asleep when the menu opens, so a contextMenus.update() loses the race. The
// trade-off: the group shows everywhere; on a spot with no image it's a no-op.
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

// Decide what a context-menu click should do. info.srcUrl (the real <img>) wins
// over `recordedUrl` (the background-image the probe saw). Returns null when the
// id isn't ours or there's no URL (e.g. invoked on a spot with no image).
export const resolveContextAction = (info = {}, recordedUrl = null) => {
  const spec = ACTIONS[info.menuItemId];
  if (!spec) return null;
  const src = info.srcUrl || recordedUrl || null;
  if (!src) return null;
  return spec.action === 'crop'
    ? { action: 'crop', src }
    : { action: 'open', src, incognito: spec.incognito };
};
