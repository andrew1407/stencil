// The Lines tab's row gestures, one listener per event type on the list body however often it
// re-renders: hover, select, the two colour swatches, the inline numbers and name, the eye, the bin
// and Delete.
// Desktop twin: app/selection/SelectionPanelLines.cpp.
import { isTypingTarget } from '../../../utils.js';
import { leaveThenRemove } from '../../motion.js';
import { selectLineFromList, setListHoverLine, setLineHidden } from '../../../core/line/selection.js';
import { removeLine } from '../../../core/line/editOps.js';
import { createSwatchPicker } from './swatchPicker.js';
import { editLineNumber } from './numEdit.js';
import { editLineName } from './nameEdit.js';
import { dragsStarted } from '../../drag/iconDrag.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const { doubleClickMs, doubleTapMs } = constants.POPOVER;

// Each swatch class and the colour of the line it edits.
const SWATCHES = Object.freeze([['lines-swatch', 'color'], ['lines-point-swatch', 'pointColor']]);

// The line whose eye was just clicked: its next row plays the toggle, every later render rests.
let eyeToggled = -1;
export const takeEyeToggled = (i) => {
  if (i !== eyeToggled) return false;
  eyeToggled = -1;
  return true;
};

// The body → the app of its latest render, which every listener reads.
const appOf = new WeakMap();

const wire = (body, host) => {
  const rowOf = (e) => e.target?.closest?.('tr.lines-row') || null;
  const indexOf = (row) => parseInt(row.dataset.idx, 10);
  const swatchAt = (i, cls) => body.querySelector?.(`tr.lines-row[data-idx="${i}"] .${cls}`) ?? null;
  let picker = null;
  // A swatch click opens its picker once the double-click window passes, and a second click inside
  // it resets instead. The line's swatch also selects the line, raising the bar; the point's keeps it.
  let pending = null;
  const swatchClick = (app, i, [cls, prop], e) => {
    const again = pending?.idx === i && pending.cls === cls;
    clearTimeout(pending?.timer);
    pending = null;
    picker ??= createSwatchPicker(host);
    const selects = prop === 'color';
    if (again) {
      if (selects) selectLineFromList(app, i);
      picker.reset(app, i, prop);
      return;
    }
    const wait = e.pointerType === 'touch' ? doubleTapMs : doubleClickMs;
    const drags = dragsStarted();   // a second press that dragged the swatch away picks nothing
    pending = { idx: i, cls, timer: setTimeout(() => {
      pending = null;
      if (dragsStarted() !== drags) return;
      if (selects) selectLineFromList(app, i);
      picker.open(app, i, prop, swatchAt(i, cls));
    }, wait) };
  };
  body.addEventListener('mouseover', (e) => {
    const row = rowOf(e);
    if (row && !row.contains(e.relatedTarget)) setListHoverLine(appOf.get(body), indexOf(row));
  });
  body.addEventListener('mouseout', (e) => {
    const row = rowOf(e);
    if (row && !row.contains(e.relatedTarget)) setListHoverLine(appOf.get(body), -1);
  });
  body.addEventListener('click', (e) => {
    const row = rowOf(e);
    if (!row) return;
    const app = appOf.get(body);
    const i = indexOf(row);
    if (e.target.closest('.lines-remove')) {
      e.stopPropagation();
      if (!app.compareReadOnly()) leaveThenRemove(row, () => removeLine(app, i));
      return;
    }
    if (e.target.closest('.lines-eye')) {
      e.stopPropagation();
      eyeToggled = i;
      if (!setLineHidden(app, i, !app.lines[i]?.hidden)) eyeToggled = -1;
      return;
    }
    const ctrlShift = (e.ctrlKey || e.metaKey) && e.shiftKey;
    if (!ctrlShift) {
      const swatch = SWATCHES.find(([cls]) => e.target.closest(`.${cls}`));
      if (swatch) { swatchClick(app, i, swatch, e); return; }
      if (e.target.closest('.lines-num, .lines-name')) return;   // edits on a double-click, never selects
    }
    selectLineFromList(app, i, ctrlShift);
  });
  body.addEventListener('dblclick', (e) => {
    const row = rowOf(e);
    const name = row && e.target.closest('.lines-name');
    if (name) { editLineName(appOf.get(body), name, indexOf(row)); return; }
    const cell = row && e.target.closest('.lines-num');
    if (cell) editLineNumber(appOf.get(body), cell, indexOf(row));
  });
  // Delete/Backspace scoped to a focused row, like the points table's rows; Alt+Delete stays global.
  body.addEventListener('keydown', (e) => {
    const row = rowOf(e);
    const app = appOf.get(body);
    if (!row || (e.key !== 'Delete' && e.key !== 'Backspace') || isTypingTarget(e.target) || app.compareReadOnly()) return;
    e.preventDefault();
    e.stopPropagation();
    const i = indexOf(row);
    removeLine(app, i);
    const rows = body.querySelectorAll('.lines-row');
    if (rows.length) rows[Math.min(i, rows.length - 1)].focus();
  });
};

// `host` keeps the hidden picker field and must outlive the list's re-renders.
export const wireLinesList = (body, host, app) => {
  if (!appOf.has(body)) wire(body, host);
  appOf.set(body, app);
};
