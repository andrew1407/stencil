// ── Context-menu definitions: ids, contexts, the flat create() list ──────────
// Only the video Preview group is a real submenu. Two item groups, at most one matching:
//   1. NATIVE (image/video contexts) — static, works even when the MV3 worker sleeps.
//   2. DYNAMIC (ALL_CONTEXTS) — backgrounds/overlay-buried/linked images have no native
//      context, so start hidden; the worker reveals them via the probe (which primes on
//      hover, well before the menu renders — see content/ctxTarget.js).
//
// TWO ROOTS, and that split is load-bearing: Chrome decides a PARENT's visibility from
// its own `contexts`, never from whether any child is visible — a single 'all' parent
// would paint as "Stencil ▸ (empty)" on plain elements. So the static root declares
// exactly the contexts it serves (action/image/video) and the dynamic group hangs off
// its OWN root, revealed together with its children — never an empty submenu.
export const MENU = {
  // Explicit "Stencil" parent so the submenu is labelled "Stencil" (not the extension
  // name Chrome auto-groups under). With a single top-level item Chrome shows it directly.
  root: 'stencil-root',
  // The dynamic (background / overlay-buried / linked image) group's own root.
  bgRoot: 'stencil-bg-root',
  // Action (toolbar-icon) items — a quick "open a fresh editor" not tied to any image.
  actionOpen: 'stencil-action-open',
  actionOpenIncognito: 'stencil-action-open-incognito',
  // Nested "Open in editor ▸" submenu parents (one per context group), so the flat list
  // becomes Open ▸ / Crop / Pin under the Stencil parent.
  openParent: 'stencil-open-parent',
  frameOpenParent: 'stencil-frame-open-parent',
  bgOpenParent: 'stencil-bg-open-parent',
  // Image actions (on <img>).
  open: 'stencil-open',
  openResume: 'stencil-open-resume',
  openIncognito: 'stencil-open-incognito',
  openModal: 'stencil-open-modal',
  openModalIncognito: 'stencil-open-modal-incognito',
  crop: 'stencil-crop',
  desktop: 'stencil-desktop',
  pin: 'stencil-pin',
  // Video current-frame actions (on <video>).
  frameOpen: 'stencil-frame-open',
  frameOpenIncognito: 'stencil-frame-open-incognito',
  frameModal: 'stencil-frame-modal',
  frameModalIncognito: 'stencil-frame-modal-incognito',
  frameCrop: 'stencil-frame-crop',
  frameDesktop: 'stencil-frame-desktop',
  framePin: 'stencil-frame-pin',
  // Video poster / preview submenu (on <video>).
  previewParent: 'stencil-preview',
  previewTab: 'stencil-preview-tab',
  previewOpen: 'stencil-preview-open',
  previewOpenIncognito: 'stencil-preview-open-incognito',
  previewModal: 'stencil-preview-modal',
  previewModalIncognito: 'stencil-preview-modal-incognito',
  previewCrop: 'stencil-preview-crop',
  // Background-image / image-link actions — no native context, so on 'all' but
  // start hidden; the probe reveals them. Same actions as the <img> group.
  bgOpen: 'stencil-bg-open',
  bgOpenResume: 'stencil-bg-open-resume',
  bgOpenIncognito: 'stencil-bg-open-incognito',
  bgOpenModal: 'stencil-bg-open-modal',
  bgOpenModalIncognito: 'stencil-bg-open-modal-incognito',
  bgCrop: 'stencil-bg-crop',
  bgDesktop: 'stencil-bg-desktop',
  bgPin: 'stencil-bg-pin'
};

// The extension's toolbar-icon (right-click) menu. Items here show on the action icon,
// never on a page element.
const ACTION_CONTEXTS = ['action'];
// Native contexts: image actions on <img>, frame + preview on <video>. NOTHING is on
// 'page'/'all', so these never show on plain elements.
const IMAGE_CONTEXTS = ['image'];
const VIDEO_CONTEXTS = ['video'];
// The static root's own contexts: the union of what its children can serve, so Chrome
// renders that root only where at least one of them applies.
const STATIC_CONTEXTS = [...ACTION_CONTEXTS, ...IMAGE_CONTEXTS, ...VIDEO_CONTEXTS];
// Background/link elements have no native context: group is on 'all', each item carries
// its own visible:false (no parent to inherit); the worker flips them together on a probe hit.
const ALL_CONTEXTS = ['all'];

