// ── Context-menu definitions + click resolution (pure, unit-tested) ──────────
// We create NO explicit "Stencil" parent: Chrome automatically groups an extension's
// multiple top-level items under a single submenu named after the extension ("Stencil
// Image Picker"), so adding our own parent produced a redundant "Stencil Image Picker ›
// Stencil ›" double-nest. Items therefore sit at top level; the only real submenu is
// the video Preview group (a genuine sub-category). The menu still appears ONLY when
// there's something to act on under the cursor — never on plain text / empty page —
// via two item groups, of which at most ONE matches at a time:
//
//   1. NATIVE group — scoped to Chrome's built-in image/video contexts, so it shows on
//      a real <img> or <video> with zero timing risk (the filtering is static; it works
//      even when the MV3 worker is asleep):
//        • image → a real <img>. Acts on the image.
//        • video → a real <video>. Acts on its CURRENT FRAME, with a "Video preview
//          image" submenu for the poster (resolved by the ctxTarget.js probe).
//
//   2. DYNAMIC group (ALL_CONTEXTS) — CSS background-image elements, overlay-buried
//      images and image links have NO native context, so these items are eligible
//      everywhere but DEFAULT-HIDDEN (each carries visible:false). The probe
//      (ctxTarget.js) detects a URL under the cursor and the worker reveals them via
//      contextMenus.update(); with nothing there they stay hidden, so they never show
//      on a plain element. The only cost is MV3's update race: right after the worker
//      wakes, the reveal can land one click late on these non-native elements (the
//      native group is immune).
export const MENU = {
  // Image actions (on <img>).
  open: 'stencil-open',
  openResume: 'stencil-open-resume',
  openIncognito: 'stencil-open-incognito',
  openModal: 'stencil-open-modal',
  openModalIncognito: 'stencil-open-modal-incognito',
  crop: 'stencil-crop',
  // Video current-frame actions (on <video>).
  frameOpen: 'stencil-frame-open',
  frameOpenIncognito: 'stencil-frame-open-incognito',
  frameModal: 'stencil-frame-modal',
  frameModalIncognito: 'stencil-frame-modal-incognito',
  frameCrop: 'stencil-frame-crop',
  // Video poster / preview submenu (on <video>).
  previewParent: 'stencil-preview',
  previewTab: 'stencil-preview-tab',
  previewOpen: 'stencil-preview-open',
  previewOpenIncognito: 'stencil-preview-open-incognito',
  previewModal: 'stencil-preview-modal',
  previewModalIncognito: 'stencil-preview-modal-incognito',
  previewCrop: 'stencil-preview-crop',
  // Background-image / image-link actions. These elements have NO native context,
  // so these items sit on the 'all' context but start HIDDEN; the ctxTarget.js
  // probe + worker reveal them only when a background/linked image is under the
  // cursor — never on a plain element. Same actions as the <img> group.
  bgOpen: 'stencil-bg-open',
  bgOpenResume: 'stencil-bg-open-resume',
  bgOpenIncognito: 'stencil-bg-open-incognito',
  bgOpenModal: 'stencil-bg-open-modal',
  bgOpenModalIncognito: 'stencil-bg-open-modal-incognito',
  bgCrop: 'stencil-bg-crop'
};

// Action item id → what clicking it does. 'open' lands in a new tab; 'open-modal'
// frames the editor in an in-page modal; 'open-tab' opens the image URL in a plain
// tab. `open: 'resume'` switches to a project already held for the same image (else a
// normal import). `target`: 'main' acts on the image / current video frame; 'preview'
// acts on the poster. The 'frame*' items are 'main' too — same behaviour as the image
// open/crop on a <video>; only the wording differs.
const ACTIONS = {
  [MENU.open]: { action: 'open', incognito: false, target: 'main' },
  [MENU.openResume]: { action: 'open', incognito: false, open: 'resume', target: 'main' },
  [MENU.openIncognito]: { action: 'open', incognito: true, target: 'main' },
  [MENU.openModal]: { action: 'open-modal', incognito: false, target: 'main' },
  [MENU.openModalIncognito]: { action: 'open-modal', incognito: true, target: 'main' },
  [MENU.crop]: { action: 'crop', target: 'main' },
  [MENU.frameOpen]: { action: 'open', incognito: false, target: 'main' },
  [MENU.frameOpenIncognito]: { action: 'open', incognito: true, target: 'main' },
  [MENU.frameModal]: { action: 'open-modal', incognito: false, target: 'main' },
  [MENU.frameModalIncognito]: { action: 'open-modal', incognito: true, target: 'main' },
  [MENU.frameCrop]: { action: 'crop', target: 'main' },
  [MENU.previewTab]: { action: 'open-tab', target: 'preview' },
  [MENU.previewOpen]: { action: 'open', incognito: false, target: 'preview' },
  [MENU.previewOpenIncognito]: { action: 'open', incognito: true, target: 'preview' },
  [MENU.previewModal]: { action: 'open-modal', incognito: false, target: 'preview' },
  [MENU.previewModalIncognito]: { action: 'open-modal', incognito: true, target: 'preview' },
  [MENU.previewCrop]: { action: 'crop', target: 'preview' },
  // Background-image / image-link items mirror the <img> actions exactly (they act on
  // the recorded URL the probe found under the cursor).
  [MENU.bgOpen]: { action: 'open', incognito: false, target: 'main' },
  [MENU.bgOpenResume]: { action: 'open', incognito: false, open: 'resume', target: 'main' },
  [MENU.bgOpenIncognito]: { action: 'open', incognito: true, target: 'main' },
  [MENU.bgOpenModal]: { action: 'open-modal', incognito: false, target: 'main' },
  [MENU.bgOpenModalIncognito]: { action: 'open-modal', incognito: true, target: 'main' },
  [MENU.bgCrop]: { action: 'crop', target: 'main' }
};

