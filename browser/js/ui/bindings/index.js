// DOM event wiring for the toolbars, keyboard and canvas — one file per control group.
// Pure glue: each binding takes the app and attaches listeners to the app's public
// methods. Nothing here holds state beyond a gesture's own (arrow-pan keys + rAF,
// smooth-zoom target); nothing in js/core/ touches the DOM to do it.
import { wireCanvasScrollbars } from '../canvasScrollbars.js';
import { wireScrollbarHover } from '../control/scrollbarHover.js';
import { enhanceAllSelects } from '../control/customSelect.js';
import { wireStyleControls } from './styleControls.js';
import { wireSelectionPanelControls } from './selectionPanel.js';
import { wirePageAndDisplayControls } from './pageAndDisplay.js';
import { wireFormulaControls } from './formula.js';
import { wireToolbarButtons } from './toolbarButtons.js';
import { wireZoomControls } from './zoom.js';
import { wireScrollPersist } from './scrollPersist.js';
import { wireTheme } from './theme.js';
import { wireKeyboard } from './keyboard.js';
import { wireArrowPan } from './arrowPan.js';
import { wireDropPaste } from './dropPaste.js';
import { wireCanvasPointer } from './canvasPointer.js';
import { wireSmoothZoom } from './smoothZoom.js';
import { wireTypedWords } from './typedWords.js';

// Wire each cohesive control group in source order: document-level listener dispatch
// order depends on it.
export function wireControls(app) {
  wireStyleControls(app);
  wireSelectionPanelControls(app);
  wirePageAndDisplayControls(app);
  wireFormulaControls(app);
  wireToolbarButtons(app);
  wireZoomControls(app);
  wireScrollPersist(app);
  wireTheme(app);
  wireKeyboard(app);
  wireArrowPan(app);
  wireDropPaste(app);
  // The canvas gets its own overlay bars (js/ui/canvasScrollbars.js); every other
  // scrollable's native thumb takes the accent only under the pointer (utils.js).
  wireCanvasScrollbars(document.getElementById('canvas-viewport'));
  wireScrollbarHover();
  wireCanvasPointer(app);
  wireSmoothZoom(app);
  wireTypedWords(app);
  // Last, so every select the layout rendered wears the app's own dropdown rather than the OS
  // one; a second pass over an already-enhanced select is a no-op.
  enhanceAllSelects(document, {
    search: ['page-size'],
    preview: (sel) => {
      if (sel.id === 'image-filter') return (v) => app.settings.preview('imageFilter', v);
      if (sel.id === 'compare-mode') return (v) => app.settings.preview('compareMode', v);
      return null;
    },
  });
}