// Flat list passed straight to chrome.contextMenus.create (in order). Everything hangs
// off one explicit "Stencil" parent (created first), so the submenu reads "Stencil"
// rather than the auto-grouped extension name; the video Preview group nests one deeper.
export const MENU_ITEMS = [
  // The static top-level parent. Its contexts are exactly the ones its children serve —
  // NOT 'all' — so Chrome hides the whole entry on a page/text/background right-click
  // instead of drawing an empty submenu.
  { id: MENU.root, title: 'Stencil', contexts: STATIC_CONTEXTS },
  // Toolbar-icon menu: open a fresh Stencil editor (no image). The incognito one opens it
  // in an incognito window so the editor's project storage is throwaway. 'action' context
  // → shown on the extension icon's right-click menu, never on a page element.
  { id: MENU.actionOpen, parentId: MENU.root, title: '✎ Open Stencil editor', contexts: ACTION_CONTEXTS },
  { id: MENU.actionOpenIncognito, parentId: MENU.root, title: '🕶 Open Stencil editor (incognito)', contexts: ACTION_CONTEXTS },
  // Image: a real <img>. The five open variants nest under an "Open in editor ▸" parent;
  // Crop + Pin stay at the top level (single actions).
  { id: MENU.openParent, parentId: MENU.root, title: '✎ Open in editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.open, parentId: MENU.openParent, title: '↗ New tab', contexts: IMAGE_CONTEXTS },
  { id: MENU.openResume, parentId: MENU.openParent, title: '↩ Resume existing editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.openIncognito, parentId: MENU.openParent, title: '🕶 New tab (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.openModal, parentId: MENU.openParent, title: '▣ Here', contexts: IMAGE_CONTEXTS },
  { id: MENU.openModalIncognito, parentId: MENU.openParent, title: '▣ Here (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.crop, parentId: MENU.root, title: '✂ Crop image…', contexts: IMAGE_CONTEXTS },
  // Hand the image to the desktop app via its stencil:// URL scheme. Default-hidden; the SW
  // reveals it only when a desktop scheme is configured (syncDesktopMenuVisibility).
  { id: MENU.desktop, parentId: MENU.root, title: '🖥 Open in desktop app', contexts: IMAGE_CONTEXTS, visible: false },
  { id: MENU.pin, parentId: MENU.root, title: '📌 Pin image', contexts: IMAGE_CONTEXTS },
  // Video: act on the CURRENT FRAME. Open variants nest under their own parent.
  { id: MENU.frameOpenParent, parentId: MENU.root, title: '✎ Open current frame', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameOpen, parentId: MENU.frameOpenParent, title: '↗ New tab', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameOpenIncognito, parentId: MENU.frameOpenParent, title: '🕶 New tab (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameModal, parentId: MENU.frameOpenParent, title: '▣ Here', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameModalIncognito, parentId: MENU.frameOpenParent, title: '▣ Here (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameCrop, parentId: MENU.root, title: '✂ Crop current frame…', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameDesktop, parentId: MENU.root, title: '🖥 Open frame in desktop app', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.framePin, parentId: MENU.root, title: '📌 Pin video', contexts: VIDEO_CONTEXTS },
  // Video: the poster / preview image — a genuine sub-category, kept as a submenu.
  // Default-hidden: the worker reveals it (PREVIEW_ITEMS) only when the probed video
  // actually has a poster, so posterless videos don't show a dead "no-op" submenu.
  { id: MENU.previewParent, parentId: MENU.root, title: 'Video preview image', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewTab, parentId: MENU.previewParent, title: '↗ Open preview in a new tab', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewOpen, parentId: MENU.previewParent, title: '✎ Open preview in editor', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewOpenIncognito, parentId: MENU.previewParent, title: '🕶 Preview in editor (incognito)', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewModal, parentId: MENU.previewParent, title: '▣ Open preview in editor here', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewModalIncognito, parentId: MENU.previewParent, title: '▣ Preview here (incognito)', contexts: VIDEO_CONTEXTS, visible: false },
  { id: MENU.previewCrop, parentId: MENU.previewParent, title: '✂ Crop preview…', contexts: VIDEO_CONTEXTS, visible: false },
  // Background-image / image-link group: 'all'-context items under their OWN root, all
  // default-hidden and revealed TOGETHER (root included) when the probe finds media the
  // native contexts can't see — so Chrome's "(empty)" submenu state cannot occur.
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
];

// The background/link items the worker reveals/hides together — the group's ROOT first,
// so it never renders without children. Exported so the SW and tests share one source.
export const DYNAMIC_ITEMS = [
  MENU.bgRoot, MENU.bgOpenParent,
  MENU.bgOpen, MENU.bgOpenResume, MENU.bgOpenIncognito,
  MENU.bgOpenModal, MENU.bgOpenModalIncognito, MENU.bgCrop, MENU.bgPin
];

// The desktop-app hand-off items, shown only when a desktop URL scheme is configured
// (syncDesktopMenuVisibility). The STATIC ones toggle on the scheme alone; MENU.bgDesktop
// ALSO needs the probe's background reveal, so the SW gates it in the CTX handler.
export const STATIC_DESKTOP_ITEMS = [MENU.desktop, MENU.frameDesktop];

// The pin items (one per context group). Handled directly in the SW (no editor launch,
// no frame capture) — they toggle the pinned state of the source URL under the cursor.
export const PIN_ITEMS = [MENU.pin, MENU.framePin, MENU.bgPin];

// Pin-item label for a given state: "Pin" when not yet pinned, "Unpin" when it is. The
// SW relabels the pin item (via the ctxTarget probe) so it reflects the source under the
// cursor; the MENU_ITEMS default titles are the unpinned ('Pin …') form of this.
export const pinItemTitle = (pinned, kind = 'image') =>
  `📌 ${pinned ? 'Unpin' : 'Pin'} ${kind === 'video' ? 'video' : 'image'}`;

// The video Preview submenu items the worker reveals/hides together — shown only when
// the probed <video> actually carries a poster (preview image). Same source-of-truth
// pattern as DYNAMIC_ITEMS, shared by the SW and tests.
export const PREVIEW_ITEMS = [
  MENU.previewParent, MENU.previewTab, MENU.previewOpen, MENU.previewOpenIncognito,
  MENU.previewModal, MENU.previewModalIncognito, MENU.previewCrop
];
