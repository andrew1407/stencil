// The Lines tab's colour picker: one hidden colour field for the list, anchored to the swatch that
// asked. A pick recolours the selected line through the selection bar's own path
// (applySelectionChange) and keeps the line's opacity, which a colour field cannot carry.
import { anchorPickerInput, cssColorParts, cssWithAlpha } from '../../utils.js';
import { normalizeHex } from '../../core/settings/accents.js';
import { applySelectionChange } from '../../core/line/selection.js';

const withLineAlpha = (line, hex) => cssWithAlpha(normalizeHex(hex) ?? hex, cssColorParts(line.color || '').alpha);
const editable = (app, idx) => !app.compareReadOnly() && app.selectedLineIdx === idx && !!app.lines[idx];

// Commits re-populate the selection bar, whose own swatch would otherwise show the old colour.
const recolor = (app, idx, hex, commit) => {
  applySelectionChange(app, 'color', withLineAlpha(app.lines[idx], hex), { commit });
  if (commit) app.showSelectionPanel(app.lines[idx]);
};

export const createSwatchPicker = (host) => {
  const input = document.createElement('input');
  input.type = 'color';
  input.className = 'lines-color-picker';
  input.tabIndex = -1;
  input.setAttribute('aria-hidden', 'true');
  let target = null;
  const apply = (commit) => () => {
    if (target && editable(target.app, target.idx)) recolor(target.app, target.idx, input.value, commit);
  };
  input.addEventListener('input', apply(false));
  input.addEventListener('change', apply(true));
  host.appendChild(input);

  return Object.freeze({
    open(app, idx, swatch) {
      if (!editable(app, idx)) return;
      target = { app, idx };
      input.value = normalizeHex(cssColorParts(app.lines[idx].color || app.color).hex) ?? normalizeHex(app.color) ?? '#ffff00';
      anchorPickerInput(input, swatch);
      try { input.showPicker(); } catch { input.click(); }
    },
    // Back to the colour a new line takes, the toolbar's.
    reset(app, idx) {
      if (editable(app, idx)) recolor(app, idx, app.color, true);
    },
  });
};
