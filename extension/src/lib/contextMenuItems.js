// Context-menu ids, contexts and the flat create() list. NATIVE items (image/video
// contexts) are static and work while the MV3 worker sleeps; DYNAMIC ones ('all') start
// hidden and the ctxTarget probe reveals them. TWO ROOTS is load-bearing: Chrome decides a
// PARENT's visibility from its own `contexts`, never from its children, so one 'all' parent
// would paint "Stencil ▸ (empty)" on plain elements.
export const MENU = Object.freeze({
  ROOT: 'stencil-root',
  BG_ROOT: 'stencil-bg-root',
  ACTION_OPEN: 'stencil-action-open',
  ACTION_OPEN_INCOGNITO: 'stencil-action-open-incognito',
  OPEN_PARENT: 'stencil-open-parent',
  FRAME_OPEN_PARENT: 'stencil-frame-open-parent',
  BG_OPEN_PARENT: 'stencil-bg-open-parent',
  OPEN: 'stencil-open',
  OPEN_RESUME: 'stencil-open-resume',
  OPEN_INCOGNITO: 'stencil-open-incognito',
  OPEN_MODAL: 'stencil-open-modal',
  OPEN_MODAL_INCOGNITO: 'stencil-open-modal-incognito',
  CROP: 'stencil-crop',
  DESKTOP: 'stencil-desktop',
  PIN: 'stencil-pin',
  FRAME_OPEN: 'stencil-frame-open',
  FRAME_OPEN_INCOGNITO: 'stencil-frame-open-incognito',
  FRAME_MODAL: 'stencil-frame-modal',
  FRAME_MODAL_INCOGNITO: 'stencil-frame-modal-incognito',
  FRAME_CROP: 'stencil-frame-crop',
  FRAME_DESKTOP: 'stencil-frame-desktop',
  FRAME_PIN: 'stencil-frame-pin',
  PREVIEW_PARENT: 'stencil-preview',
  PREVIEW_TAB: 'stencil-preview-tab',
  PREVIEW_OPEN: 'stencil-preview-open',
  PREVIEW_OPEN_INCOGNITO: 'stencil-preview-open-incognito',
  PREVIEW_MODAL: 'stencil-preview-modal',
  PREVIEW_MODAL_INCOGNITO: 'stencil-preview-modal-incognito',
  PREVIEW_CROP: 'stencil-preview-crop',
  BG_OPEN: 'stencil-bg-open',
  BG_OPEN_RESUME: 'stencil-bg-open-resume',
  BG_OPEN_INCOGNITO: 'stencil-bg-open-incognito',
  BG_OPEN_MODAL: 'stencil-bg-open-modal',
  BG_OPEN_MODAL_INCOGNITO: 'stencil-bg-open-modal-incognito',
  BG_CROP: 'stencil-bg-crop',
  BG_DESKTOP: 'stencil-bg-desktop',
  BG_PIN: 'stencil-bg-pin'
});

const ACTION_CONTEXTS = ['action'];
const IMAGE_CONTEXTS = ['image'];
const VIDEO_CONTEXTS = ['video'];
// The static root's contexts are exactly the union its children serve — NOT 'all' — so
// Chrome hides the whole entry on a plain right-click instead of drawing an empty submenu.
const STATIC_CONTEXTS = [...ACTION_CONTEXTS, ...IMAGE_CONTEXTS, ...VIDEO_CONTEXTS];
// Each 'all' item carries its own visible:false; the worker flips them together on a probe hit.
const ALL_CONTEXTS = ['all'];

