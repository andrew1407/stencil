import { createEditorMode } from './editorMode.js';
import { SEARCH_SECTION } from '../../lib/drop/dragSections.js';
import { statusEl, run, dismiss } from '../panelDom.js';
import { item, submenu, openMenuNodes, closeMenu } from '../row/rowMenu.js';
import { imageDataUrl } from '../openActions.js';
import { bindDataUrlPreview, hidePreview } from '../row/preview.js';
import { scan } from '../list/scan.js';
import { sections } from '../list/sections.js';
import { editorMode } from './editorHandle.js';

// The editor-mode sections shown when this panel stands ON the Stencil editor.
Object.assign(editorMode, createEditorMode({
  setStatus: (text) => { statusEl.textContent = text; },
  run,
  dismiss,
  menu: { item, submenu, open: openMenuNodes, close: closeMenu },
  // A folded results section unfolds, or ticking a page looks like nothing happened.
  onSourceTab: (picked) => {
    if (picked && picked.length) sections.setCollapsed(SEARCH_SECTION, false);
    scan();
  },
  // The panel-side path, so an SVG row imports rasterised (the worker has no DOM to draw it).
  imageDataUrl,
  preview: { bind: bindDataUrlPreview, hide: hidePreview },
}));
