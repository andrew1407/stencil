// The Open-Image crop's aspect choice (page / plain ratio / custom) and the rows that come and
// go with the Crop tick. Split out of ui/openImageModal.js; the rect itself is openImageCrop.js.
import constants from '../config/constants.json' with { type: 'json' };
import { makeDustRow, makeDustToggle } from './motion/dustRow.js';
const { PAGE_SIZES } = constants;

// The crop's own aspect affects only this preview, never the project. Plain ratios sit beside
// it: every named ISO page shares one ratio (√2), so a single "Page" entry says it all.
const CROP_RATIOS = { '1:1': { width: 1, height: 1 }, '2:3': { width: 2, height: 3 } };

export function createCropRows({ app, els, fitToPage, persist }) {
  const { cropDims, cropSizeRow, cropSizeCustom, cropSizeSel, cropSizeW, cropSizeH, orientBtn } = els;
  let pageKey = 'page', customW = 21, customH = 29.7;
  // NOT getPageDimensions(): that swaps to landscape from the current canvas aspect.
  const pageDims = () => {
    if (pageKey === 'custom') return { width: customW, height: customH };
    if (CROP_RATIOS[pageKey]) return CROP_RATIOS[pageKey];
    return app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : (PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);
  };

  // Crop-only rows fly on the KEYWORD-CHIP recipe (motion/dustRow.js) so both surfaces play the
  // same numbers. The play belongs to the CHECKBOX alone, or every tab switch replays an arrival.
  let byUser = false;
  const syncDims = makeDustRow(cropDims);
  // Its own flight, separate from the row's: the row's cloud must stay single-line, or disabling
  // Crop with Custom picked scatters onto the Incognito row under it (user report).
  const syncCustom = makeDustRow(cropSizeCustom, 'inline-flex');
  // 'flex', not the factory's default 'block': this is a .vs-row, and 'block' stacked the label
  // and the select (user report). Its cloud is scoped to what the row holds (user report).
  const syncRow = makeDustRow(cropSizeRow, 'flex',
    () => (pageKey === 'custom' ? cropSizeRow.querySelector('.oi-crop-size') : cropSizeSel));
  // orientBtn sits INLINE beside the checkbox/caption in a row those two already keep
  // open — no height of its own to collapse, so the toggle's cloud without the row-slide.
  const syncOrient = makeDustToggle(orientBtn);

  // Silent, and BEFORE the row's own cloud: collapsing Custom's fields first means the row
  // disintegrates from its plain single-line shape, not the taller box with them still in it.
  const syncCropUi = (shown) => {
    syncCustom(shown && pageKey === 'custom', false);
    syncOrient(shown, byUser);
    syncRow(shown, byUser);
  };
  const syncDimsRow = (cropping) => { syncDims(cropping, byUser); byUser = false; };
  const hideDims = () => syncDims(false, false);
  const hideAll = () => {
    syncDims(false, false);
    syncOrient(false, false);
    syncCustom(false, false);
    syncRow(false, false);
  };

  // Unlike the orientation flip, an arbitrary new aspect has no reciprocal to carry the old box
  // across, so it resets to the same fresh default a first Crop tick gets.
  const applyCropPageChange = () => { if (fitToPage()) persist(); };
  cropSizeSel.addEventListener('change', () => {
    pageKey = cropSizeSel.value;
    syncCustom(pageKey === 'custom', true);
    applyCropPageChange();
  });
  cropSizeW.addEventListener('input', () => {
    customW = parseFloat(cropSizeW.value) || customW;
    if (pageKey === 'custom') applyCropPageChange();
  });
  cropSizeH.addEventListener('input', () => {
    customH = parseFloat(cropSizeH.value) || customH;
    if (pageKey === 'custom') applyCropPageChange();
  });

  // Starts on "Page" — the blank size a fresh open reads still wants the project's own page,
  // not whatever ratio the crop selector was left on from a prior open.
  const reset = () => {
    pageKey = 'page';
    customW = app.customPageWidth; customH = app.customPageHeight;
    cropSizeSel.value = pageKey;
    cropSizeW.value = customW; cropSizeH.value = customH;
  };

  return { pageDims, syncCropUi, syncDimsRow, hideDims, hideAll, reset,
           markUserChange: () => { byUser = true; } };
}
