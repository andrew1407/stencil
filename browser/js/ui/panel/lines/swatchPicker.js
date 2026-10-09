// The Lines tab's colour picker: one hidden colour field for the list, anchored to the swatch that
// asked. A pick sets that row's line or point colour through the bar's own path (applyLineChange)
// and keeps the colour's opacity, which a colour field cannot carry. Desktop twin:
// app/setup/WindowAssemblySwatch.cpp.
import { cssColorParts, cssWithAlpha } from '../../../utils.js';
import { openColorPicker } from '../../bindings/controls/colorTrial.js';
import { normalizeHex } from '../../../core/settings/accents.js';
import { applyLineChange } from '../../../core/line/selection.js';
import { pointColorOf } from '../../../core/line/render.js';

const shownColor = (line, prop) => (prop === 'pointColor' ? pointColorOf(line) : line.color || '');
const withAlpha = (line, prop, hex) => cssWithAlpha(normalizeHex(hex) ?? hex, cssColorParts(shownColor(line, prop)).alpha);
const editable = (app, idx) => !app.compareReadOnly() && !!app.lines[idx];

// A commit re-shows the bar when it holds this line, whose own swatch would show the old colour.
const recolor = (app, idx, prop, value, commit) => {
  if (applyLineChange(app, idx, prop, value, { commit }) && commit && idx === app.selectedLineIdx)
    app.showSelectionPanel(app.lines[idx]);
};

export const createSwatchPicker = (host) => {
  const input = document.createElement('input');
  input.type = 'color';
  input.className = 'lines-color-picker';
  input.tabIndex = -1;
  input.setAttribute('aria-hidden', 'true');
  let target = null;
  const apply = (commit) => () => {
    if (!target || !editable(target.app, target.idx)) return;
    const { app, idx, prop } = target;
    recolor(app, idx, prop, withAlpha(app.lines[idx], prop, input.value), commit);
  };
  input.addEventListener('input', apply(false));
  input.addEventListener('change', apply(true));
  host.appendChild(input);

  return Object.freeze({
    open(app, idx, prop, swatch) {
      if (!editable(app, idx)) return;
      target = { app, idx, prop };
      const hex = cssColorParts(shownColor(app.lines[idx], prop) || app.color).hex;
      input.value = normalizeHex(hex) ?? normalizeHex(app.color) ?? '#ffff00';
      openColorPicker(input, swatch);
    },
    // Back to what a new line takes: the toolbar's line colour, and its point colour (the line
    // colour while the toolbar's points follow it, core/line/shapeBuilder.js).
    reset(app, idx, prop) {
      if (!editable(app, idx)) return;
      const to = prop === 'color' ? app.color : (app.pointColor || app.color);
      recolor(app, idx, prop, withAlpha(app.lines[idx], prop, to), true);
    },
  });
};
