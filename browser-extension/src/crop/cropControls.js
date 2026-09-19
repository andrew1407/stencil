// The two things that decide the crop's ASPECT; every change re-centres the rect.
import { pageSizeOptions } from '../lib/cropGeometry.js';

export const createCropControls = ({ state, resetCrop, swapCrop = resetCrop }) => {
  // Custom first, then every ISO A/B/C format from the shared table (canonical order).
  const pageSel = document.getElementById('page-select');
  pageSel.innerHTML = '<option value="custom">Custom</option>' + pageSizeOptions();

  const syncPageControls = () => {
    pageSel.value = state.page;
    document.getElementById('custom-dims').hidden = state.page !== 'custom';
  };

  const syncOrientationButtons = () => {
    document.querySelectorAll('#orient-seg button').forEach(b => b.classList.toggle('active', (b.dataset.album === 'true') === state.album));
  };

  const onCustom = () => {
    const w = parseFloat(document.getElementById('custom-w').value);
    const h = parseFloat(document.getElementById('custom-h').value);
    if (w > 0) state.customW = w;
    if (h > 0) state.customH = h;
    if (state.page === 'custom') resetCrop();
  };

  pageSel.addEventListener('change', () => {
    state.page = pageSel.value;
    syncPageControls();
    resetCrop();
  });
  document.getElementById('orient-seg').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    state.album = b.dataset.album === 'true';
    syncOrientationButtons();
    swapCrop();
  });

  return { syncPageControls, syncOrientationButtons, onCustom };
};
