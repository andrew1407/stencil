// The "Lines" tab list: the app keeps the state and the canvas redraw; this only paints rows.
import { icon } from './icons.js';
import { leaveThenRemove } from './motion.js';
import { fillState } from '../core/layout.js';

// Class toggle only — the list is never scrolled by a canvas hover.
export const applyLinesListHover = (app) => {
  const el = document.getElementById('lines-list');
  if (!el) return;
  el.querySelectorAll('.lines-row').forEach(r => {
    r.classList.toggle('lines-row-hover', parseInt(r.dataset.idx) === app.hoverLineIdx);
  });
};

// One row per committed line, reflecting the selection. No-op unless the Lines tab is
// showing, so the redraw/updateButtons hooks that call it stay cheap.
export const renderLinesList = (app) => {
  const el = document.getElementById('lines-list');
  if (!el || el.style.display === 'none') return;
  el.replaceChildren();
  if (!app.lines.length) {
    const empty = document.createElement('div');
    empty.className = 'lines-empty';
    empty.textContent = 'No lines yet.';
    el.appendChild(empty);
    return;
  }
  app.lines.forEach((line, i) => {
    const row = document.createElement('div');
    row.className = 'lines-row' + (app.isLineSelected(i) ? ' lines-row-selected' : '');
    if (i === app.hoverLineIdx) row.classList.add('lines-row-hover');
    row.dataset.idx = String(i);
    row.addEventListener('mouseenter', () => app.setListHoverLine(i));
    row.addEventListener('mouseleave', () => app.setListHoverLine(-1));

    const swatch = document.createElement('span');
    swatch.className = 'lines-swatch';
    swatch.style.background = line.locked && !fillState(line, app.defaultFillColor).enabled
      ? 'transparent' : (line.color || '#ffff00');
    swatch.style.borderColor = line.color || '#ffff00';

    const label = document.createElement('span');
    label.className = 'lines-label';
    const np = line.points.length;
    const parts = [`Line ${i + 1}`, `${np} pt${np === 1 ? '' : 's'}`];
    if (line.locked) parts.push('area');
    label.textContent = parts.join(' · ');

    const rm = document.createElement('button');
    rm.className = 'lines-remove btn-icon';
    rm.type = 'button';
    rm.dataset.title = 'Remove line';
    rm.setAttribute('aria-label', `Remove line ${i + 1}`);
    rm.innerHTML = icon('trash', { size: 13 });
    rm.addEventListener('click', (e) => {
      e.stopPropagation();
      if (app.compareReadOnly()) return; // read-only compare view
      leaveThenRemove(row, () => app.removeLine(i));
    });

    row.addEventListener('click', (e) => {
      app.selectLineFromList(i, (e.ctrlKey || e.metaKey) && e.shiftKey);
    });
// Focusable so Delete/Backspace can be scoped to this list, like the points table's rows;
// the global Alt+Delete is untouched.
    row.tabIndex = 0;
    row.addEventListener('keydown', (e) => {
      if (e.key !== 'Delete' && e.key !== 'Backspace') return;
      if (app.compareReadOnly()) return; // read-only compare view
      e.preventDefault();
      e.stopPropagation();
      app.removeLine(i);
      const rows = el.querySelectorAll('.lines-row');
      if (rows.length) rows[Math.min(i, rows.length - 1)].focus();
    });
    row.append(swatch, label, rm);
    el.appendChild(row);
  });
};

