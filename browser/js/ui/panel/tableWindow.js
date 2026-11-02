// The table shape of a RowWindow (rowWindow.js): striped <tr> rows under a sticky header inside the
// panel's #coord-body scroller, a spacer being one empty cell across every column. Used by the points
// table (coordTable.js) and the lines list (lines/list.js).
import { RowWindow } from './rowWindow.js';

const tableShape = (columns) => Object.freeze({
  tag: 'tr',
  stripes: true,
  scroller: (body) => body.closest?.('#coord-body'),
  headPx: (body) => body.parentElement?.tHead?.offsetHeight ?? 0,
  spacer: (px) => {
    const row = document.createElement('tr');
    const cell = document.createElement('td');
    cell.colSpan = columns;
    cell.style.cssText = `height:${px}px;padding:0;border:0;`;
    row.appendChild(cell);
    return row;
  },
});

export const makeTableWindow = (body, { columns, ...opts }) => new RowWindow(body, { ...opts, shape: tableShape(columns) });
