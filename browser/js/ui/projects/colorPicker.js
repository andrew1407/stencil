// One hidden colour field for the whole projects list, anchored to whichever row's swatch asked.
// Never display:none and never built in the handler: the native picker opens beside a laid-out box.
import { anchorPickerInput } from '../../utils.js';
import { normalizeHex } from '../../core/settings/accents.js';

export function createColorPicker({ app, list, render }) {
  const colorInput = document.createElement('input');
  colorInput.type = 'color';
  colorInput.className = 'project-color-picker';
  colorInput.tabIndex = -1;
  colorInput.setAttribute('aria-hidden', 'true');
  let colorTarget = null;
  colorInput.addEventListener('change', () => {
    if (!colorTarget) return;
    app.setProjectColor(colorTarget.id, colorInput.value);
    colorTarget.color = colorInput.value;
    render();
  });
  // Beside the list: render() wipes the list's own innerHTML.
  (list.parentElement || list).appendChild(colorInput);

  return (meta, btn) => {
    colorTarget = meta;
    colorInput.value = normalizeHex(meta.color) || '#7c3aed';
    anchorPickerInput(colorInput, btn);
    try {
      if (typeof colorInput.showPicker === 'function') colorInput.showPicker();
      else colorInput.click();
    } catch { colorInput.click(); }
  };
}
