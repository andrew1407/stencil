// A Lines-row name edited in place, as numEdit.js edits a number: it opens on the name shown, selected;
// Enter or blur commits one history step (renameLine: trimmed, capped, '' = unnamed), Escape keeps it.
// Desktop twin: the lines table's name editor (app/selection/linesTableParts.hpp).
import { renameLine } from '../../../core/line/selection.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

/** What a row shows for line `idx`: its own name, else the muted "Line N" it stands for. */
export const shownName = (line, idx) => (line?.name ? line.name : `Line ${idx + 1}`);

/** The name `typed` over line `idx` stores: its own "Line N", left as it was, keeps it unnamed. */
export const typedName = (line, idx, typed) =>
  (!line?.name && String(typed ?? '').trim() === shownName(null, idx) ? '' : typed);

// The cell's face for the line as it now reads.
export const paintNameCell = (cell, line, idx) => {
  cell.textContent = shownName(line, idx);
  cell.classList.toggle('lines-name-unset', !line?.name);
};

export const editLineName = (app, cell, idx) => {
  const line = app.lines[idx];
  if (!line || app.compareReadOnly() || cell.querySelector('input')) return;
  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'lines-name-input';
  input.maxLength = constants.LIMITS.lineNameMax;
  input.placeholder = shownName(null, idx);
  input.value = shownName(line, idx);
  input.spellcheck = false;
  cell.replaceChildren(input);
  input.focus();
  input.select();
  let done = false;
  const finish = (commit) => {
    if (done) return;
    done = true;
    // A commit re-renders the list; anything else puts the face back as it was.
    if (!(commit && renameLine(app, idx, typedName(line, idx, input.value)))) paintNameCell(cell, app.lines[idx], idx);
  };
  input.addEventListener('blur', () => finish(true));
  // The field owns its keys: Backspace and Delete edit it, and no row or canvas hotkey fires.
  input.addEventListener('keydown', (e) => {
    e.stopPropagation();
    if (e.key === 'Enter') { e.preventDefault(); finish(true); }
    else if (e.key === 'Escape') { e.preventDefault(); finish(false); }
  });
};