// Passed straight to chrome.contextMenus.create, in order; the explicit "Stencil" parent
// comes first so the submenu reads "Stencil", not the auto-grouped extension name.
export const MENU_ITEMS = Object.freeze([
  { id: MENU.ROOT, title: 'Stencil', contexts: STATIC_CONTEXTS },
  { id: MENU.ACTION_OPEN, parentId: MENU.ROOT, title: '✎ Open Stencil editor', contexts: ACTION_CONTEXTS },
  { id: MENU.ACTION_OPEN_INCOGNITO, parentId: MENU.ROOT, title: '🕶 Open Stencil editor (incognito)', contexts: ACTION_CONTEXTS },
  { id: MENU.OPEN_PARENT, parentId: MENU.ROOT, title: '✎ Open in editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.OPEN, parentId: MENU.OPEN_PARENT, title: '↗ New tab', contexts: IMAGE_CONTEXTS },
  { id: MENU.OPEN_RESUME, parentId: MENU.OPEN_PARENT, title: '↩ Resume existing editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.OPEN_INCOGNITO, parentId: MENU.OPEN_PARENT, title: '🕶 New tab (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.OPEN_MODAL, parentId: MENU.OPEN_PARENT, title: '▣ Here', contexts: IMAGE_CONTEXTS },
  { id: MENU.OPEN_MODAL_INCOGNITO, parentId: MENU.OPEN_PARENT, title: '▣ Here (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.CROP, parentId: MENU.ROOT, title: '✂ Crop image…', contexts: IMAGE_CONTEXTS },
  // Revealed only when a desktop URL scheme is configured (syncDesktopMenuVisibility).
  { id: MENU.DESKTOP, parentId: MENU.ROOT, title: '🖥 Open in desktop app', contexts: IMAGE_CONTEXTS, visible: false },
  { id: MENU.PIN, parentId: MENU.ROOT, title: '📌 Pin image', contexts: IMAGE_CONTEXTS },
  { id: MENU.FRAME_OPEN_PARENT, parentId: MENU.ROOT, title: '✎ Open current frame', contexts: VIDEO_CONTEXTS },
  { id: MENU.FRAME_OPEN, parentId: MENU.FRAME_OPEN_PARENT, title: '↗ New tab', contexts: VIDEO_CONTEXTS },
  { id: MENU.FRAME_OPEN_INCOGNITO, parentId: MENU.FRAME_OPEN_PARENT, title: '🕶 New tab (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.FRAME_MODAL, parentId: MENU.FRAME_OPEN_PARENT, title: '▣ Here', contexts: VIDEO_CONTEXTS },
  { id: MENU.FRAME_MODAL_INCOGNITO, parentId: MENU.FRAME_OPEN_PARENT, title: '▣ Here (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.FRAME_CROP, parentId: MENU.ROOT, title: '✂ Crop current frame…', contexts: VIDEO_CONTEXTS },
  { id: MENU.FRAME_DESKTOP, parentId: MENU.ROOT, title: '🖥 Open frame in desktop app', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.FRAME_PIN, parentId: MENU.ROOT, title: '📌 Pin video', contexts: VIDEO_CONTEXTS },
  // Revealed (PREVIEW_ITEMS) only when the probed video actually has a poster.
  { id: MENU.PREVIEW_PARENT, parentId: MENU.ROOT, title: 'Video preview image', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.PREVIEW_TAB, parentId: MENU.PREVIEW_PARENT, title: '↗ Open preview in a new tab', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.PREVIEW_OPEN, parentId: MENU.PREVIEW_PARENT, title: '✎ Open preview in editor', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.PREVIEW_OPEN_INCOGNITO, parentId: MENU.PREVIEW_PARENT, title: '🕶 Preview in editor (incognito)', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.PREVIEW_MODAL, parentId: MENU.PREVIEW_PARENT, title: '▣ Open preview in editor here', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.PREVIEW_MODAL_INCOGNITO, parentId: MENU.PREVIEW_PARENT, title: '▣ Preview here (incognito)', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.PREVIEW_CROP, parentId: MENU.PREVIEW_PARENT, title: '✂ Crop preview…', contexts: VIDEO_CONTEXTS, visible: false },
  // The dynamic group under its OWN root, revealed TOGETHER (root included) on a probe hit.
  { id: MENU.BG_ROOT, title: 'Stencil', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_OPEN_PARENT, parentId: MENU.BG_ROOT, title: '✎ Open in editor', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_OPEN, parentId: MENU.BG_OPEN_PARENT, title: '↗ New tab', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_OPEN_RESUME, parentId: MENU.BG_OPEN_PARENT, title: '↩ Resume existing editor', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_OPEN_INCOGNITO, parentId: MENU.BG_OPEN_PARENT, title: '🕶 New tab (incognito)', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_OPEN_MODAL, parentId: MENU.BG_OPEN_PARENT, title: '▣ Here', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_OPEN_MODAL_INCOGNITO, parentId: MENU.BG_OPEN_PARENT, title: '▣ Here (incognito)', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_CROP, parentId: MENU.BG_ROOT, title: '✂ Crop image…', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_DESKTOP, parentId: MENU.BG_ROOT, title: '🖥 Open in desktop app', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.BG_PIN, parentId: MENU.BG_ROOT, title: '📌 Pin image', contexts: ALL_CONTEXTS, visible: false }
]);

// Revealed/hidden together — the group's ROOT first, so it never renders without children.
export const DYNAMIC_ITEMS = Object.freeze([
  MENU.BG_ROOT, MENU.BG_OPEN_PARENT,
  MENU.BG_OPEN, MENU.BG_OPEN_RESUME, MENU.BG_OPEN_INCOGNITO,
  MENU.BG_OPEN_MODAL, MENU.BG_OPEN_MODAL_INCOGNITO, MENU.BG_CROP, MENU.BG_PIN
]);

// Toggle on the desktop scheme alone; MENU.BG_DESKTOP ALSO needs the probe's reveal.
export const STATIC_DESKTOP_ITEMS = Object.freeze([MENU.DESKTOP, MENU.FRAME_DESKTOP]);

// Handled directly in the SW: they toggle the pinned state of the URL under the cursor.
export const PIN_ITEMS = Object.freeze([MENU.PIN, MENU.FRAME_PIN, MENU.BG_PIN]);

// The MENU_ITEMS default titles are the unpinned form of this.
export const pinItemTitle = (pinned, kind = 'image') =>
  `📌 ${pinned ? 'Unpin' : 'Pin'} ${kind === 'video' ? 'video' : 'image'}`;

// Revealed/hidden together, only when the probed <video> carries a poster.
export const PREVIEW_ITEMS = Object.freeze([
  MENU.PREVIEW_PARENT, MENU.PREVIEW_TAB, MENU.PREVIEW_OPEN, MENU.PREVIEW_OPEN_INCOGNITO,
  MENU.PREVIEW_MODAL, MENU.PREVIEW_MODAL_INCOGNITO, MENU.PREVIEW_CROP
]);
