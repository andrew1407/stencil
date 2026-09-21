import { supportsShareFiles } from '../../../utils.js';
import { wireExportOptionsMenu } from '../../export/exportOptionsMenu.js';
export function wireStyleControls(app) {
  // The unified Open dialog (openImageModal) owns the Open triggers: #load-image-btn,
  // #open-image-btn and the blank shortcuts all open it.
  wireExportOptionsMenu(document.getElementById('copy-image'), app, {
    run: (variant) => app.export.copyImageToClipboard(variant),
    hotkeyIds: { current: 'copyImage', original: 'copyImageOriginal', tint: 'copyImageTint' },
  });
  const shareBtn = document.getElementById('share-image');
  if (shareBtn) {
    if (supportsShareFiles()) shareBtn.style.display = '';
    shareBtn.addEventListener('click', () => app.export.shareImage());
  }
  document.getElementById('rotate-left').addEventListener('click', () => app.imageModel.rotateImage(-1));
  document.getElementById('rotate-right').addEventListener('click', () => app.imageModel.rotateImage(1));
  document.getElementById('line-color').addEventListener('change', e => app.settings.setColor(e.target.value));
  document.getElementById('point-color').addEventListener('change', e => app.settings.setPointColor(e.target.value));
  // Live drag (input) previews without persisting; the trailing change commits.
  document.getElementById('line-thickness').addEventListener('input', e => app.settings.setThickness(e.target.value, { persist: false }));
  document.getElementById('line-thickness').addEventListener('change', e => app.settings.setThickness(e.target.value));
  document.getElementById('point-size').addEventListener('input', e => app.settings.setPointSize(e.target.value, { persist: false }));
  document.getElementById('point-size').addEventListener('change', e => app.settings.setPointSize(e.target.value));
  document.getElementById('line-style').addEventListener('change', e => app.settings.setLineStyle(e.target.value));
  document.getElementById('image-filter').addEventListener('change', e => app.settings.setImageFilter(e.target.value));
  let filterColorTimer = null;
  document.getElementById('filter-color').addEventListener('input', e => {
    // Reflect the model + mirror immediately; debounce the redraw/persist commit.
    app.filterColor = e.target.value;
    const ctxTint = document.getElementById('ctx-tint-color');
    if (ctxTint) ctxTint.value = e.target.value;
    clearTimeout(filterColorTimer);
    filterColorTimer = setTimeout(() => app.settings.setFilterColor(e.target.value), 80);
  });
}
