import { unitToCm } from '../../utils.js';
import { enhanceSelect } from '../customSelect.js';
export function wirePageAndDisplayControls(app) {
  document.getElementById('page-size').addEventListener('change', e => app.settings.setPageSize(e.target.value));
  document.getElementById('custom-page-width').addEventListener('change', e => {
    // Inputs are typed in the active unit; the setter stores cm.
    const v = parseFloat(e.target.value);
    app.settings.setCustomPageWidth(Number.isNaN(v) ? 21 : unitToCm(v, app.unit));
  });
  document.getElementById('custom-page-height').addEventListener('change', e => {
    const v = parseFloat(e.target.value);
    app.settings.setCustomPageHeight(Number.isNaN(v) ? 29.7 : unitToCm(v, app.unit));
  });
  const unitSel = document.getElementById('unit-select');
  if (unitSel) unitSel.addEventListener('change', e => app.settings.setUnit(e.target.value));
  // The native <select>s stay the state source, so the change listeners and setVal(...) keep
  // working behind the app's own dropdown.
  enhanceSelect(document.getElementById('page-size'), { search: true });
  enhanceSelect(unitSel);
  document.getElementById('show-points').addEventListener('change', e => app.settings.setShowPoints(e.target.checked));
  document.getElementById('show-lines').addEventListener('change', e => app.settings.setShowLines(e.target.checked));
  const compareSel = document.getElementById('compare-mode');
  if (compareSel) compareSel.addEventListener('change', e => app.settings.setCompareMode(e.target.value));
}
