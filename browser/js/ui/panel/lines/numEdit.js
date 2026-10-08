// A Lines-row number edited in place, as the points table's px cells are: Enter or blur commits
// one history step through the bar's own path (applyLineChange, clamped to LIMITS), Escape keeps
// the number. Desktop twin: the lines table's spin-box editor (app/selection/linesTableParts.hpp).
import { applyLineChange } from '../../../core/line/selection.js';
import { clampThickness, clampPointSize } from '../../../core/settings/limits.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const { thickMin, thickMax, pointMin, pointMax } = constants.LIMITS;
const FIELDS = Object.freeze({
  thickness: { min: thickMin, max: thickMax, clamp: clampThickness },
  pointSize: { min: pointMin, max: pointMax, clamp: clampPointSize },
});

// A line drawn before it had a size of its own draws at the toolbar's.
const valueOf = (app, line, prop) => (prop === 'pointSize' ? line.pointSize ?? app.pointSize : line[prop]);

export const editLineNumber = (app, cell, idx) => {
  const prop = cell.dataset.prop;
  const field = FIELDS[prop];
  const line = app.lines[idx];
  if (!field || !line || app.compareReadOnly() || cell.querySelector('input')) return;
  const was = valueOf(app, line, prop);
  const input = document.createElement('input');
  input.type = 'number';
  input.className = 'lines-num-input';
  input.min = String(field.min);
  input.max = String(field.max);
  input.step = '1';
  input.value = String(was);
  cell.replaceChildren(input);
  input.focus();
  input.select();
  let done = false;
  const finish = (commit) => {
    if (done) return;
    done = true;
    // Whole numbers, as the bar reads them; an untouched field keeps even a fractional size.
    const typed = input.value === String(was) ? was : parseInt(input.value, 10);
    const next = Number.isFinite(typed) ? field.clamp(typed) : was;
    const take = commit && next !== was;
    cell.textContent = String(take ? next : was);
    // The commit re-renders the list; the bar follows when it holds this line.
    if (take && applyLineChange(app, idx, prop, next) && idx === app.selectedLineIdx) app.showSelectionPanel(app.lines[idx]);
  };
  input.addEventListener('blur', () => finish(true));
  // The field owns its keys: Backspace edits it, arrows step it, and no row or canvas hotkey fires.
  input.addEventListener('keydown', (e) => {
    e.stopPropagation();
    if (e.key === 'Enter') { e.preventDefault(); finish(true); }
    else if (e.key === 'Escape') { e.preventDefault(); finish(false); }
  });
};
