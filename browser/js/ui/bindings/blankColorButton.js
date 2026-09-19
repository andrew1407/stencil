import { anchorPickerInput } from '../../utils.js';
import { normalizeHex } from '../../core/accents.js';
export function wireBlankColorButton(app) {
  const blankBtn = document.getElementById('blank-color-btn');
  const blankInput = document.getElementById('blank-color-input');
  if (blankBtn && blankInput) {
    const applyBlank = () => { if (app.activeIsBlank()) app.setBlankColor(blankInput.value); };
    blankInput.addEventListener('input', applyBlank);
    blankInput.addEventListener('change', applyBlank);
    blankBtn.addEventListener('click', e => {
      if (!app.activeIsBlank()) return;
      e.stopPropagation();
      blankInput.value = normalizeHex(app.blankColor) || '#ffffff';
      anchorPickerInput(blankInput, blankBtn);
      try {
        if (typeof blankInput.showPicker === 'function') blankInput.showPicker();
        else blankInput.click();
      } catch {
        blankInput.click();
      }
    });
  }
}