// Native contexts: image actions on <img>, frame + preview on <video>. NOTHING is on
// 'page'/'all', so these never show on plain elements.
const IMAGE_CONTEXTS = ['image'];
const VIDEO_CONTEXTS = ['video'];
// Background-image / image-link elements have no native context, so this group is
// eligible everywhere ('all') but DEFAULT-HIDDEN — the probe reveals it (toggles each
// item's `visible`) only when there's a real URL under the cursor. Without a parent to
// inherit hidden state from, every item carries its own visible:false and the worker
// flips them together.
const ALL_CONTEXTS = ['all'];

// Flat list passed straight to chrome.contextMenus.create (in order). There is NO
// explicit "Stencil" parent — Chrome auto-groups these top-level items under the
// extension name. The only nested submenu is the video Preview group.
export const MENU_ITEMS = [
  // Image: a real <img>.
  { id: MENU.open, title: '✎ Open image in Stencil editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.openResume, title: '↩ Resume in existing Stencil editor', contexts: IMAGE_CONTEXTS },
  { id: MENU.openIncognito, title: '🕶 Open in Stencil (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.openModal, title: '▣ Open image in Stencil here', contexts: IMAGE_CONTEXTS },
  { id: MENU.openModalIncognito, title: '▣ Open in Stencil here (incognito)', contexts: IMAGE_CONTEXTS },
  { id: MENU.crop, title: '✂ Crop image in Stencil…', contexts: IMAGE_CONTEXTS },
  // Video: act on the CURRENT FRAME.
  { id: MENU.frameOpen, title: '✎ Open current frame in editor', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameOpenIncognito, title: '🕶 Current frame in editor (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameModal, title: '▣ Open current frame in editor here', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameModalIncognito, title: '▣ Current frame here (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.frameCrop, title: '✂ Crop current frame…', contexts: VIDEO_CONTEXTS },
  // Video: the poster / preview image — a genuine sub-category, kept as a submenu.
  { id: MENU.previewParent, title: 'Video preview image', contexts: VIDEO_CONTEXTS },
  { id: MENU.previewTab, parentId: MENU.previewParent, title: '↗ Open preview in a new tab', contexts: VIDEO_CONTEXTS },
  { id: MENU.previewOpen, parentId: MENU.previewParent, title: '✎ Open preview in editor', contexts: VIDEO_CONTEXTS },
  { id: MENU.previewOpenIncognito, parentId: MENU.previewParent, title: '🕶 Preview in editor (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.previewModal, parentId: MENU.previewParent, title: '▣ Open preview in editor here', contexts: VIDEO_CONTEXTS },
  { id: MENU.previewModalIncognito, parentId: MENU.previewParent, title: '▣ Preview here (incognito)', contexts: VIDEO_CONTEXTS },
  { id: MENU.previewCrop, parentId: MENU.previewParent, title: '✂ Crop preview…', contexts: VIDEO_CONTEXTS },
  // Background-image / image-link group: top-level items on the 'all' context, each
  // DEFAULT-HIDDEN. The native image/video items above can't match these elements (no
  // native context), so the worker reveals this group — and only this group — for
  // background/linked images under the cursor. Same actions as the <img> group.
  { id: MENU.bgOpen, title: '✎ Open image in Stencil editor', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenResume, title: '↩ Resume in existing Stencil editor', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenIncognito, title: '🕶 Open in Stencil (incognito)', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenModal, title: '▣ Open image in Stencil here', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgOpenModalIncognito, title: '▣ Open in Stencil here (incognito)', contexts: ALL_CONTEXTS, visible: false },
  { id: MENU.bgCrop, title: '✂ Crop image in Stencil…', contexts: ALL_CONTEXTS, visible: false }
];

// The background/link items the worker reveals/hides together (they have no native
// context, so they start hidden and surface only when the probe finds a URL under the
// cursor). Export so the background SW and tests share one source.
export const DYNAMIC_ITEMS = [
  MENU.bgOpen, MENU.bgOpenResume, MENU.bgOpenIncognito,
  MENU.bgOpenModal, MENU.bgOpenModalIncognito, MENU.bgCrop
];

// Decide what a context-menu click should do. info.srcUrl (the real <img>/<video>'s
// resolved source) wins over `recordedUrl` (for a preview item, the poster URL the
// caller passes in). Returns null when the id isn't ours or there's no URL.
export const resolveContextAction = (info = {}, recordedUrl = null) => {
  const spec = ACTIONS[info.menuItemId];
  if (!spec) return null;
  const src = info.srcUrl || recordedUrl || null;
  if (!src) return null;
  const out = { action: spec.action, src };
  // open/open-modal carry the incognito flag; crop/open-tab don't (keeps the
  // existing result shapes for those two).
  if (spec.action === 'open' || spec.action === 'open-modal') out.incognito = spec.incognito;
  // `open` (resume) and a non-default `target` (preview) are added only when set,
  // so common main open/crop results stay byte-identical to before.
  if (spec.open) out.open = spec.open;
  if (spec.target && spec.target !== 'main') out.target = spec.target;
  return out;
};
