// DOM event wiring for the toolbars, keyboard and canvas — one file per control group.
// Pure glue: each binding takes the app and attaches listeners to the app's public
// methods. Nothing here holds state beyond a gesture's own (arrow-pan keys + rAF,
// smooth-zoom target); nothing in js/core/ touches the DOM to do it.
import { wireCanvasScrollbars } from '../canvas/scrollbars.js';
import { wireScrollbarHover } from '../control/scrollbarHover.js';
import { enhanceAllSelects } from '../control/customSelect.js';
import { installDblReset } from '../control/dblReset.js';
import { wireColorDrag } from '../drag/colorDrag.js';
import { wireStyleControls } from './controls/styleControls.js';
import { wireSelectionPanelControls } from './selectionPanel.js';
import { wirePageAndDisplayControls } from './controls/pageAndDisplay.js';
import { wireFormulaControls } from './controls/formula.js';
import { wireToolbarButtons } from './controls/toolbarButtons.js';
import { wireZoomControls } from './viewport/zoom.js';
import { wireScrollPersist } from './viewport/scrollPersist.js';
import { wireTheme } from './theme.js';
import { wireKeyboard } from './keys/keyboard.js';
import { wireArrowPan } from './viewport/arrowPan.js';
import { wireDropPaste } from './dropPaste.js';
import { wireCanvasPointer } from './canvasPointer.js';
import { wireSmoothZoom } from './viewport/smoothZoom.js';
import { wireTypedWords } from './keys/typedWords.js';
import { wireDrawDoublePress } from './keys/drawDoublePress.js';
import { wireControlState } from '../control/state.js';
import { beginQuarterTurn } from '../motion/quarterTurn.js';
import { beginMirrorFlip } from '../motion/mirrorFlip.js';
import { TUNE } from '../motion/tune.js';

// Wire each cohesive control group in source order: document-level listener dispatch
// order depends on it.
export function wireControls(app) {
  wireControlState(app);
  wireStyleControls(app);
  wireSelectionPanelControls(app);
  wirePageAndDisplayControls(app);
  wireFormulaControls(app);
  wireToolbarButtons(app);
  wireZoomControls(app);
  wireScrollPersist(app);
  wireTheme(app);
  wireKeyboard(app);
  wireDrawDoublePress(app);
  wireArrowPan(app);
  wireDropPaste(app);
  // The canvas gets its own overlay bars (js/ui/canvas/scrollbars.js); every other
  // scrollable's native thumb takes the accent only under the pointer (utils.js).
  wireCanvasScrollbars(document.getElementById('canvas-viewport'));
  wireScrollbarHover();
  wireCanvasPointer(app);
  wireSmoothZoom(app);
  wireTypedWords(app);
  // Both orientation flights run on the quarter turn's clock, the canvas's zoom ease held off.
  const flight = (canvas) => ({ ms: TUNE.ROTATE_MS, easing: TUNE.ROTATE_EASING,
    onStart: () => canvas.classList.add('zoom-no-transition'),
    onEnd: () => canvas.classList.remove('zoom-no-transition') });
  app.quarterTurn = () => {
    const canvas = app.canvas, box = canvas.parentElement;
    return beginQuarterTurn(box, { ...flight(canvas), viewport: box?.closest('.canvas-viewport') });
  };
  app.mirrorFlip = () => beginMirrorFlip(app.canvas.parentElement, flight(app.canvas));
  installDblReset(document);
  wireColorDrag(app);
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
