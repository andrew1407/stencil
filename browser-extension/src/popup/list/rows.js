// The image list's rows: every row while the list is short, a RowWindow (lib/virtualList/rowWindow.js)
// of the rows on screen once it is long, each unknown-size image measured as its row scrolls into view.
// Rows are built by popup/row/row.js; applyFilters (filters.js) decides which images are listed.
import { RowWindow } from '../../lib/virtualList/rowWindow.js';
import { passesFilters } from '../../lib/highlight/filters.js';
import { filterLeave, createFilterTransition } from '../../lib/motion.js';
import { dimText } from '../../lib/rowModel.js';
import { listEl } from '../panelDom.js';
import { buildRow } from '../row/row.js';
import { getDraggingRow } from '../gestures.js';
import { state, rowElFor } from './model.js';
import { filters, renderCount } from './filters.js';
import { createMeasureWatch } from './measureWatch.js';

const WINDOW_ROWS = 100;
const GAP_PX = 6;   // .list's flex gap (popup.css): a spacer adds one beside its neighbour, so it gives one back

const listShape = Object.freeze({
  tag: 'li',
  scroller: (body) => body,
  spacer: (px) => {
    const li = document.createElement('li');
    li.style.cssText = `height:${Math.max(0, px - GAP_PX)}px;flex:none;padding:0;margin:0;`;
    return li;
  },
});

const sizes = createMeasureWatch({
  makeObserver: (onEntries) => new IntersectionObserver(onEntries, { root: listEl, rootMargin: '200px' }),
  measure: (image, li, done) => measure(image, li, done),
});

const rowFor = (image) => {
  const li = buildRow(image);
  sizes.watch(li, image);
  return li;
};

const rowWindow = new RowWindow(listEl, {
  shape: listShape,
  indexAttr: 'data-idx',
  threshold: WINDOW_ROWS,
  build: (i) => {
    const li = rowFor(state.filtered[i]);
    li.dataset.idx = String(i);
    return li;
  },
  onRender: () => sizes.sweep(),
  isHeld: () => !!getDraggingRow(),
});

// A leaving ghost belongs to no image and holds no place in the window.
export const filterTransition = createFilterTransition({
  list: listEl,
  onLeave: (li) => {
    sizes.forget(li);
    delete li.dataset.idx;
  },
});

// Lists state.filtered: the window when it is long, else every row.
export const showRows = () => {
  if (!rowWindow.show(state.filtered.length)) listEl.replaceChildren(...state.filtered.map(rowFor));
  sizes.sweep();
};

export const clearRows = () => {
  rowWindow.show(0);
  listEl.replaceChildren();
  sizes.sweep();
};

// `image`'s row, rendered and scrolled to first when the window holds another span; null when unlisted.
export const revealRow = (image) => {
  const i = state.filtered.indexOf(image);
  if (i < 0) return null;
  if (rowWindow.active && !rowElFor(image)) rowWindow.reveal(i);
  return rowElFor(image);
};

// Drops landing in one frame re-render the window once.
let relistFrame = 0;
const relistSoon = () => {
  relistFrame ||= requestAnimationFrame(() => {
    relistFrame = 0;
    if (rowWindow.active) showRows();
  });
};
// A scroll during a row drag leaves the window as it was (isHeld); the drop catches it up.
listEl.addEventListener('dragend', () => { if (rowWindow.active) relistSoon(); });

// A filter dropping the row, not a delete: the light fade, not the scatter. The row on screen now may
// be a rebuild of the one measured.
const dropFiltered = (image, measured) => {
  const li = rowElFor(image)?.parentElement ?? measured;
  filterLeave(li, () => {
    li.remove();
    state.filtered = state.filtered.filter((it) => it !== image);
    renderCount();
    if (rowWindow.active) relistSoon();
  });
};

const measure = (image, li, done) => {
  const probe = new Image();
  probe.onerror = done;
  probe.onload = () => {
    done();
    image.w = probe.naturalWidth;
    image.h = probe.naturalHeight;
    image.measured = true;
    const dim = (rowElFor(image) ?? li).querySelector('.dim');
    if (dim) dim.textContent = dimText(image);
    if (!passesFilters(image, filters)) dropFiltered(image, li);
  };
  probe.src = image.src;
};
