// Context-menu ids, contexts and the flat create() list. NATIVE items (image/video
// contexts) are static and work while the MV3 worker sleeps; DYNAMIC ones ('all') start
// hidden and the ctxTarget probe reveals them. TWO ROOTS is load-bearing: Chrome decides a
// PARENT's visibility from its own `contexts`, never from its children, so one 'all' parent
// would paint "Stencil ▸ (empty)" on plain elements.
export const MENU = Object.freeze({
  root: 'stencil-root',
  bgRoot: 'stencil-bg-root',
  actionOpen: 'stencil-action-open',
  actionOpenIncognito: 'stencil-action-open-incognito',
  openParent: 'stencil-open-parent',
  frameOpenParent: 'stencil-frame-open-parent',
  bgOpenParent: 'stencil-bg-open-parent',
  open: 'stencil-open',
  openResume: 'stencil-open-resume',
  openIncognito: 'stencil-open-incognito',
  openModal: 'stencil-open-modal',
  openModalIncognito: 'stencil-open-modal-incognito',
  crop: 'stencil-crop',
  desktop: 'stencil-desktop',
  pin: 'stencil-pin',
  frameOpen: 'stencil-frame-open',
  frameOpenIncognito: 'stencil-frame-open-incognito',
  frameModal: 'stencil-frame-modal',
  frameModalIncognito: 'stencil-frame-modal-incognito',
  frameCrop: 'stencil-frame-crop',
  frameDesktop: 'stencil-frame-desktop',
  framePin: 'stencil-frame-pin',
  previewParent: 'stencil-preview',
  previewTab: 'stencil-preview-tab',
  previewOpen: 'stencil-preview-open',
  previewOpenIncognito: 'stencil-preview-open-incognito',
  previewModal: 'stencil-preview-modal',
  previewModalIncognito: 'stencil-preview-modal-incognito',
  previewCrop: 'stencil-preview-crop',
  bgOpen: 'stencil-bg-open',
  bgOpenResume: 'stencil-bg-open-resume',
  bgOpenIncognito: 'stencil-bg-open-incognito',
  bgOpenModal: 'stencil-bg-open-modal',
  bgOpenModalIncognito: 'stencil-bg-open-modal-incognito',
  bgCrop: 'stencil-bg-crop',
  bgDesktop: 'stencil-bg-desktop',
  bgPin: 'stencil-bg-pin'
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
  { id: MENU.root, title: 'Stencil', contexts: STATIC_CONTEXTS },
  { id: MENU.actionOpen, parentId: MENU.root, title: '✎ Open Stencil editor', contexts: ACTION_CONTEXTS },
  { id: MENU.actionOpenIncognito, parentId: MENU.root, title: '🕶 Open Stencil editor (incognito)', contexts: ACTION_CONTEXTS },
  { id: MENU.openParent, parentId: MENU.root, title: '✎ Open in editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.open, parentId: MENU.openParent, title: '↗ New tab', contexts: IMAGE_CONTEXTS },
  { id: MENU.openResume, parentId: MENU.openParent, title: '↩ Resume existing editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.openIncognito, parentId: MENU.openParent, title: '🕶 New tab (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.openModal, parentId: MENU.openParent, title: '▣ Here', contexts: IMAGE_CONTEXTS },
  { id: MENU.openModalIncognito, parentId: MENU.openParent, title: '▣ Here (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.crop, parentId: MENU.root, title: '✂ Crop image…', contexts: IMAGE_CONTEXTS },
  // Revealed only when a desktop URL scheme is configured (syncDesktopMenuVisibility).
  { id: MENU.desktop, parentId: MENU.root, title: '🖥 Open in desktop app', contexts: IMAGE_CONTEXTS, visible: false },
  { id: MENU.pin, parentId: MENU.root, title: '📌 Pin image', contexts: IMAGE_CONTEXTS },
  { id: MENU.frameOpenParent, parentId: MENU.root, title: '✎ Open current frame', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameOpen, parentId: MENU.frameOpenParent, title: '↗ New tab', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameOpenIncognito, parentId: MENU.frameOpenParent, title: '🕶 New tab (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameModal, parentId: MENU.frameOpenParent, title: '▣ Here', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameModalIncognito, parentId: MENU.frameOpenParent, title: '▣ Here (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameCrop, parentId: MENU.root, title: '✂ Crop current frame…', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameDesktop, parentId: MENU.root, title: '🖥 Open frame in desktop app', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.framePin, parentId: MENU.root, title: '📌 Pin video', contexts: VIDEO_CONTEXTS },
  // Revealed (PREVIEW_ITEMS) only when the probed video actually has a poster.
  { id: MENU.previewParent, parentId: MENU.root, title: 'Video preview image', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewTab, parentId: MENU.previewParent, title: '↗ Open preview in a new tab', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewOpen, parentId: MENU.previewParent, title: '✎ Open preview in editor', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewOpenIncognito, parentId: MENU.previewParent, title: '🕶 Preview in editor (incognito)', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewModal, parentId: MENU.previewParent, title: '▣ Open preview in editor here', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewModalIncognito, parentId: MENU.previewParent, title: '▣ Preview here (incognito)', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewCrop, parentId: MENU.previewParent, title: '✂ Crop preview…', contexts: VIDEO_CONTEXTS, visible: false },
  // The dynamic group under its OWN root, revealed TOGETHER (root included) on a probe hit.
  { id: MENU.bgRoot, title: 'Stencil', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenParent, parentId: MENU.bgRoot, title: '✎ Open in editor', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpen, parentId: MENU.bgOpenParent, title: '↗ New tab', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenResume, parentId: MENU.bgOpenParent, title: '↩ Resume existing editor', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenIncognito, parentId: MENU.bgOpenParent, title: '🕶 New tab (incognito)', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenModal, parentId: MENU.bgOpenParent, title: '▣ Here', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenModalIncognito, parentId: MENU.bgOpenParent, title: '▣ Here (incognito)', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgCrop, parentId: MENU.bgRoot, title: '✂ Crop image…', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgDesktop, parentId: MENU.bgRoot, title: '🖥 Open in desktop app', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgPin, parentId: MENU.bgRoot, title: '📌 Pin image', contexts: ALL_CONTEXTS, visible: false }
]);

// Revealed/hidden together — the group's ROOT first, so it never renders without children.
export const DYNAMIC_ITEMS = Object.freeze([
  MENU.bgRoot, MENU.bgOpenParent,
  MENU.bgOpen, MENU.bgOpenResume, MENU.bgOpenIncognito,
  MENU.bgOpenModal, MENU.bgOpenModalIncognito, MENU.bgCrop, MENU.bgPin
]);

// Toggle on the desktop scheme alone; MENU.bgDesktop ALSO needs the probe's reveal.
export const STATIC_DESKTOP_ITEMS = Object.freeze([MENU.desktop, MENU.frameDesktop]);

// Handled directly in the SW: they toggle the pinned state of the URL under the cursor.
export const PIN_ITEMS = Object.freeze([MENU.pin, MENU.framePin, MENU.bgPin]);

// The MENU_ITEMS default titles are the unpinned form of this.
export const pinItemTitle = (pinned, kind = 'image') =>
  `📌 ${pinned ? 'Unpin' : 'Pin'} ${kind === 'video' ? 'video' : 'image'}`;

// Revealed/hidden together, only when the probed <video> carries a poster.
export const PREVIEW_ITEMS = Object.freeze([
  MENU.previewParent, MENU.previewTab, MENU.previewOpen, MENU.previewOpenIncognito,
  MENU.previewModal, MENU.previewModalIncognito, MENU.previewCrop
]);
