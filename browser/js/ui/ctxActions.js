import { notify, supportsShareFiles } from '../utils.js';
import { icon } from './icons.js';
import { setChecked, swapCheckGlyph } from './controlSwap.js';
import { wireAltPreview } from './exportPreview.js';
import { EXPORT_VARIANTS } from './exportVariants.js';

// The context menu's image, layout, clipboard and draw rows.
export const wireCtxActions = (app, { menu, closeMenu, wireSubmenu }) => {

  // Nested submenu parents: the top-level loop only wires direct menu children.
  wireSubmenu(document.getElementById('ctx-copy-img'), document.getElementById('ctx-copy-img-sub'));
  wireSubmenu(document.getElementById('ctx-dl-img'), document.getElementById('ctx-dl-img-sub'));

  // Each variant row copies/downloads its own literal variant; Alt+hover previews it.
  const IMG_ACTIONS = [
    ['ctx-copy-img-', v => app.export.copyImageToClipboard(v)],
    ['ctx-dl-img-', v => app.export.saveImage(v)],
  ];
  for (const [prefix, run] of IMG_ACTIONS) {
    for (const v of EXPORT_VARIANTS) {
      const item = document.getElementById(`${prefix}${v}`);
      item.addEventListener('click', () => { closeMenu(); run(v); });
      wireAltPreview(item, app, v);
    }
  }

  const shareItem = document.getElementById('ctx-share-img');
  if (shareItem && supportsShareFiles()) shareItem.style.display = '';
  // shareImage() first, closeMenu() after: the Web Share API needs the call inside the
  // click's user-activation window, and the close flight is dead weight ahead of it.
  shareItem?.addEventListener('click', () => {
    app.export.shareImage(); closeMenu();
  });

  document.getElementById('ctx-dl-layout').addEventListener('click', () => {
    closeMenu(); app.export.downloadJSON();
  });

  document.getElementById('ctx-ul-layout').addEventListener('click', () => {
    closeMenu(); document.getElementById('upload-json').click();
  });

  document.getElementById('ctx-copy-layout').addEventListener('click', () => {
    closeMenu(); app.export.copyLayoutToClipboard();
  });

  document.getElementById('ctx-paste-img').addEventListener('click', async () => {
    closeMenu();
    try {
      const items = await navigator.clipboard.read();
      for (const item of items) {
        const imgType = item.types.find(t => t.startsWith('image/'));
        if (imgType) {
          if (app.image && !(await app.confirm('Replace current image with pasted image?', { title: 'Replace image', confirmIcon: 'paste' }))) {
            notify('Image paste canceled', 'info');
            return;
          }
          const blob = await item.getType(imgType);
          const file = new File([blob], 'pasted-image.png', { type: imgType });
          app.loadImageFromFile(file);
          notify('Image pasted from clipboard', 'ok');
          return;
        }
      }
      notify('No image found in clipboard', 'fail');
    } catch (err) {
      notify('Clipboard read failed: ' + (err.message || err), 'fail');
    }
  });

  document.getElementById('ctx-paste-layout').addEventListener('click', async () => {
    closeMenu();
    try {
      const text = await navigator.clipboard.readText();
      if (!text) { notify('Clipboard is empty', 'fail'); return; }
      let data = null;
      try {
        data = JSON.parse(text);
      } catch {
        /* not JSON — left as null, the guard below notifies the user */
      }
      if (!data || !Array.isArray(data.lines)) {
        notify('Clipboard does not contain a layout JSON', 'fail');
        return;
      }
      app.export.applyPastedLayout(data);
    } catch (err) {
      notify('Clipboard read failed: ' + (err.message || err), 'fail');
    }
  });

  document.getElementById('ctx-draw-toggle').addEventListener('click', () => {
    closeMenu();
    if (app.isDrawing) app.stopDrawingMode();
    else if (app.image) app.startDrawingMode();
  });

  // Switch to that mode and start drawing; a selected line is continued (one-shot).
  document.getElementById('ctx-draw-line').addEventListener('click', () => {
    closeMenu();
    if (!app.image) { notify('Load an image first', 'fail'); return; }
    app.setDrawMode('line');
    app.startDrawingMode();
    notify('Drag to draw a line', 'info');
  });

  document.getElementById('ctx-draw-rect').addEventListener('click', () => {
    closeMenu();
    if (!app.image) { notify('Load an image first', 'fail'); return; }
    app.setDrawMode('rect');
    app.startDrawingMode();
    notify('Drag to draw a rectangle', 'info');
  });

  document.getElementById('ctx-show-points').addEventListener('click', () => {
    app.showPoints = !app.showPoints;
    setChecked(document.getElementById('show-points'), app.showPoints);
    app.renderer.redraw(); app.storage.save();
    swapCheckGlyph(document.getElementById('ctx-chk-points'), app.showPoints ? icon('check', { size: 14 }) : '');
  });

  document.getElementById('ctx-show-lines').addEventListener('click', () => {
    app.showLines = !app.showLines;
    setChecked(document.getElementById('show-lines'), app.showLines);
    app.renderer.redraw(); app.storage.save();
    swapCheckGlyph(document.getElementById('ctx-chk-lines'), app.showLines ? icon('check', { size: 14 }) : '');
  });

  document.getElementById('ctx-clear-lines').addEventListener('click', () => {
    closeMenu();
    app.clearAllLines();
  });
};
