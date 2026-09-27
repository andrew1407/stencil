import { normalizeHex } from '../../../core/settings/accents.js';
import { activeIsBlank, setBlankColor, previewBlankColor } from '../../../core/image/blankImage.js';
import { colorTrial } from './colorTrial.js';
// A drag through the picker tries each colour on the stage and the swatch; closing it on a new
// colour recolours the blank once (reload, history reset, save, push), as the desktop's dialog does.
export function wireBlankColorButton(app) {
  const blankBtn = document.getElementById('blank-color-btn');
  const blankInput = document.getElementById('blank-color-input');
  if (blankBtn && blankInput) {
    const swatch = blankBtn.querySelector?.('.blank-color-swatch');
    const openPicker = colorTrial(blankInput, {
      onTry: (value) => {
        previewBlankColor(app, value);
        if (swatch && activeIsBlank(app)) swatch.style.background = value || app.blankColor || '#ffffff';
      },
      onCommit: (value) => { if (activeIsBlank(app)) setBlankColor(app, value); },
    });
    blankBtn.addEventListener('click', e => {
      if (!activeIsBlank(app)) return;
      e.stopPropagation();
      blankInput.value = normalizeHex(app.blankColor) || '#ffffff';
      previewBlankColor(app, null);
      if (swatch) swatch.style.background = app.blankColor || '#ffffff';
      openPicker(blankBtn);
    });
  }
}
