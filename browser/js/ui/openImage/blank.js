// The Open-Image dialog's Blank tab: the fill colour with its two presets, the pixel size, and
// Create. Split out of ui/openImageModal.js; a blank canvas has no source and no preview.
import { notify } from '../../utils.js';
import { defaultBlankSizePx } from '../../core/layout.js';

export function createBlankTab({ app, els, pageDims, target, close }) {
  const { colorEl, colorHexEl, widthEl, heightEl, createBtn } = els;

  // Every route that writes the fill writes the hex with it: the presets set the SAME
  // value the picker holds (desktop parity — both land on customColor_).
  const syncColorHex = () => {
    if (colorHexEl) colorHexEl.textContent = String(colorEl.value || '').toUpperCase();
  };
  colorEl.addEventListener('input', syncColorHex);
  els.whiteBtn.addEventListener('click', () => { colorEl.value = '#ffffff'; syncColorHex(); });
  els.blackBtn.addEventListener('click', () => { colorEl.value = '#000000'; syncColorHex(); });

  createBtn.addEventListener('click', async () => {
    const w = parseInt(widthEl.value, 10), h = parseInt(heightEl.value, 10);
    if (!(w >= 1 && w <= 8192) || !(h >= 1 && h <= 8192)) {
      notify('Width and height must be 1–8192 px', 'fail');
      return;
    }
    if (app.image && !(await app.confirm('Replace the current image with a new blank image?', { title: 'Replace image', confirmIcon: 'swap' }))) return;
    const address = target() || undefined;
    app.createBlankImage({ color: colorEl.value, width: w, height: h, address })
      .then(() => { close(); notify(`Blank ${w}×${h} image created`, 'ok'); })
      .catch((err) => notify(err && err.message ? err.message : 'Could not create the image', 'fail'));
  });

  // The size a fresh open offers: the project's own page, in pixels.
  const reset = () => {
    colorEl.value = '#ffffff';
    syncColorHex();
    const px = defaultBlankSizePx(pageDims());
    widthEl.value = px.width;
    heightEl.value = px.height;
  };
  return { reset };
}
