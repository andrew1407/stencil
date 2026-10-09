import { normalizeHex } from '../../../core/settings/accents.js';
import { activeIsBlank, setBlankColor, previewBlankColor } from '../../../core/image/blankImage.js';
import { colorTrial } from './colorTrial.js';
import { registerColorSwatch } from '../../drag/colorDragSwatches.js';
import { wireColorButton } from '../../control/dblReset.js';
// A drag through the picker tries each colour on the stage and the swatch; closing it on a new
// colour recolours the blank once (reload, history reset, save, push), as the desktop's dialog does.
export function wireBlankColorButton(app) {
  const blankBtn = document.getElementById('blank-color-btn');
  const blankInput = document.getElementById('blank-color-input');
  if (blankBtn && blankInput) {
    const swatch = blankBtn.querySelector?.('.blank-color-swatch');
    // A colour dropped on the button lands as the picker's own commit.
    registerColorSwatch(blankBtn, {
      read: () => normalizeHex(app.blankColor) || '#ffffff',
      enabled: () => activeIsBlank(app),
      apply: (value) => { blankInput.value = value; blankInput.dispatchEvent(new Event('change')); },
    });
    const openPicker = colorTrial(blankInput, {
      onTry: (value) => {
        previewBlankColor(app, value);
        if (swatch && activeIsBlank(app)) swatch.style.background = value || app.blankColor || '#ffffff';
      },
      onCommit: (value) => { if (activeIsBlank(app)) setBlankColor(app, value); },
    });
    wireColorButton(blankBtn, {
      open: () => {
        if (!activeIsBlank(app)) return;
        blankInput.value = normalizeHex(app.blankColor) || '#ffffff';
        previewBlankColor(app, null);
        if (swatch) swatch.style.background = app.blankColor || '#ffffff';
        openPicker(blankBtn);
      },
      // The blank's default is white (openImage/sources/blank.js).
      reset: () => { if (activeIsBlank(app)) setBlankColor(app, '#ffffff'); },
    });
  }
}
