// ── Editor mode: the extra sections shown when the panel stands ON the editor. ──
import { createEditorMode } from './editorMode.js';
import { SEARCH_SECTION } from '../lib/dragSections.js';
import { statusEl, run, dismiss } from './panelDom.js';
import { item, submenu, openMenuNodes, closeMenu } from './rowMenu.js';
import { imageDataUrl } from './openActions.js';
import { bindDataUrlPreview, hidePreview } from './preview.js';
import { scan } from './scan.js';
import { sections } from './sections.js';
import { editorMode } from './editorHandle.js';

// Editor mode: the two extra sections shown when this panel stands ON the Stencil editor —
// the open-editor list, the source-page picker and the import-into-this-editor path. The
// menu is handed over whole, so an editor row's ⋯ is the row menu.
Object.assign(editorMode, createEditorMode({
  setStatus: (text) => { statusEl.textContent = text; },
  run,
  dismiss,
  menu: { item, submenu, open: openMenuNodes, close: closeMenu },
  // Pages were ticked (or re-scanned): the ordinary list below re-scans into them. If the
  // results section is folded, unfold it — ticking a page and seeing nothing appear reads as
  // "the picker is broken" when the images are really just hidden behind a collapsed header.
  onSourceTab: (picked) => {
    if (picked && picked.length) sections.setCollapsed(SEARCH_SECTION, false);
    scan();
  },
  // The bytes an import hands over, resolved by the SAME panel-side path every other open
  // action uses — so an SVG row imports as the rasterised PNG here too, not as raw markup
  // the service worker has no DOM to draw.
  imageDataUrl,
  // The panel's floating magnifier, so an editor row's canvas preview enlarges on hover
  // exactly as an image row's thumbnail does.
  preview: { bind: bindDataUrlPreview, hide: hidePreview },
}));
