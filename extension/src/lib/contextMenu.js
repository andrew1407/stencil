// Context-menu click resolution and the visibility model behind contextMenuItems.js's
// two-root split: no visible parent without children.
import { MENU, MENU_ITEMS } from './contextMenuItems.js';

export {
  DYNAMIC_ITEMS, MENU, MENU_ITEMS, PIN_ITEMS, PREVIEW_ITEMS, STATIC_DESKTOP_ITEMS, pinItemTitle,
} from './contextMenuItems.js';

// open:'resume' switches to an existing project; target 'preview' is the poster.
const ACTIONS = {
  [MENU.open]: { action: 'open', incognito: false, target: 'main' },
  [MENU.openResume]: { action: 'open', incognito: false, open: 'resume', target: 'main' },
  [MENU.openIncognito]: { action: 'open', incognito: true, target: 'main' },
  [MENU.openModal]: { action: 'open-modal', incognito: false, target: 'main' },
  [MENU.openModalIncognito]: { action: 'open-modal', incognito: true, target: 'main' },
  [MENU.crop]: { action: 'crop', target: 'main' },
  [MENU.desktop]: { action: 'desktop', target: 'main' },
  [MENU.frameOpen]: { action: 'open', incognito: false, target: 'main' },
  [MENU.frameOpenIncognito]: { action: 'open', incognito: true, target: 'main' },
  [MENU.frameModal]: { action: 'open-modal', incognito: false, target: 'main' },
  [MENU.frameModalIncognito]: { action: 'open-modal', incognito: true, target: 'main' },
  [MENU.frameCrop]: { action: 'crop', target: 'main' },
  [MENU.frameDesktop]: { action: 'desktop', target: 'main' },
  [MENU.previewTab]: { action: 'open-tab', target: 'preview' },
  [MENU.previewOpen]: { action: 'open', incognito: false, target: 'preview' },
  [MENU.previewOpenIncognito]: { action: 'open', incognito: true, target: 'preview' },
  [MENU.previewModal]: { action: 'open-modal', incognito: false, target: 'preview' },
  [MENU.previewModalIncognito]: { action: 'open-modal', incognito: true, target: 'preview' },
  [MENU.previewCrop]: { action: 'crop', target: 'preview' },
  [MENU.bgOpen]: { action: 'open', incognito: false, target: 'main' },
  [MENU.bgOpenResume]: { action: 'open', incognito: false, open: 'resume', target: 'main' },
  [MENU.bgOpenIncognito]: { action: 'open', incognito: true, target: 'main' },
  [MENU.bgOpenModal]: { action: 'open-modal', incognito: false, target: 'main' },
  [MENU.bgOpenModalIncognito]: { action: 'open-modal', incognito: true, target: 'main' },
  [MENU.bgCrop]: { action: 'crop', target: 'main' },
  [MENU.bgDesktop]: { action: 'desktop', target: 'main' }
};

// `bg`: a plain image URL the native contexts can't see (a <video> has its own native
// group). `preview`: the probed <video> carries a poster, so the submenu has content.
export const menuVisibilityFor = (data) => ({
  bg: !!(data && data.url && !data.video),
  preview: !!(data && data.video && data.poster),
});

// Which items Chrome would draw: an item shows when its own `contexts` match AND it is
// visible, and a child only if its whole parent chain does. `revealed` = ids flipped on.
export const visibleMenu = (context, revealed = []) => {
  const on = new Set(revealed);
  const byId = new Map(MENU_ITEMS.map((i) => [i.id, i]));
  const matches = (i) => i.contexts.includes(context) || i.contexts.includes('all');
  const shown = (i) => (i.visible === false ? on.has(i.id) : !on.has(`!${i.id}`));
  const visible = (i) => {
    if (!matches(i) || !shown(i)) return false;
    const parent = i.parentId ? byId.get(i.parentId) : null;
    return parent ? visible(parent) : true;
  };
  return MENU_ITEMS.filter(visible).map((i) => i.id);
};

// info.srcUrl wins over `recordedUrl` (the poster URL for preview items).
export const resolveContextAction = (info = {}, recordedUrl = null) => {
  const spec = ACTIONS[info.menuItemId];
  if (!spec) return null;
  const src = info.srcUrl || recordedUrl || null;
  if (!src) return null;
  const out = { action: spec.action, src };
  if (spec.action === 'open' || spec.action === 'open-modal') out.incognito = spec.incognito;
  // Added only when set, so common open/crop results keep their shape.
  if (spec.open) out.open = spec.open;
  if (spec.target && spec.target !== 'main') out.target = spec.target;
  return out;
};
